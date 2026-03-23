//=============================================================================
// Contains all the hooks and interfaces between the emulator interface
// and the main emulator core.
//=============================================================================

#include <array>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>

#include "memmap.h"
#include "apu.h"
#include "gfx.h"
#include "snapshot.h"
#include "cheats.h"
#include "soundux.h"

#include "3dsutils.h"
#include "3dssettings.h"
#include "3dslog.h"
#include "3dsfiles.h"
#include "3dsgpu.h"
#include "3dssound.h"
#include "3dsmenu.h"
#include "3dsui.h"
#include "3dsui_notif.h"
#include "3dsui_img.h"
#include "3dsinput.h"
#include "3dsimpl.h"
#include "3dsimpl_tilecache.h"
#include "3dsimpl_gpu.h"

// Compiled shaders
#include "shader_tiles_shbin.h"
#include "shader_mode7_shbin.h"
#include "shader_screen_shbin.h"

radio_state slotStates[SAVESLOTS_MAX];

static S9xScreenshot screenshot = {0};

extern SCheatData Cheat;

typedef Result (*GSP_CacheCallback)(const void* addr, u32 size);

//---------------------------------------------------------
// Initializes the emulator core.
//
// You must call snd3dsSetSampleRate here to set 
// the CSND's sampling rate.
//---------------------------------------------------------

void setDepthBufferByTex(C3D_RenderTarget* target, C3D_Tex* depthTex)
{
    if (!target || !depthTex) return;

	C3D_FrameBufDepth(&target->frameBuf, depthTex->data, GPU_RB_DEPTH24_STENCIL8);
	target->ownsDepth = true;
}

