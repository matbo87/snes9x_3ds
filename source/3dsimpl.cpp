//=============================================================================
// Contains all the hooks and interfaces between the emulator interface
// and the main emulator core.
//=============================================================================

#include <array>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <3ds.h>

#include <dirent.h>
#include "snes9x.h"
#include "memmap.h"
#include "apu.h"
#include "gfx.h"
#include "snapshot.h"
#include "cheats.h"
#include "soundux.h"

#include "3dsexit.h"
#include "3dsfiles.h"
#include "3dsgpu.h"
#include "3dssound.h"
#include "3dsmenu.h"
#include "3dsui.h"
#include "3dsinput.h"
#include "3dsimpl.h"
#include "3dsimpl_tilecache.h"
#include "3dsimpl_gpu.h"
#include "3dslog.h"

// Compiled shaders
#include "shader_tiles_shbin.h"
#include "shader_mode7_shbin.h"
#include "shader_screen_shbin.h"

radio_state slotStates[SAVESLOTS_MAX];

static S9xScreenshot screenshot = {0};
static int lastBackgroundHeight = -1;

//---------------------------------------------------------
// Initializes the emulator core.
//
// You must call snd3dsSetSampleRate here to set 
// the CSND's sampling rate.
//---------------------------------------------------------
bool impl3dsInitializeCore()
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

	u32 paramFilterDefault = GPU_TEXTURE_MAG_FILTER(GPU_NEAREST) | GPU_TEXTURE_MIN_FILTER(GPU_NEAREST) | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_BORDER) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_BORDER);
	
	SGPUTextureConfig textureConfig[] = {
		{ SNES_TILE_CACHE, 1024, 1024, GPU_RGBA5551, paramFilterDefault, false, false, false },
		{ SNES_MODE7_TILE_CACHE, 128, 128, GPU_RGBA5551, paramFilterDefault, false, false, false },
		{ SNES_MODE7_TILE_0, 16, 16, GPU_RGBA5551, GPU_TEXTURE_MAG_FILTER(GPU_NEAREST) | GPU_TEXTURE_MIN_FILTER(GPU_NEAREST) | GPU_TEXTURE_WRAP_S(GPU_REPEAT) | GPU_TEXTURE_WRAP_T(GPU_REPEAT), true, true, false },
		{ SNES_MODE7_FULL, 1024, 1024, GPU_RGBA5551, paramFilterDefault, true, true, false }, // 2.000 MB
		{ SNES_MAIN, 256, 256, GPU_RGBA8, paramFilterDefault, true, true, true }, // 0.250 MB
		{ SNES_SUB, 256, 256, GPU_RGBA8, paramFilterDefault, true, true, true },
		{ SNES_DEPTH, 256, 256, GPU_RGBA8, paramFilterDefault, true, true, false },
	};

	bool textureAllocated;
	size_t total = sizeof(textureConfig) / sizeof(textureConfig[0]);

	for (int i = 0; i < total; i++) 
	{
		textureAllocated = gpu3dsInitTexture(&textureConfig[i]);

		if (settings3DS.LogFileEnabled) {
			SGPU_TEXTURE_ID id = textureConfig[i].id;

    		SGPUTexture *texture = &GPU3DS.textures[id];

			log3dsWrite("[%s] dim: %dx%d, size:%.2fkb, format: %s, onVram: %s, render target: %s",
				SGPUTextureIDToString(id),
				texture->tex.width, texture->tex.height,
				(float)texture->tex.size / 1024,
				SGPUTexColorToString(texture->tex.fmt),
				textureConfig[i].onVram ? "v" : "x",
				textureConfig[i].hasTarget ? "v" : "x"
			);
		}

		if (!textureAllocated)
			break;
	}

	// if any texture has been failed to initialize
    if (!textureAllocated)
    {
        log3dsWrite("Unable to allocate all textures");

        return false;
    }	

	// Bind depth texture to sub and main render target. This works so far, but doesn't feel right. 
	// Citra will show warnings because of using RGBA8 texture in GPU_RB_DEPTH24_STENCIL8 buffer format
	//
	// check if other tex targets should also own depth buffers for performance reasons
	GPU3DS.textures[SNES_MAIN].target->frameBuf.depthBuf = C3D_Tex2DGetImagePtr(&GPU3DS.textures[SNES_DEPTH].tex, 0, NULL);
	GPU3DS.textures[SNES_SUB].target->frameBuf.depthBuf = C3D_Tex2DGetImagePtr(&GPU3DS.textures[SNES_DEPTH].tex, 0, NULL);

	log3dsWrite("allocate vbos:");

	//  rectangle vertices (windowLR, backdrop, fixed color color math, brightness)
	size_t vbo_scene_rect_size = gpu3dsGetNextPowerOf2(sizeof(SRectVertex) * MAX_VERTICES_RECT * 2);

	//  bg0-bg3, obj, sub screen color math
	size_t vbo_scene_tile_size = gpu3dsGetNextPowerOf2(sizeof(STileVertex) * MAX_VERTICES * 2);

	// bg0-bg1
	size_t vbo_scene_mode7_line_size = gpu3dsGetNextPowerOf2(sizeof(SMode7LineVertex) * MAX_VERTICES_MODE7_LINE * 2);

	// mode 7 full texture + tile0 = MAX_VERTICES_MODE7_TILE * 2 (double buffering) 
	size_t vbo_mode7_tile_size = gpu3dsGetNextPowerOf2(sizeof(SMode7TileVertex) * MAX_VERTICES_MODE7_TILE * 2);

	// 4 quads (0-5 = background, 6 - 11 = ingame, 12-17 = bezel, 18-23 = cover)
	size_t vbo_screen_size = gpu3dsGetNextPowerOf2(sizeof(SQuadVertex) * MAX_VERTICES_QUAD * 2);
	
	SVertexListInfo listInfos[] = {
		{ VBO_SCENE_RECT, vbo_scene_rect_size, sizeof(SRectVertex), 2, { {GPU_SHORT, 2}, {GPU_UNSIGNED_BYTE, 4} } },
		{ VBO_SCENE_TILE, vbo_scene_tile_size, sizeof(STileVertex), 2, { {GPU_SHORT, 3}, {GPU_SHORT, 2} } },
		{ VBO_SCENE_MODE7_LINE, vbo_scene_mode7_line_size, sizeof(SMode7LineVertex), 2, { {GPU_SHORT, 2}, {GPU_FLOAT, 2} } },
		{ VBO_MODE7_TILE, vbo_mode7_tile_size, sizeof(SMode7TileVertex), 1, { {GPU_SHORT, 4} } },
		{ VBO_SCREEN, vbo_screen_size, sizeof(SQuadVertex), 3, { {GPU_SHORT, 4}, {GPU_SHORT, 2}, {GPU_UNSIGNED_BYTE, 4} } },
	};

	bool listAllocated;
	
	for (int i = 0; i < VBO_COUNT; i++) 
	{
		listAllocated = gpu3dsAllocVertexList(&listInfos[i]);

		if (settings3DS.LogFileEnabled) {
			SGPU_VBO_ID id = listInfos[i].id;

			int stride = 0;
			for (size_t j = 0; j < listInfos[i].totalAttributes; j++) {
				int bytes = listInfos[i].attrFormat[j].format == GPU_FLOAT || listInfos[i].attrFormat[j].format == GPU_BYTE 
				? listInfos[i].attrFormat[j].format + 1 
				: listInfos[i].attrFormat[j].format;

				stride += bytes * listInfos[i].attrFormat[j].count;
			}
			
			log3dsWrite("[%s] size: %.2fkb, vertex size: %dbytes, stride: %d, total attributes: %d",
				SGPUVboIDToString(id),
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

	log3dsWrite("allocate screenshot data");

	if (!impl3dsAllocScreenshot()) {
		return false;
	}
	
	log3dsWrite("-- initialize SNES core --");

    memset(&Settings, 0, sizeof(Settings));
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
    Settings.AutoSaveDelay = 60;         // Bug fix to save SRAM within 60 frames (1 second instead of 30 seconds)
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

	// make an initial dummy draw call on citra
	// otherwise mode7 texture is not visible 
	if (!GPU3DS.isReal3DS) {
		gpu3dsAddRectangleVertexes(0, 0, 8, 8, 0);
		gpu3dsSetDefaultRenderState(SPROGRAM_TILES, true);
		SVertexList *list = &GPU3DS.vertices[VBO_SCENE_RECT];

		C3D_FrameBegin(0);
		gpu3dsDraw(list, NULL, list->count);
		C3D_FrameEnd(0);
	}

    return true;
}

//---------------------------------------------------------
// Finalizes and frees up any resources.
//---------------------------------------------------------
void impl3dsFinalize()
{
	log3dsWrite("dealloc vbos");
    for (int i = 0; i < VBO_COUNT; i++)
        gpu3dsDeallocVertexList(&GPU3DS.vertices[i]);


	log3dsWrite("dealloc ibo");
	gpu3dsDeallocLayers();

	log3dsWrite("destroy textures");
    for (int i = 0; i < TEX_COUNT; i++)
        gpu3dsDestroyTexture(&GPU3DS.textures[i]);

	if (screenshot.data != NULL) {
		log3dsWrite("dealloc screenshot data");
		linearFree(screenshot.data);
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


//---------------------------------------------------------
// Border image for game screen
//---------------------------------------------------------
void impl3dsSetBorderImage() {
	if (settings3DS.GameBorder == 0) {
        log3dsWrite("reset border image");

		return;
	}
	
	if (settings3DS.GameBorder == 1) {
		ui3dsRestoreDefault(UI_BORDER);

		return;
	}

	std::string fileName = file3dsGetAssociatedFilename(Memory.ROMFilename, ".png", "borders", true);

	if (fileName.empty()) {
		return;
	}
	
	ui3dsUpdateSubtexture(UI_BORDER, fileName.c_str());
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

		std::string path = file3dsGetAssociatedFilename(romFilePath, ".srm", "saves");

		if (!path.empty()) {
    		Memory.LoadSRAM (path.c_str());
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


//---------------------------------------------------------
// This is called when preparing to start emulating
// a new frame. Use this to do any preparation of data
// and the hardware before the frame is emulated.
//---------------------------------------------------------
void impl3dsPrepareForNewFrame()
{
	gpu3dsPrepareLayersForNextFrame();
    gpu3dsSwapVertexListForNextFrame(&GPU3DS.vertices[VBO_SCENE_RECT]);
    gpu3dsSwapVertexListForNextFrame(&GPU3DS.vertices[VBO_SCENE_TILE]);
    gpu3dsSwapVertexListForNextFrame(&GPU3DS.vertices[VBO_SCENE_MODE7_LINE]);
    gpu3dsSwapVertexListForNextFrame(&GPU3DS.vertices[VBO_SCREEN]);
}

void sceneRender(bool firstFrame, float backgroundOpacity) {
	SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];

	int screenWidth = screenSettings.GameScreenWidth;
	bool fading = backgroundOpacity >= 0.0f;
	bool isFullScreen = settings3DS.StretchWidth == screenWidth && settings3DS.StretchHeight == SCREEN_HEIGHT;

	// draw the area behind the game screen
	if(!isFullScreen) {
		// don't draw the last line of pixels when game height = SNES_HEIGHT_EXTENDED
		int backgroundHeight = ((PPU.ScreenHeight == SNES_HEIGHT_EXTENDED && settings3DS.ScreenStretch <= 1) || settings3DS.ScreenStretch == 6)
			? SNES_HEIGHT_EXTENDED
			: SCREEN_HEIGHT;

		if (firstFrame || backgroundHeight != lastBackgroundHeight) {
			ui3dsDrawBackground(list, &GPU3DS.textures[UI_BORDER], backgroundHeight == SNES_HEIGHT_EXTENDED);
			lastBackgroundHeight = backgroundHeight;
		}
	}

	int sWidth, sHeight, sx0, sy0, cropPixels;

	if (!screenshot.dirty) 
	{
		sWidth = settings3DS.StretchWidth;
		sHeight = settings3DS.StretchHeight == -1 ? PPU.ScreenHeight : settings3DS.StretchHeight;
		cropPixels = settings3DS.CropPixels;

		// Make sure "8:7 Fit" won't increase sWidth when current PPU.ScreenHeight = SNES_HEIGHT_EXTENDED
		if (sWidth == 01010000)
		{
			sWidth = PPU.ScreenHeight < SNES_HEIGHT_EXTENDED ? 273 : SNES_WIDTH; // SNES_HEIGHT_EXTENDED * SNES_WIDTH / SNES_HEIGHT = ~273
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

	int sx1 = sx0 + sWidth;
	int sy1 = sy0 + sHeight;

	gpu3dsAddQuadVertexes(
		sx0, sy0, sx1, sy1,
		cropPixels, cropPixels ? cropPixels : 0, 
		256 - cropPixels, PPU.ScreenHeight - cropPixels, 
		0);

	SGPURenderState renderState = GPU3DS.currentRenderState;
	renderState.textureBind = SNES_MAIN;
	renderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0;
	gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TEXTURE_BIND | FLAG_TEXTURE_ENV, &renderState);
	gpu3dsDraw(list, NULL, list->count);

	if (fading) {
		float opacityOffset = -0.1f;
		float rectAlpha = opacityOffset + (1.0f - backgroundOpacity);

		if (rectAlpha > 0.0f) {
			u32 alpha = (u32)(rectAlpha * 255.0f);
			u32 blendColor1 = 0x11111100 | alpha; // 0xRRGGBBAA | 0xAA

			gpu3dsAddQuadVertexes(0, 0, screenWidth, SCREEN_HEIGHT, 0, 0, SCREEN_TOP_WIDTH, SCREEN_HEIGHT, 0, blendColor1);

			int height = 50;
			int y0 = (SCREEN_HEIGHT - height) / 2;
			u32 blendColor2 = 0x00 | alpha;
			
			gpu3dsAddQuadVertexes(0, y0, screenWidth, y0 + height, 0, 0, SCREEN_TOP_WIDTH, SCREEN_HEIGHT, 0, blendColor2);

			renderState.textureEnv = TEX_ENV_REPLACE_COLOR;
			renderState.alphaBlending = ALPHA_BLENDING_ENABLED;
			gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TEXTURE_ENV | FLAG_ALPHA_BLENDING, &renderState);

			gpu3dsDraw(list, NULL, list->count);
		}
	}
}

void impl3dsDrawPauseScreen(float fadeDuration) {
	TickCounter fadeTimer;
	float endOpacity = 0.2f;
	float elapsed = 0.0f;

	int rectHeight = 50;
	int y0 = (SCREEN_HEIGHT - rectHeight) / 2;

	bool firstFrame = true;
	while (aptMainLoop())
	{
		if (firstFrame) {
			osTickCounterStart(&fadeTimer);
		}
		
		osTickCounterUpdate(&fadeTimer);
		elapsed += (float)osTickCounterRead(&fadeTimer);

		float t = elapsed / fadeDuration;
		float opacity = 1.0f * (1.0f - t) + endOpacity * t;

		if (elapsed >= fadeDuration || fadeDuration <= 0.0f) {
			opacity = endOpacity;
		}

    	gpu3dsSwapVertexListForNextFrame(&GPU3DS.vertices[VBO_SCREEN]);
		gpu3dsSetDefaultRenderState(SPROGRAM_SCREEN, true);
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		sceneRender(true, opacity);
		C3D_FrameEnd(0);

		firstFrame = false;

		if (opacity == endOpacity) break;
	}

	// ensure the GPU's display transfer from C3D_FrameEnd() has completed before swapping
	gfxScreenSwapBuffers(screenSettings.GameScreen, false);
	gspWaitForVBlank();
	
    int textWidth = 150;
    int textCx = screenSettings.GameScreenWidth / 2 - textWidth / 2;
    int textCy = SCREEN_HEIGHT / 2 - 8;
    int shadowColor = 0x111111;

	ui3dsDrawStringWithNoWrapping(screenSettings.GameScreen, textCx + 1, textCy + 1, textCx + textWidth + 1, textCy + 7 + 1, shadowColor, HALIGN_LEFT, 
		"\x13\x14\x15\x16\x16 \x0e\x0f\x10\x11\x12 \x17\x18 \x14\x15\x16\x19\x1a\x15");
	ui3dsDrawStringWithNoWrapping(screenSettings.GameScreen, textCx, textCy, textCx + textWidth, textCy + 7, 0xffffff, HALIGN_LEFT, 
		"\x13\x14\x15\x16\x16 \x0e\x0f\x10\x11\x12 \x17\x18 \x14\x15\x16\x19\x1a\x15");
	gfxScreenSwapBuffers(screenSettings.GameScreen, false);
	gspWaitForVBlank();
}

//---------------------------------------------------------
// Executes one frame.
//---------------------------------------------------------

void impl3dsRunOneFrame(bool firstFrame, bool skipDrawingFrame)
{
	IPPU.RenderThisFrame = !skipDrawingFrame;
	impl3dsPrepareForNewFrame();
	gpu3dsSetDefaultRenderState(!skipDrawingFrame ? SPROGRAM_TILES : SPROGRAM_SCREEN, true);

	C3D_FrameBegin(0);
	Memory.ApplySpeedHackPatches(); // first frame only?

	t3dsStartTimer(TIMER_S9X_MAIN_LOOP);

	if (!Settings.SA1)
		S9xMainLoop();
	else
		S9xMainLoopWithSA1();
		
	t3dsStopTimer(TIMER_S9X_MAIN_LOOP);
	
	if (!skipDrawingFrame)
		gpu3dsSetDefaultRenderState(SPROGRAM_SCREEN, false);

	sceneRender(firstFrame);

	C3D_FrameEnd(0);

	// handle screenshot triggered via hotkey
	if (screenshot.dirty) {
		char path[_MAX_PATH - 1];
		char message[_MAX_PATH - 1];
		bool success = impl3dsTakeScreenshot(path, false);
	
		if (success) {
			snprintf(message, _MAX_PATH - 1, "Screenshot saved to %s", path);
		} else {
			snprintf(message, _MAX_PATH - 1, "%s", "Failed to save screenshot!");
		}

		menu3dsSetSecondScreenContent(message, (success ? Themes[settings3DS.Theme].dialogColorSuccess : Themes[settings3DS.Theme].dialogColorWarn));
		
		// reset lastBackgroundHeight to redraw background int the next frame
		if (!screenshot.centered) {
			lastBackgroundHeight = -1; 
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
	std::string ext = "." + std::to_string(slotNumber) + ".frz";
	std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ext.c_str(), "savestates");
	bool success = impl3dsSaveState(path.c_str());
	
	if (success) {
		log3dsWrite("saving to slot %d succeeded", slotNumber);
		// reset last slot
		if (settings3DS.CurrentSaveSlot != slotNumber && settings3DS.CurrentSaveSlot > 0)
			impl3dsUpdateSlotState(settings3DS.CurrentSaveSlot);

		impl3dsUpdateSlotState(slotNumber, false, true);
	} else {
		log3dsWrite("saving to slot %d failed", slotNumber);
	}
	
	return success;
}

bool impl3dsSaveStateAuto()
{
	std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ".auto.frz", "savestates");

	return impl3dsSaveState(path.c_str());
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
	std::string ext = "." + std::to_string(slotNumber) + ".frz";
	std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ext.c_str(), "savestates");
	bool success = impl3dsLoadState(path.c_str());

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
	std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ".auto.frz", "savestates");

	return impl3dsLoadState(path.c_str());
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


void impl3dsSaveLoadMessage(bool saveMode, saveLoad_state saveLoadState) 
{
    char message[_MAX_PATH];
	int dialogBackgroundColor;

	switch (saveLoadState)
	{
		case SAVELOAD_IN_PROGRESS:
			dialogBackgroundColor = Themes[settings3DS.Theme].dialogColorInfo;
			snprintf(message, _MAX_PATH, "%s slot #%d...", saveMode ? "Saving into" : "Loading from", settings3DS.CurrentSaveSlot);
			break;
		case SAVELOAD_SUCCEEDED:
			dialogBackgroundColor = Themes[settings3DS.Theme].dialogColorSuccess;
			snprintf(message, _MAX_PATH, "Slot %d %s.", settings3DS.CurrentSaveSlot, saveMode ? "save completed" : "loaded");
			break;
		case SAVELOAD_FAILED:
			dialogBackgroundColor = Themes[settings3DS.Theme].dialogColorWarn;
			snprintf(message, _MAX_PATH, "Unable to %s #%d!", saveMode ? "save into" : "load from", settings3DS.CurrentSaveSlot);
			break;
	}
 
	menu3dsSetSecondScreenContent(message, dialogBackgroundColor);
}

void impl3dsQuickSaveLoad(bool saveMode) {
	// quick load during AutoSaveSRAM may cause data abort exception
	// so we use snd3DS.generateSilence as flag here
	if (snd3DS.generateSilence) return;

	if (settings3DS.CurrentSaveSlot <= 0)
		settings3DS.CurrentSaveSlot = 1;
		
	snd3DS.generateSilence = true;
	impl3dsSaveLoadMessage(saveMode, SAVELOAD_IN_PROGRESS);
	
	bool success = saveMode ? impl3dsSaveStateSlot(settings3DS.CurrentSaveSlot) : impl3dsLoadStateSlot(settings3DS.CurrentSaveSlot);
	
	impl3dsSaveLoadMessage(saveMode, success ? SAVELOAD_SUCCEEDED : SAVELOAD_FAILED);
	snd3DS.generateSilence = false;
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

		std::string ext = "." + std::to_string(slotNumber) + ".frz";
		std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ext.c_str(), "savestates");
   	 	slotStates[slotNumber - 1] = IsFileExists(path.c_str()) ? RADIO_ACTIVE : RADIO_INACTIVE;
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

    char message[_MAX_PATH];
	snprintf(message, _MAX_PATH - 1, "Current Save Slot: #%d", settings3DS.CurrentSaveSlot);
	menu3dsSetSecondScreenContent(message, Themes[settings3DS.Theme].dialogColorSuccess);
}

void impl3dsSwapJoypads() {
    Settings.SwapJoypads = Settings.SwapJoypads ? false : true;

    char message[_MAX_PATH];
	snprintf(message, _MAX_PATH - 1, "Controllers Swapped.\nPlayer #%d active.", Settings.SwapJoypads ? 2 : 1);
	menu3dsSetSecondScreenContent(message, Themes[settings3DS.Theme].dialogColorSuccess);
}

bool impl3dsAllocScreenshot() {
	int bpp = gpu3dsGetPixelSize(GPU_RGB8); // 3 bits per pixel
    u32 bufferSize = SNES_WIDTH * SNES_HEIGHT_EXTENDED * bpp;
	screenshot.data = (u8*)linearAlloc(bufferSize);

	return screenshot.data != NULL;
}

void impl3dsSetScreenshotSlot() {
	for (int slot = settings3DS.ScreenshotSlot; slot <= SCREENSHOT_MAX; slot++) {
		char ext[16];
		snprintf(ext, sizeof(ext), ".%02d.png", slot); // 01-99
		std::string filename = file3dsGetAssociatedFilename(Memory.ROMFilename, ext, "screenshots");
	
		if (!filename.empty() && !IsFileExists(filename.c_str())) {
			settings3DS.ScreenshotSlot = slot;
			break;
		}
	}
}

void impl3dsPrepareScreenshot(float scale, bool centered) {
	if (screenshot.dirty || ui3dsGetSecondScreenDialogState() != HIDDEN) return;
	
	screenshot.dirty = true;
	screenshot.width = SNES_WIDTH * scale;
	screenshot.height = PPU.ScreenHeight * scale;
	screenshot.cropPixels = 0;
	screenshot.centered = centered;

	if (centered) {
		screenshot.x = (screenSettings.GameScreenWidth - screenshot.width) / 2;
		screenshot.y = (SCREEN_HEIGHT - screenshot.height) / 2;
	} else {
		screenshot.x = screenSettings.GameScreenWidth - screenshot.width;
		screenshot.y = SCREEN_HEIGHT - screenshot.height;
	}
	
	// disable linear filtering for pixel perfect screenshot
	screenshot.prevFilter = GPU_TEXTURE_FILTER_PARAM(settings3DS.ScreenFilter);
	settings3DS.ScreenFilter = scale == 1.0f ? 0 : 1;

	// force re-binding texture because texture filter has changed
	if (screenshot.prevFilter != settings3DS.ScreenFilter) {
		GPU3DS.currentRenderState.textureBind = TEX_COUNT; 
	}
}

bool impl3dsTakeScreenshot(char *path, bool menuOpen) {
	if (snd3DS.generateSilence) return false;

	snd3DS.generateSilence = true;

	// clear pause screen first
	if (menuOpen) {
		impl3dsPrepareScreenshot();

		gpu3dsSwapVertexListForNextFrame(&GPU3DS.vertices[VBO_SCREEN]);
		gpu3dsSetDefaultRenderState(SPROGRAM_SCREEN, true);

		C3D_FrameBegin(0);
		sceneRender(false);
		C3D_FrameEnd(0);
	}

	if (!(settings3DS.ScreenshotSlot >= 1 && settings3DS.ScreenshotSlot <= SCREENSHOT_MAX)) {
		settings3DS.ScreenshotSlot = 1;
	}
	
	char ext[16];
	snprintf(ext, sizeof(ext), ".%02d.png", settings3DS.ScreenshotSlot); // 01-99
	std::string filename = file3dsGetAssociatedFilename(Memory.ROMFilename, ext, "screenshots");
	snprintf(path, _MAX_PATH - 1, "%s", filename.c_str());

	int bpp = gpu3dsGetPixelSize(GPU_RGB8);
	int channels = 3;
    
	int x0 = screenshot.x;
	int y0 = screenshot.y;
	int x1 = x0 + screenshot.width;
	int y1 = y0 + screenshot.height;

	// sync frame buffer
	gspWaitForVBlank();
	gfxScreenSwapBuffers(screenSettings.GameScreen, false);
    
	u8* fb = (u8*)gfxGetFramebuffer(screenSettings.GameScreen, GFX_LEFT, NULL, NULL); // expecting GSP_BGR8_OES format
	u32* imageData = (u32*)screenshot.data; // Cast to u32*

	for (int x = x0; x < x1; x++) {
		for (int y = y0; y < y1; y++) {
			int si = (y - y0) * screenshot.width + (x - x0);
			int fb_col = SCREEN_HEIGHT - 1 - y;
			int fbofs = (x * SCREEN_HEIGHT + fb_col) * bpp;

			u8 b = fb[fbofs + 0];
			u8 g = fb[fbofs + 1];
			u8 r = fb[fbofs + 2];
			
			imageData[si] = (0xFF << 24) | (b << 16) | (g << 8) | r;
		}
	}

    bool success = ui3dsSavePNG(path, screenshot.width, screenshot.height, channels, imageData);
	log3dsWrite("screenshot saved %s: %s", path, success ? "v" : "x");

	screenshot.dirty = false;
	snd3DS.generateSilence = false;

	settings3DS.ScreenshotSlot++;
	settings3DS.ScreenFilter = screenshot.prevFilter;
	settings3DS.dirty = true;

	if (menuOpen) {
		// restore pause screen
		impl3dsDrawPauseScreen();
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
    snd3DS.generateSilence = true;
	std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ".srm", "saves");

	if (!path.empty()) {
		Memory.SaveSRAM (path.c_str());
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
	const char		*d;
	struct stat		buf;

	_splitpath(Memory.ROMFilename, drive, dir, fname, ext);

	do
		snprintf(s, PATH_MAX + 1, "%s/%s.%03d%s", dir, fname, i++, ex);
	while (stat(s, &buf) == 0 && i < 1000);

	return (s);
}


bool8 S9xReadMousePosition (int which1_0_to_1, int &x, int &y, uint32 &buttons)
{

}

bool8 S9xReadSuperScopePosition (int &x, int &y, uint32 &buttons)
{

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

    if (p = strrchr (f, SLASH_CHAR))
	return (p + 1);

    return (f);
}


bool8 S9xOpenSnapshotFile (const char *filename, bool8 read_only, STREAM *file)
{

	char	s[PATH_MAX + 1];
	char	drive[_MAX_DRIVE + 1], dir[_MAX_DIR + 1], fname[_MAX_FNAME + 1], ext[_MAX_EXT + 1];

    snprintf(s, PATH_MAX + 1, "%s", filename);

	if ((*file = OPEN_STREAM(s, read_only ? "rb" : "wb")))
		return (TRUE);

	return (FALSE);
}

void S9xCloseSnapshotFile (STREAM file)
{
	CLOSE_STREAM(file);
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
    if (which1_0_to_4 != 0)
        return 0;

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