bool impl3dsInitialize()
{
	// Initialize our CSND engine.
	//
	log3dsWrite("snd3dsSetSampleRate: %d, samples per loop: %d", 32000, 256);
	snd3dsSetSampleRate(32000, 256);

	log3dsWrite("load up and initialize shaders");
    gpu3dsLoadShader(SPROGRAM_SCREEN, (u32 *)shader_screen_shbin, shader_screen_shbin_size, 0);
	gpu3dsLoadShader(SPROGRAM_TILES, (u32 *)shader_tiles_shbin, shader_tiles_shbin_size, 6);
	gpu3dsLoadShader(SPROGRAM_MODE7, (u32 *)shader_mode7_shbin, shader_mode7_shbin_size, 3);

	if (!gpu3dsInitializeShaderUniformLocations()) {
		return false;
	}
	
    // Create all the necessary ingame textures
    //
	// Main screen requires 8-bit alpha, otherwise alpha blending will not work well
	// Mode7 texture requires 16x16 as a minimum
	//
	// Depth texture for the sub / main screens improves performance 
	// -> Games like Axelay, F-Zero now run close to full speed!
	//
	log3dsWrite("allocate textures:");

	u32 defaultTextureParams = GPU_TEXTURE_MAG_FILTER(GPU_NEAREST) | GPU_TEXTURE_MIN_FILTER(GPU_NEAREST) | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_BORDER) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_BORDER);
	u32 mode7Tile0TextureParams = GPU_TEXTURE_MAG_FILTER(GPU_NEAREST) | GPU_TEXTURE_MIN_FILTER(GPU_NEAREST) | GPU_TEXTURE_WRAP_S(GPU_REPEAT) | GPU_TEXTURE_WRAP_T(GPU_REPEAT);
	
	const SGPUTextureConfig vramTexConfig[] = {
		{ defaultTextureParams, SNES_SUB, GPU_RGBA8, 256, 256 },
		{ mode7Tile0TextureParams, SNES_MODE7_TILE_0, GPU_RGBA5551, 16, 16 },
		{ defaultTextureParams, SNES_MAIN_R, GPU_RGBA8, 256, 256 }, // stereo right eye — must be in Bank A before 2MB Mode7 texture
		{ defaultTextureParams, SNES_MODE7_FULL, GPU_RGBA5551, 1024, 1024 },
		{ defaultTextureParams, SNES_MAIN, GPU_RGBA8, 256, 256 },
		{ defaultTextureParams, SNES_DEPTH, GPU_RGBA8, 256, 256 },
	};

    const int totalVramTextures = static_cast<int>(sizeof(vramTexConfig) / sizeof(vramTexConfig[0]));

	for (int i = 0; i < totalVramTextures; i++) 
	{
		SGPU_TEXTURE_ID id = vramTexConfig[i].id;
		SGPUTexture *texture = &GPU3DS.textures[id];

		if (!gpu3dsAllocVramTextureAndTarget(&GPU3DS.textures[id], &vramTexConfig[i])) {
        	log3dsWrite("Unable to allocate vram texture %s", utils3dsTextureIDToString(id));

        	return false;
		}

		log3dsWrite("ingame vram texture \"%s\" dim: %dx%d, size:%.2fkb, format: %s",
			utils3dsTextureIDToString(texture->id),
			texture->tex.width, texture->tex.height,
			(float)texture->tex.size / 1024,
			utils3dsTexColorToString(texture->tex.fmt)
		);
	}

	// Share SNES_DEPTH as the depth buffer for all SNES render targets.
	// Must run after ALL vram textures are allocated (SNES_MAIN_R included).
	{
		C3D_Tex *depthTex = &GPU3DS.textures[SNES_DEPTH].tex;
		setDepthBufferByTex(GPU3DS.textures[SNES_MAIN].target, depthTex);
		setDepthBufferByTex(GPU3DS.textures[SNES_MAIN_R].target, depthTex);
		setDepthBufferByTex(GPU3DS.textures[SNES_SUB].target, depthTex);
	}

	const SGPUTextureConfig lramTexConfig[] = {
		{ defaultTextureParams, SNES_TILE_CACHE, GPU_RGBA5551, 1024, 1024 },
		{ defaultTextureParams, SNES_MODE7_TILE_CACHE, GPU_RGBA5551, 128, 128 }
	};

	const int totalLramTextures = static_cast<int>(sizeof(lramTexConfig) / sizeof(lramTexConfig[0]));

	for (int i = 0; i < totalLramTextures; i++) 
	{
		SGPU_TEXTURE_ID id = lramTexConfig[i].id;
		SGPUTexture *texture = &GPU3DS.textures[id];

		if (!gpu3dsAllocLinearTexture(&GPU3DS.textures[id], &lramTexConfig[i])) {
        	log3dsWrite("Unable to allocate linear ram texture %s", utils3dsTextureIDToString(id));

        	return false;
		}

		log3dsWrite("ingame linear ram texture \"%s\" dim: %dx%d, size:%.2fkb, format: %s",
			utils3dsTextureIDToString(texture->id),
			texture->tex.width, texture->tex.height,
			(float)texture->tex.size / 1024,
			utils3dsTexColorToString(texture->tex.fmt)
		);
	}

	log3dsWrite("allocate vbos:");

	// windowLR, backdrop, fixed color color math, brightness
	int vbo_scene_rect_size = (int)gpu3dsGetNextPowerOf2(sizeof(SRectVertex) * MAX_VERTICES_RECT * 2);

	//  bg0-bg3, obj, sub screen color math
	int vbo_scene_tile_size = (int)gpu3dsGetNextPowerOf2(sizeof(STileVertex) * MAX_VERTICES * 2);

	// bg0-bg1
	int vbo_scene_mode7_line_size = (int)gpu3dsGetNextPowerOf2(sizeof(SMode7LineVertex) * MAX_VERTICES_MODE7_LINE * 2);

	// mode 7 full texture + tile0 = MAX_VERTICES_MODE7_TILE
	int vbo_mode7_tile_size = (int)gpu3dsGetNextPowerOf2(sizeof(SMode7TileVertex) * MAX_VERTICES_MODE7_TILE * 2);

	// background, cover, bezel, ingame, splash, etc.
	int vbo_screen_size = (int)gpu3dsGetNextPowerOf2(sizeof(SQuadVertex) * MAX_VERTICES_QUAD * 2);
	
	SVertexListInfo listInfos[] = {
		{ VBO_SCENE_RECT, vbo_scene_rect_size, sizeof(SRectVertex), 2, { {GPU_SHORT, 2}, {GPU_UNSIGNED_BYTE, 4} } },
		{ VBO_SCENE_TILE, vbo_scene_tile_size, sizeof(STileVertex), 2, { {GPU_SHORT, 3}, {GPU_SHORT, 2} } },
		{ VBO_SCENE_MODE7_LINE, vbo_scene_mode7_line_size, sizeof(SMode7LineVertex), 2, { {GPU_SHORT, 2}, {GPU_FLOAT, 2} } },
		{ VBO_MODE7_TILE, vbo_mode7_tile_size, sizeof(SMode7TileVertex), 1, { {GPU_SHORT, 4} } },
		{ VBO_SCREEN, vbo_screen_size, sizeof(SQuadVertex), 4, { {GPU_FLOAT, 4}, {GPU_FLOAT, 2}, {GPU_UNSIGNED_BYTE, 4}, {GPU_UNSIGNED_BYTE, 4} } },
	};

	bool listAllocated;
	
	for (int i = 0; i < VBO_COUNT; i++) 
	{
		listAllocated = gpu3dsAllocVertexList(&listInfos[i]);

		if (settings3DS.LogFileEnabled) {
			SGPU_VBO_ID id = listInfos[i].id;

			int stride = 0;
				for (int j = 0; j < listInfos[i].totalAttributes; j++) {
				int bytes = listInfos[i].attrFormat[j].format == GPU_FLOAT || listInfos[i].attrFormat[j].format == GPU_BYTE 
				? listInfos[i].attrFormat[j].format + 1 
				: listInfos[i].attrFormat[j].format;

				stride += bytes * listInfos[i].attrFormat[j].count;
			}
			
			log3dsWrite("[%s] size: %.2fkb, vertex size: %dbytes, stride: %d, total attributes: %d",
				utils3dsVboIDToString(id),
				(float)listInfos[i].sizeInBytes / 1024,
				listInfos[i].vertexSize,
				stride,
				GPU3DS.vertices[id].attrInfo.attrCount
			);
		}

		if (!listAllocated)
			break;
	}

	// if any list has been failed to initialize
    if (!listAllocated)
    {
        log3dsWrite("Unable to allocate all vbos");

        return false;
    }

	log3dsWrite("allocate ibo and layer sections");

	gpu3dsResetState();
	gpu3dsInitLayers();
	
	log3dsWrite("-- initialize SNES core --");

	Settings = SSettings{}; 

	Settings.Paused = false;
    Settings.BGLayering = TRUE;
    Settings.SoundBufferSize = 0;
    Settings.CyclesPercentage = 100;
    Settings.APUEnabled = Settings.NextAPUEnabled = TRUE;
    Settings.H_Max = SNES_CYCLES_PER_SCANLINE;
    Settings.SkipFrames = 0;
    Settings.ShutdownMaster = TRUE;
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.FrameTime = Settings.FrameTimeNTSC;
    Settings.DisableSampleCaching = FALSE;
    Settings.DisableMasterVolume = FALSE;
    Settings.Mouse = FALSE;
    Settings.SuperScope = FALSE;
    Settings.MultiPlayer5 = FALSE;
    Settings.ControllerOption = SNES_JOYPAD;
    Settings.SupportHiRes = FALSE;
    Settings.NetPlay = FALSE;
	Settings.NoPatch = TRUE;
    Settings.ServerName [0] = 0;
    Settings.ThreadSound = FALSE;
    Settings.AutoSaveDelay = 3600;       // SRAM auto-save delay in frames (~60 seconds at 60fps)
#ifdef _NETPLAY_SUPPORT
    Settings.Port = NP_DEFAULT_PORT;
#endif
    Settings.ApplyCheats = TRUE;
    Settings.TurboMode = FALSE;
    Settings.TurboSkipFrames = 15;

    Settings.Transparency = FALSE;
    Settings.SixteenBit = TRUE;
    Settings.HBlankStart = (256 * Settings.H_Max) / SNES_HCOUNTER_MAX;

    // Sound related settings.
    Settings.DisableSoundEcho = FALSE;
    Settings.SixteenBitSound = TRUE;
    Settings.SoundPlaybackRate = 32000;
    Settings.Stereo = TRUE;
    Settings.SoundBufferSize = 0;
    Settings.APUEnabled = Settings.NextAPUEnabled = TRUE;
    Settings.InterpolatedSound = TRUE;
    Settings.AltSampleDecode = 0;
    Settings.SoundEnvelopeHeightReading = 1;

    if(!Memory.Init())
    {
        log3dsWrite("Unable to initialize memory");
        
		return false;
    }

	log3dsWrite("Memory initialized");

    if(!S9xInitAPU())
    {
        log3dsWrite("Unable to initialize APU");

        return false;
    }

	log3dsWrite("APU initialized");

    if(!S9xGraphicsInit())
    {
        log3dsWrite("Unable to initialize graphics");

        return false;
    }

	log3dsWrite("S9xGraphics initialized");

    if(!S9xInitSound (7, Settings.Stereo, Settings.SoundBufferSize))
    {
        log3dsWrite("Unable to initialize sound");

        return false;
    }

	log3dsWrite("S9xSound initialized");

    so.playback_rate = Settings.SoundPlaybackRate;
    so.stereo = Settings.Stereo;
    so.sixteen_bit = Settings.SixteenBitSound;
    so.buffer_size = 32768;
    so.encoded = FALSE;

    return true;
}

//---------------------------------------------------------
// Finalizes and frees up any resources.
//---------------------------------------------------------
void impl3dsFinalize()
{
	log3dsWrite("dealloc vbos");
    for (int i = 0; i < VBO_COUNT; i++) {
        gpu3dsDeallocVertexList(&GPU3DS.vertices[i]);
    }

	log3dsWrite("dealloc ibo");
	gpu3dsDeallocLayers();

	log3dsWrite("destroy textures");
    for (int i = 0; i < TEX_COUNT; i++) {
        gpu3dsDestroyTexture(&GPU3DS.textures[i]);
    }

	log3dsWrite("S9xGraphicsDeinit");
    S9xGraphicsDeinit();

	log3dsWrite("S9xDeinitAPU");
    S9xDeinitAPU();
    
	log3dsWrite("Memory.Deinit");
    Memory.Deinit();
}


//---------------------------------------------------------
// Mix sound samples into a temporary buffer.
//
// This gives time for the sound generation to execute
// from the 2nd core before copying it to the actual
// output buffer.
//---------------------------------------------------------
void impl3dsGenerateSoundSamples()
{
	S9xSetAPUDSPReplay ();
	S9xMixSamplesIntoTempBuffer(256 * 2);
}


//---------------------------------------------------------
// Mix sound samples into a temporary buffer.
//
// This gives time for the sound generation to execute
// from the 2nd core before copying it to the actual
// output buffer.
//---------------------------------------------------------
void impl3dsOutputSoundSamples(short *leftSamples, short *rightSamples)
{
	S9xApplyMasterVolumeOnTempBufferIntoLeftRightBuffers(
		leftSamples, rightSamples, 256 * 2);

}

void impl3dsUpdateUiAssets() {
    const struct UiAssetConfig {
        SGPU_TEXTURE_ID id;
        int settingValue;
        const char* folderName;
    } assets[] = {
        { UI_OVERLAY,   static_cast<int>(settings3DS.GameOverlay),      "overlays" },
        { UI_BG_GAME,   static_cast<int>(settings3DS.GameScreenBg),     "backgrounds/game_screen" },
        { UI_BG_SECOND, static_cast<int>(settings3DS.SecondScreenBg),   "backgrounds/second_screen"  }
    };

    char fileName[PATH_MAX];

    for (const auto& asset : assets) {
        Setting::AssetMode mode = static_cast<Setting::AssetMode>(asset.settingValue);
        bool externalAssetActive = false;

        if (mode == Setting::AssetMode::Adaptive || mode == Setting::AssetMode::CustomOnly) {
            file3dsGetRelatedPath(Memory.ROMFilename, fileName, sizeof(fileName), ".png", asset.folderName, true);    
			
			// load custom asset
            externalAssetActive = img3dsLoadAsset(asset.id, fileName);
        }

        if (!externalAssetActive) {
            // load default asset
            img3dsLoadAsset(asset.id);
        }
    }
}

//---------------------------------------------------------
// This is called when a ROM needs to be loaded and the
// emulator engine initialized.
//---------------------------------------------------------
bool impl3dsLoadROM(char *romFilePath)
{
    bool loaded = Memory.LoadROM(romFilePath);

    if(loaded) {
        log3dsWrite("ROM loaded: %s", romFilePath);

        char path[PATH_MAX];
        file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".srm", "saves");

        if (path[0] != '\0') {
            Memory.LoadSRAM (path);
        }

        // ensure controller is always set to player 1 when rom has loaded
        Settings.SwapJoypads = 0;
        cache3dsInit();
        gpu3dsInitializeMode7Vertexes();
    }
    
    return loaded;
}


//---------------------------------------------------------
// This is called when the user chooses to reset the
// console
//---------------------------------------------------------
void impl3dsResetConsole()
{
	S9xReset();
	gpu3dsInitializeMode7Vertexes();
}

// applies the provided cache operation (flush or invalidate) to the correct memory.
static void impl3dsApplyCacheOp(gfxScreen_t screen, bool isStereo, bool isWide, GSP_CacheCallback cacheOp)
{
    u16 w, h;
    u8* fb = gfxGetFramebuffer(screen, GFX_LEFT, &w, &h);
    
    if (!fb) return;

    u32 bpp = 0;
    switch (gfxGetScreenFormat(screen)) 
    {
        case GSP_RGBA8_OES:   bpp = 4; break;
        case GSP_BGR8_OES:    bpp = 3; break;
        default:              bpp = 2; break;
    }

    u32 dataSize = w * h * bpp;

    if (screen == GFX_TOP && isWide) {
        dataSize *= 2; 
    }

    cacheOp(fb, dataSize);

    if (screen == GFX_TOP && isStereo && !isWide) {
        u8* fbRight = gfxGetFramebuffer(screen, GFX_RIGHT, &w, &h);
        if (fbRight) {
            cacheOp(fbRight, dataSize);
        }
    }
}

void impl3dsFlushScreen(gfxScreen_t screen, bool isTopStereo, bool isWide) 
{
    impl3dsApplyCacheOp(screen, isTopStereo, isWide, GSPGPU_FlushDataCache);
}

void impl3dsInvalidateScreen(gfxScreen_t screen, bool isTopStereo, bool isWide) 
{
    impl3dsApplyCacheOp(screen, isTopStereo, isWide, GSPGPU_InvalidateDataCache);
}

static void impl3dsSceneRenderEye(bool firstFrame, bool paused, SVertexList *list,
	int sWidth, int sHeight, int sx0, int sy0, int cropPixels, bool isFullScreen, float xOffset) {

	gpu3dsSetDefaultRenderState(SPROGRAM_SCREEN, false);
	int screenWidth = settings3DS.GameScreenWidth;

	// draw the area behind the game screen (clear is done upfront in impl3dsSceneRender)
	if(!isFullScreen && !screenshot.dirty) {
		img3dsDrawBackground(UI_BG_GAME, paused, xOffset);
	}

	int sx1 = sx0 + sWidth;
	int sy1 = sy0 + sHeight;

	gpu3dsAddSimpleQuadVertexes(
		sx0, sy0, sx1, sy1,
		cropPixels, cropPixels ? cropPixels : 0,
		SNES_WIDTH - cropPixels, PPU.ScreenHeight - cropPixels, 0);

	if (sHeight == SNES_HEIGHT_EXTENDED) {
		// mask the bottom pixel row for games with extended height by drawing a 1px black bar
    	// without this, game border would be visible below the 239px game screen
		gpu3dsAddQuadRect(sx0, 239, sx1, 240, 0, 0, 0, 0xff);
	}

	GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0;
	GPU3DS.currentRenderState.textureBind =
		(gpu3dsIs3DEnabled() && GPU3DS.activeSide == GFX_RIGHT) ? SNES_MAIN_R : SNES_MAIN;
	gpu3dsDraw(list, NULL, list->count);

	if (!screenshot.dirty) {
		img3dsDrawGameOverlay(UI_OVERLAY, sWidth, sHeight);

		if (paused) {
			// dim overlay + pause notification (nearest layer)
			SGPUTexture *notifTexture = &GPU3DS.textures[UI_NOTIF_MSG];
			int wx = notifTexture->tex.width - 1;
			int wy = notifTexture->tex.height - 1;
			gpu3dsAddQuadRect(0, 0, screenWidth, SCREEN_HEIGHT, wx, wy, 0, 0xaa);
			notif3dsDraw(UI_NOTIF_MSG, settings3DS.GameScreen, -xOffset);
		} else {
			notif3dsDraw(UI_NOTIF_MSG, settings3DS.GameScreen);
			notif3dsDraw(UI_NOTIF_FPS, settings3DS.GameScreen);
		}
	}
}

void impl3dsSceneRender(bool firstFrame, bool paused) {
	SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];

    int screenWidth = settings3DS.GameScreenWidth;
	int sWidth, sHeight, sx0, sy0, cropPixels;

	if (!screenshot.dirty)
	{
		sWidth = settings3DS.StretchWidth;
		sHeight = settings3DS.StretchHeight == -1 ? PPU.ScreenHeight : settings3DS.StretchHeight;
		cropPixels = settings3DS.CropPixels;

		// Make sure "8:7 Fit" won't increase sWidth when current PPU.ScreenHeight = SNES_HEIGHT_EXTENDED
        if (settings3DS.ScreenStretch == Setting::ScreenStretch::Fit_8_7 && PPU.ScreenHeight >= SNES_HEIGHT_EXTENDED)
		{
			sWidth = SNES_WIDTH;
			sHeight = SNES_HEIGHT_EXTENDED;
		}

		sx0 = (screenWidth - sWidth) / 2;
	 	sy0 = (SCREEN_HEIGHT - sHeight) / 2;
	}
	else
	{
		sWidth = screenshot.width;
		sHeight = screenshot.height;
		cropPixels = screenshot.cropPixels;
		sx0 = screenshot.x;
	 	sy0 = screenshot.y;
	}

	bool isFullScreen = settings3DS.StretchWidth >= screenWidth && settings3DS.StretchHeight >= SCREEN_HEIGHT;
	bool isTopStereo = gpu3dsIs3DEnabled();
	float xOffset = isTopStereo ? gpu3dsGetIOD() : 0.0f;

	if (!isFullScreen && !screenshot.dirty) {
		gpu3dsClearScreen(settings3DS.GameScreen, isTopStereo);
	}

	GPU3DS.activeSide = GFX_LEFT;
	impl3dsSceneRenderEye(firstFrame, paused, list, sWidth, sHeight, sx0, sy0, cropPixels, isFullScreen, -xOffset);

	if (isTopStereo) {
		GPU3DS.activeSide = GFX_RIGHT;
		GPU3DS.appliedRenderState.target = TARGET_UNSET;

		impl3dsSceneRenderEye(firstFrame, paused, list, sWidth, sHeight, sx0, sy0, cropPixels, isFullScreen, xOffset);

		GPU3DS.activeSide = GFX_LEFT;
	}
}

//---------------------------------------------------------
// Executes one frame.
//---------------------------------------------------------

void impl3dsRunOneFrame(bool firstFrame, bool skipDrawingFrame)
{
	notif3dsTick();
	notif3dsSync();

	IPPU.RenderThisFrame = !skipDrawingFrame;

	if (firstFrame)
		Memory.ApplySpeedHackPatches();

	gpu3dsPrepareSnesScreenForNextFrame();

	t3dsStartTimer(TIMER_S9X_MAIN_LOOP);
	if (!Settings.SA1)
		S9xMainLoop();
	else
		S9xMainLoopWithSA1();
	t3dsStopTimer(TIMER_S9X_MAIN_LOOP);

	// C3D_FRAME_SYNCDRAW only when needed for screenshots (drains previous display transfer).
	gpu3dsFrameBegin(screenshot.dirty ? C3D_FRAME_SYNCDRAW : 0, !skipDrawingFrame);
		// Citra quirk
		// otherwise mode7 texture isnt visible at all
		if (!GPU3DS.citraReady) {
			GPU3DS.citraReady = true;
			gpu3dsAddRectangleVertexes(0, 0, 1, 1, 0);
			SVertexList *list = &GPU3DS.vertices[VBO_SCENE_RECT];
			gpu3dsDraw(list, NULL, list->count);
		}

		if (!firstFrame && !skipDrawingFrame) {
			t3dsStartTimer(TIMER_DRAW_SNES_SCREEN);
    		gpu3dsDrawSnesScreen();
			t3dsStopTimer(TIMER_DRAW_SNES_SCREEN);
		}

		if (firstFrame || !skipDrawingFrame) {
			t3dsStartTimer(TIMER_DRAW_SCENE);
    		impl3dsSceneRender(firstFrame);
			t3dsStopTimer(TIMER_DRAW_SCENE);
		}
	gpu3dsFrameEnd();

	if (screenshot.dirty && !skipDrawingFrame) {
		char path[PATH_MAX];

		bool success = impl3dsTakeScreenshot(path, sizeof(path), false);
		if (success) {
			notif3dsTrigger(Notif::Screenshot, Notif::Type::Success, settings3DS.GameScreen);
		} else {
			notif3dsTrigger(Notif::Misc, Notif::Type::Error, settings3DS.GameScreen, NOTIF_DEFAULT_DURATION, "Failed to save screenshot!");
		}
	}
}


//---------------------------------------------------------
// This is called when the bottom screen is touched
// during emulation, and the emulation engine is ready
// to display the pause menu.
//---------------------------------------------------------
void impl3dsTouchScreenPressed()
{
	// Save the SRAM if it has been modified before we going
	// into the menu.
	//
	if (settings3DS.ForceSRAMWriteOnPause || CPU.SRAMModified)
	{
		S9xAutoSaveSRAM();
	}
}


//---------------------------------------------------------
// This is called when the user chooses to save the state.
// This function should save the state into a file whose
// name contains the slot number. This will return
// true if the state is saved successfully.
//---------------------------------------------------------
bool impl3dsSaveStateSlot(int slotNumber)
{
    char path[PATH_MAX], ext[16];
    snprintf(ext, sizeof(ext), ".%d.frz", slotNumber);
    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ext, "savestates");

    if (impl3dsSaveState(path)) {
        log3dsWrite("saving to slot %d succeeded", slotNumber);
        
        if (settings3DS.CurrentSaveSlot != slotNumber && settings3DS.CurrentSaveSlot > 0) 
            impl3dsUpdateSlotState(settings3DS.CurrentSaveSlot);
            
        impl3dsUpdateSlotState(slotNumber, false, true);
        return true;
    }
    
    log3dsWrite("saving to slot %d failed", slotNumber);
    return false;
}

bool impl3dsSaveStateAuto()
{
    if (!settings3DS.isRomLoaded || !settings3DS.AutoSavestate) 
        return true;

    char path[PATH_MAX];
    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".auto.frz", "savestates");
    return impl3dsSaveState(path);
}

bool impl3dsSaveState(const char* filename)
{
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

	return Snapshot(filename);
}

//---------------------------------------------------------
// This is called when the user chooses to load the state.
// This function should save the state into a file whose
// name contains the slot number. This will return
// true if the state is loaded successfully.
//---------------------------------------------------------
bool impl3dsLoadStateSlot(int slotNumber)
{
    char path[PATH_MAX], ext[16];
    snprintf(ext, sizeof(ext), ".%d.frz", slotNumber);
    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ext, "savestates");

    bool success = impl3dsLoadState(path);
    
    if (success) {
        log3dsWrite("loading slot %d succeeded", slotNumber);
        // reset last slot
        if (settings3DS.CurrentSaveSlot != slotNumber && settings3DS.CurrentSaveSlot > 0)
            impl3dsUpdateSlotState(settings3DS.CurrentSaveSlot);
            
        impl3dsUpdateSlotState(slotNumber, false, true);
    } else {
        log3dsWrite("loading slot %d failed", slotNumber);
    }
    
    return success;
}

bool impl3dsLoadStateAuto()
{
    char path[PATH_MAX];
    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".auto.frz", "savestates");

    return impl3dsLoadState(path);
}

bool impl3dsLoadState(const char* filename)
{
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

	bool success = S9xLoadSnapshot(filename);
	if (success)
	{
		gpu3dsInitializeMode7Vertexes();
	}
	return success;
}

void impl3dsQuickSaveLoad(bool saveMode) {
    // quick load during AutoSaveSRAM may cause data abort exception
    // so we use snd3DS.generateSilence as flag here
    if (snd3DS.generateSilence) return;

    if (settings3DS.CurrentSaveSlot <= 0)
        settings3DS.CurrentSaveSlot = 1;
        
    snd3DS.generateSilence = true;
    
    bool success = saveMode ? impl3dsSaveStateSlot(settings3DS.CurrentSaveSlot) : impl3dsLoadStateSlot(settings3DS.CurrentSaveSlot);

	
	if (success) {
		Notif::Event event = saveMode ? Notif::SaveState : Notif::LoadState;
    	notif3dsTrigger(event, Notif::Type::Success, settings3DS.GameScreen);
	} else {
		char message[64];
		const char* action = saveMode ? "save into" : "load from";
		
		snprintf(message, sizeof(message), "Unable to %s Slot #%d!", action, settings3DS.CurrentSaveSlot);
		notif3dsTrigger(Notif::Misc, Notif::Type::Error, settings3DS.GameScreen, NOTIF_DEFAULT_DURATION, message);
	}
	
	snd3DS.generateSilence = false;
}

void impl3dsSaveCheats()
{
    if (!settings3DS.cheatsDirty || !settings3DS.isRomLoaded || Cheat.num_cheats == 0) return;

    char path[PATH_MAX];
    
    // try .chx first
    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".chx", "cheats", true);
    if (!S9xSaveCheatTextFile(path)) {
        // fallback to .cht
        file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".cht", "cheats", true);
        S9xSaveCheatFile(path);
    }

    settings3DS.cheatsDirty = false;
    log3dsWrite("SAVE CHEAT: %s", path);
}

int impl3dsGetSlotState(int slotNumber) {
	return static_cast<int>(slotStates[slotNumber - 1]);
}

void impl3dsUpdateSlotState(int slotNumber, bool newRomLoaded, bool saved) {
    if (saved) {
        slotStates[slotNumber - 1] = RADIO_ACTIVE_CHECKED;
        return;
    }
	
	// IsFileExists check necessary after new ROM has loaded
	if (newRomLoaded) {
        char path[PATH_MAX], ext[16];
        snprintf(ext, sizeof(ext), ".%d.frz", slotNumber);
        file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ext, "savestates");
        slotStates[slotNumber - 1] = IsFileExists(path) ? RADIO_ACTIVE : RADIO_INACTIVE;
	}
	
	if (slotNumber == settings3DS.CurrentSaveSlot || !newRomLoaded) {
		 switch (slotStates[slotNumber - 1])
        {
            case RADIO_INACTIVE:
                slotStates[slotNumber - 1] = RADIO_INACTIVE_CHECKED;
                break;
            case RADIO_ACTIVE:
                slotStates[slotNumber - 1] = RADIO_ACTIVE_CHECKED;
                break;
			case RADIO_INACTIVE_CHECKED:
                slotStates[slotNumber - 1] = RADIO_INACTIVE;
                break;
            case RADIO_ACTIVE_CHECKED:
                slotStates[slotNumber - 1] = RADIO_ACTIVE;
                break;
        }
	}
}

void impl3dsSelectSaveSlot(int direction) {
	// reset last slot
	if (settings3DS.CurrentSaveSlot > 0)
		impl3dsUpdateSlotState(settings3DS.CurrentSaveSlot);
	
	if (direction == 1) 
		settings3DS.CurrentSaveSlot = settings3DS.CurrentSaveSlot % SAVESLOTS_MAX + 1;
	else
		settings3DS.CurrentSaveSlot = settings3DS.CurrentSaveSlot <= 1 ? SAVESLOTS_MAX : settings3DS.CurrentSaveSlot - 1;

	impl3dsUpdateSlotState(settings3DS.CurrentSaveSlot);
	notif3dsTrigger(Notif::SlotChanged, Notif::Type::Info, settings3DS.GameScreen);
}

void impl3dsSwapJoypads() {
    Settings.SwapJoypads = Settings.SwapJoypads ? false : true;
    notif3dsTrigger(Notif::ControllerSwapped, Notif::Type::Info, settings3DS.GameScreen);
}

void impl3dsPrepareScreenshot(float scale, bool centered) {
	if (screenshot.dirty) return;
	
	screenshot.dirty = true;
	screenshot.width = SNES_WIDTH * scale;
	screenshot.height = PPU.ScreenHeight * scale;
	screenshot.cropPixels = 0;

	if (centered) {
        screenshot.x = (settings3DS.GameScreenWidth - screenshot.width) / 2;
		screenshot.y = (SCREEN_HEIGHT - screenshot.height) / 2;
	} else {
        screenshot.x = settings3DS.GameScreenWidth - screenshot.width;
		screenshot.y = SCREEN_HEIGHT - screenshot.height;
	}
	
	// disable linear filtering for pixel perfect screenshot
    screenshot.prevFilter = settings3DS.ScreenFilter;
    settings3DS.ScreenFilter = scale == 1.0f ? GPU_NEAREST : GPU_LINEAR;

	// force re-binding texture because texture filter has changed
	if (screenshot.prevFilter != settings3DS.ScreenFilter) {
		GPU3DS.currentRenderState.textureBind = TEX_UNSET; 
	}
}

bool impl3dsTakeScreenshot(char *path, size_t bufferSize, bool menuOpen) {
	if (snd3DS.generateSilence) return false;

	snd3DS.generateSilence = true;

	// clear paused game screen first + draw it at screenshot dimenions
	if (menuOpen) {
		gpu3dsFrameBegin();
		impl3dsPrepareScreenshot();
		impl3dsSceneRender(true, false);
		gpu3dsFrameEnd();
	}

	time_t rawtime = time(NULL);
	struct tm* t = localtime(&rawtime);
	char suffix[64];
	strftime(suffix, sizeof(suffix), ".%Y%m%d_%H%M%S.png", t);
    file3dsGetRelatedPath(Memory.ROMFilename, path, bufferSize, suffix, "screenshots");

    // Wait for the display transfer (PPF) event that C3D_FrameEnd queued.
    // Callers must ensure a frame was actually rendered before this point —
    // if no display transfer is pending, gspWaitForEvent will block forever.
    gspWaitForEvent(GSPGPU_EVENT_PPF, GPU3DS.isReal3DS);

    // Undo the buffer swap that C3D_FrameEnd performed internally
    // so gfxGetFramebuffer returns the buffer the GPU just wrote to.
    gfxScreenSwapBuffers(settings3DS.GameScreen, false);
    impl3dsInvalidateScreen(settings3DS.GameScreen);

    bool success = img3dsSaveScreenRegion(path, screenshot.width, screenshot.height, screenshot.x, screenshot.y, settings3DS.GameScreen);
	log3dsWrite("screenshot saved %s: %s", path, success ? "v" : "x");

	screenshot.dirty = false;
	snd3DS.generateSilence = false;
	settings3DS.ScreenFilter = screenshot.prevFilter;

	// restore the paused game screen at actual dimensions
	if (menuOpen) {
		gpu3dsFrameBegin();
		notif3dsTrigger(Notif::Event::Paused, Notif::Type::Default, settings3DS.GameScreen);
		notif3dsSync();
		impl3dsSceneRender(true, true);
		notif3dsHide();
		gpu3dsFrameEnd();
	}

	return success;
}


//=============================================================================
// Snes9x related functions
//=============================================================================
void _splitpath (const char *path, char *drive, char *dir, char *fname, char *ext)
{
	*drive = 0;

	const char	*slash = strrchr(path, SLASH_CHAR),
				*dot   = strrchr(path, '.');

	if (dot && slash && dot < slash)
		dot = NULL;

	if (!slash)
	{
		*dir = 0;

		strcpy(fname, path);

		if (dot)
		{
			fname[dot - path] = 0;
			strcpy(ext, dot + 1);
		}
		else
			*ext = 0;
	}
	else
	{
		strcpy(dir, path);
		dir[slash - path] = 0;

		strcpy(fname, slash + 1);

		if (dot)
		{
			fname[dot - slash - 1] = 0;
			strcpy(ext, dot + 1);
		}
		else
			*ext = 0;
	}
}

void _makepath (char *path, const char *, const char *dir, const char *fname, const char *ext)
{
	if (dir && *dir)
	{
		strcpy(path, dir);
		strcat(path, SLASH_STR);
	}
	else
		*path = 0;

	strcat(path, fname);

	if (ext && *ext)
	{
		strcat(path, ".");
		strcat(path, ext);
	}
}

void S9xMessage (int type, int number, const char *message)
{
	//printf("%s\n", message);
}

bool8 S9xInitUpdate (void)
{
	return (TRUE);
}

bool8 S9xDeinitUpdate (int width, int height, bool8 sixteen_bit)
{
	return (TRUE);
}



void S9xAutoSaveSRAM (void)
{
    // Ensure that the timer is reset
    //
    //CPU.AccumulatedAutoSaveTimer = 0;
    CPU.SRAMModified = false;

    // Bug fix: Instead of stopping CSND, we generate silence
    // like we did prior to v0.61
    //
        char path[PATH_MAX];
        file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".srm", "saves");

        if (path[0] != '\0') {
            Memory.SaveSRAM (path);
	}

    // Bug fix: Instead of starting CSND, we continue to mix
    // like we did prior to v0.61
    //
    snd3DS.generateSilence = false;
}

void S9xGenerateSound ()
{
}


void S9xExit (void)
{

}

void S9xSetPalette (void)
{
	return;
}


bool8 S9xOpenSoundDevice(int mode, bool8 stereo, int buffer_size)
{
	return (TRUE);
}

const char * S9xGetFilenameInc (const char *ex)
{
	static char	s[PATH_MAX + 1];
	char		drive[_MAX_DRIVE + 1], dir[_MAX_DIR + 1], fname[_MAX_FNAME + 1], ext[_MAX_EXT + 1];

	unsigned int	i = 0;
	struct stat		buf;

	_splitpath(Memory.ROMFilename, drive, dir, fname, ext);

	do {
		const char *suffix = ex ? ex : "";
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wformat-truncation"
		int written = snprintf(s, sizeof(s), "%s/%s.%03u%s", dir, fname, i++, suffix);
		#pragma GCC diagnostic pop
		if (written < 0 || (size_t) written >= sizeof(s)) {
			s[0] = '\0';
			break;
		}
	}
	while (stat(s, &buf) == 0 && i < 1000);

	return (s);
}


bool8 S9xReadMousePosition (int which1_0_to_1, int &x, int &y, uint32 &buttons)
{
	return FALSE;
}

bool8 S9xReadSuperScopePosition (int &x, int &y, uint32 &buttons)
{
	return FALSE;
}

bool JustifierOffscreen()
{
	return 0;
}

void JustifierButtons(uint32& justifiers)
{

}

char * osd_GetPackDir(void)
{

    return NULL;
}

const char *S9xBasename (const char *f)
{
    const char *p;
    if ((p = strrchr (f, '/')) != NULL || (p = strrchr (f, '\\')) != NULL)
	return (p + 1);

    if ((p = strrchr (f, SLASH_CHAR)))
        return (p + 1);

    return (f);
}


bool8 S9xOpenSnapshotFile (const char *filename, bool8 read_only, STREAM *file)
{
    char s[PATH_MAX + 1];
    snprintf(s, PATH_MAX + 1, "%s", filename);

    if ((*file = file3dsOpen(s, read_only ? "rb" : "wb")))
    {
        return (TRUE);
    }

    return (FALSE);
}

void S9xCloseSnapshotFile (STREAM file)
{
	file3dsClose(file);
}

void S9xParseArg (char **argv, int &index, int argc)
{

}

void S9xExtraUsage ()
{

}

void S9xGraphicsMode ()
{

}
void S9xTextMode ()
{

}
void S9xSyncSpeed (void)
{
}

uint32 prevConsoleJoyPad = 0;
u32 prevConsoleButtonPressed[10];
u32 buttons3dsPressed[10];

uint32 S9xReadJoypad (int which1_0_to_4)
{
    if (which1_0_to_4 != 0) {
        return 0;
    }

	u32 keysHeld3ds = input3dsGetCurrentKeysHeld();
    u32 consoleJoyPad = 0;

    if (keysHeld3ds & (settings3DS.BindCirclePad == 1 ? KEY_UP : KEY_DUP)) consoleJoyPad |= SNES_UP_MASK;
    if (keysHeld3ds & (settings3DS.BindCirclePad == 1 ? KEY_DOWN : KEY_DDOWN)) consoleJoyPad |= SNES_DOWN_MASK;
    if (keysHeld3ds & (settings3DS.BindCirclePad == 1 ? KEY_LEFT : KEY_DLEFT)) consoleJoyPad |= SNES_LEFT_MASK;
    if (keysHeld3ds & (settings3DS.BindCirclePad == 1 ? KEY_RIGHT : KEY_DRIGHT)) consoleJoyPad |= SNES_RIGHT_MASK;

	#define SET_CONSOLE_JOYPAD(i, mask, buttonMapping) 				\
		buttons3dsPressed[i] = (keysHeld3ds & mask);				\
		if (keysHeld3ds & mask) 									\
			consoleJoyPad |= 										\
				buttonMapping[i][0] |								\
				buttonMapping[i][1] |								\
				buttonMapping[i][2] |								\
				buttonMapping[i][3];								\

	if (settings3DS.UseGlobalButtonMappings)
	{
		SET_CONSOLE_JOYPAD(BTN3DS_L, KEY_L, settings3DS.GlobalButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_R, KEY_R, settings3DS.GlobalButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_A, KEY_A, settings3DS.GlobalButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_B, KEY_B, settings3DS.GlobalButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_X, KEY_X, settings3DS.GlobalButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_Y, KEY_Y, settings3DS.GlobalButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_SELECT, KEY_SELECT, settings3DS.GlobalButtonMapping);
		SET_CONSOLE_JOYPAD(BTN3DS_START, KEY_START, settings3DS.GlobalButtonMapping);
		SET_CONSOLE_JOYPAD(BTN3DS_ZL, KEY_ZL, settings3DS.GlobalButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_ZR, KEY_ZR, settings3DS.GlobalButtonMapping)
	}
	else
	{
		SET_CONSOLE_JOYPAD(BTN3DS_L, KEY_L, settings3DS.ButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_R, KEY_R, settings3DS.ButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_A, KEY_A, settings3DS.ButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_B, KEY_B, settings3DS.ButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_X, KEY_X, settings3DS.ButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_Y, KEY_Y, settings3DS.ButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_SELECT, KEY_SELECT, settings3DS.ButtonMapping);
		SET_CONSOLE_JOYPAD(BTN3DS_START, KEY_START, settings3DS.ButtonMapping);
		SET_CONSOLE_JOYPAD(BTN3DS_ZL, KEY_ZL, settings3DS.ButtonMapping)
		SET_CONSOLE_JOYPAD(BTN3DS_ZR, KEY_ZR, settings3DS.ButtonMapping)
	}


    // Handle turbo / rapid fire buttons.
    //
    std::array<int, 8> turbo = settings3DS.Turbo;
    if (settings3DS.UseGlobalTurbo)
        turbo = settings3DS.GlobalTurbo;

    #define HANDLE_TURBO(i, buttonMapping) 										\
		if (settings3DS.Turbo[i] && buttons3dsPressed[i]) { 		\
			if (!prevConsoleButtonPressed[i]) 						\
			{ 														\
				prevConsoleButtonPressed[i] = 11 - turbo[i]; 		\
			} 														\
			else 													\
			{ 														\
				prevConsoleButtonPressed[i]--; 						\
				consoleJoyPad &= ~(									\
				buttonMapping[i][0] |								\
				buttonMapping[i][1] |								\
				buttonMapping[i][2] |								\
				buttonMapping[i][3]									\
				); \
			} \
		} \


	if (settings3DS.UseGlobalButtonMappings)
	{
		HANDLE_TURBO(BTN3DS_A, settings3DS.GlobalButtonMapping);
		HANDLE_TURBO(BTN3DS_B, settings3DS.GlobalButtonMapping);
		HANDLE_TURBO(BTN3DS_X, settings3DS.GlobalButtonMapping);
		HANDLE_TURBO(BTN3DS_Y, settings3DS.GlobalButtonMapping);
		HANDLE_TURBO(BTN3DS_L, settings3DS.GlobalButtonMapping);
		HANDLE_TURBO(BTN3DS_R, settings3DS.GlobalButtonMapping);
		HANDLE_TURBO(BTN3DS_ZL, settings3DS.GlobalButtonMapping);
		HANDLE_TURBO(BTN3DS_ZR, settings3DS.GlobalButtonMapping);
	}
	else
	{
		HANDLE_TURBO(BTN3DS_A, settings3DS.ButtonMapping);
		HANDLE_TURBO(BTN3DS_B, settings3DS.ButtonMapping);
		HANDLE_TURBO(BTN3DS_X, settings3DS.ButtonMapping);
		HANDLE_TURBO(BTN3DS_Y, settings3DS.ButtonMapping);
		HANDLE_TURBO(BTN3DS_L, settings3DS.ButtonMapping);
		HANDLE_TURBO(BTN3DS_R, settings3DS.ButtonMapping);
		HANDLE_TURBO(BTN3DS_ZL, settings3DS.ButtonMapping);
		HANDLE_TURBO(BTN3DS_ZR, settings3DS.ButtonMapping);
	}

    prevConsoleJoyPad = consoleJoyPad;

    return consoleJoyPad;
}
