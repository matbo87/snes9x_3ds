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
#include <stb_image.h>

#include <dirent.h>
#include "snes9x.h"
#include "memmap.h"
#include "apu.h"
#include "gfx.h"
#include "snapshot.h"
#include "cheats.h"
#include "soundux.h"

#include "3dssnes9x.h"
#include "3dsexit.h"
#include "3dsfiles.h"
#include "3dsgpu.h"
#include "3dssound.h"
#include "3dsmenu.h"
#include "3dsui.h"
#include "3dsinput.h"
#include "3dssettings.h"
#include "3dsimpl.h"
#include "3dsimpl_tilecache.h"
#include "3dsimpl_gpu.h"

// Compiled shaders
//
#include "shaderfast_shbin.h"
#include "shaderfast2_shbin.h"
#include "shaderfast3_shbin.h"
#include "shaderfastm7_shbin.h"

#include "shaderslow_shbin.h"
#include "shaderslow2_shbin.h"
#include "shaderslow3_shbin.h"
#include "shaderslowm7_shbin.h"


//------------------------------------------------------------------------
// Memory Usage = 0.26 MB   for 4-point rectangle (triangle strip) vertex buffer
#define RECTANGLE_BUFFER_SIZE           0x40000

//------------------------------------------------------------------------
// Memory Usage = 8.00 MB   for 6-point quad vertex buffer (Citra only)
#define CITRA_VERTEX_BUFFER_SIZE        0x800000

// Memory Usage = Not used (Real 3DS only)
#define CITRA_TILE_BUFFER_SIZE          0x200

// Memory usage = 2.00 MB   for 6-point full texture mode 7 update buffer
#define CITRA_M7_BUFFER_SIZE            0x200000

// Memory usage = 0.39 MB   for 2-point mode 7 scanline draw
#define CITRA_MODE7_LINE_BUFFER_SIZE    0x60000


//------------------------------------------------------------------------
// Memory Usage = 0.06 MB   for 6-point quad vertex buffer (Real 3DS only)
#define REAL3DS_VERTEX_BUFFER_SIZE      0x1000

// Memory Usage = 3.00 MB   for 2-point rectangle vertex buffer (Real 3DS only)
#define REAL3DS_TILE_BUFFER_SIZE        0x300000

// Memory usage = 0.78 MB   for 2-point full texture mode 7 update buffer
#define REAL3DS_M7_BUFFER_SIZE          0xC0000

// Memory usage = 0.13 MB   for 2-point mode 7 scanline draw
#define REAL3DS_MODE7_LINE_BUFFER_SIZE  0x20000


//---------------------------------------------------------
// Our textures
//---------------------------------------------------------
SGPUTexture *borderTexture;
SGPUTexture *snesMainScreenTarget;
SGPUTexture *snesSubScreenTarget;

SGPUTexture *snesTileCacheTexture;
SGPUTexture *snesMode7FullTexture;
SGPUTexture *snesMode7TileCacheTexture;
SGPUTexture *snesMode7Tile0Texture;

SGPUTexture *snesDepthForScreens;
SGPUTexture *snesDepthForOtherTextures;

static u32 screen_next_pow_2(u32 i) {
    i--;
    i |= i >> 1;
    i |= i >> 2;
    i |= i >> 4;
    i |= i >> 8;
    i |= i >> 16;
    i++;

    return i;
}

radio_state slotStates[SAVESLOTS_MAX];
float currentBorderAlpha = -1;

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
	snd3dsSetSampleRate(32000, 256);

	// Initialize our tile cache engine.
	//
    cache3dsInit();

	// Initialize our GPU.
	// Load up and initialize any shaders
	//
    if (GPU3DS.isReal3DS)
    {
        gpu3dsLoadShader(0, (u32 *)shaderfast_shbin, shaderfast_shbin_size, 6);
    	gpu3dsLoadShader(1, (u32 *)shaderslow_shbin, shaderslow_shbin_size, 0);     // copy to screen
    	gpu3dsLoadShader(2, (u32 *)shaderfast2_shbin, shaderfast2_shbin_size, 6);   // draw tiles
        gpu3dsLoadShader(3, (u32 *)shaderfastm7_shbin, shaderfastm7_shbin_size, 3); // mode 7 shader
    }
    else
    {
    	gpu3dsLoadShader(0, (u32 *)shaderslow_shbin, shaderslow_shbin_size, 0);
    	gpu3dsLoadShader(1, (u32 *)shaderslow_shbin, shaderslow_shbin_size, 0);     // copy to screen
        gpu3dsLoadShader(2, (u32 *)shaderslow2_shbin, shaderslow2_shbin_size, 0);   // draw tiles
        gpu3dsLoadShader(3, (u32 *)shaderslowm7_shbin, shaderslowm7_shbin_size, 0); // mode 7 shader
    }

	gpu3dsInitializeShaderRegistersForRenderTarget(0, 10);
	gpu3dsInitializeShaderRegistersForTexture(4, 14);
	gpu3dsInitializeShaderRegistersForTextureOffset(6);
	gpu3dsUseShader(0);

    // Create all the necessary textures
    //
    snesTileCacheTexture = gpu3dsCreateTextureInLinearMemory(1024, 1024, GPU_RGBA5551);
    snesMode7TileCacheTexture = gpu3dsCreateTextureInLinearMemory(128, 128, GPU_RGBA4);

    // This requires 16x16 texture as a minimum
    snesMode7Tile0Texture = gpu3dsCreateTextureInVRAM(16, 16, GPU_RGBA4);    //
    snesMode7FullTexture = gpu3dsCreateTextureInVRAM(1024, 1024, GPU_RGBA4); // 2.000 MB

    // Main screen requires 8-bit alpha, otherwise alpha blending will not work well
    snesMainScreenTarget = gpu3dsCreateTextureInVRAM(256, 256, GPU_RGBA8);      // 0.250 MB
    snesSubScreenTarget = gpu3dsCreateTextureInVRAM(256, 256, GPU_RGBA8);       // 0.250 MB

    // Depth texture for the sub / main screens.
    // Performance: Create depth buffers in VRAM improves GPU performance!
    //              Games like Axelay, F-Zero (EUR) now run close to full speed!
    //
    snesDepthForScreens = gpu3dsCreateTextureInVRAM(256, 256, GPU_RGBA8);       // 0.250 MB
    snesDepthForOtherTextures = gpu3dsCreateTextureInVRAM(512, 512, GPU_RGBA8); // 1.000 MB

    if (snesTileCacheTexture == NULL || snesMode7FullTexture == NULL ||
        snesMode7TileCacheTexture == NULL || snesMode7Tile0Texture == NULL ||
        snesMainScreenTarget == NULL || snesSubScreenTarget == NULL ||
        snesDepthForScreens == NULL  || snesDepthForOtherTextures == NULL)
    {
        printf ("Unable to allocate textures\n");
        return false;
    }

    if (GPU3DS.isReal3DS)
    {
        gpu3dsAllocVertexList(&GPU3DSExt.rectangleVertexes, RECTANGLE_BUFFER_SIZE, sizeof(SVertexColor), 2, SVERTEXCOLOR_ATTRIBFORMAT);
        gpu3dsAllocVertexList(&GPU3DSExt.mode7TileVertexes, sizeof(SMode7TileVertex) * 16400 * 1 * 2 + 0x200, sizeof(SMode7TileVertex), 2, SMODE7TILEVERTEX_ATTRIBFORMAT);
        gpu3dsAllocVertexList(&GPU3DSExt.quadVertexes, REAL3DS_VERTEX_BUFFER_SIZE, sizeof(STileVertex), 2, STILEVERTEX_ATTRIBFORMAT);
        gpu3dsAllocVertexList(&GPU3DSExt.tileVertexes, REAL3DS_TILE_BUFFER_SIZE, sizeof(STileVertex), 2, STILEVERTEX_ATTRIBFORMAT);
        gpu3dsAllocVertexList(&GPU3DSExt.mode7LineVertexes, REAL3DS_MODE7_LINE_BUFFER_SIZE, sizeof(SMode7LineVertex), 2, SMODE7LINEVERTEX_ATTRIBFORMAT);
    }
    else
    {
        gpu3dsAllocVertexList(&GPU3DSExt.rectangleVertexes, RECTANGLE_BUFFER_SIZE, sizeof(SVertexColor), 2, SVERTEXCOLOR_ATTRIBFORMAT);
        gpu3dsAllocVertexList(&GPU3DSExt.mode7TileVertexes, sizeof(SMode7TileVertex) * 16400 * 6 * 2 + 0x200, sizeof(SMode7TileVertex), 2, SMODE7TILEVERTEX_ATTRIBFORMAT);
        gpu3dsAllocVertexList(&GPU3DSExt.quadVertexes, CITRA_VERTEX_BUFFER_SIZE, sizeof(STileVertex), 2, STILEVERTEX_ATTRIBFORMAT);
        gpu3dsAllocVertexList(&GPU3DSExt.tileVertexes, CITRA_TILE_BUFFER_SIZE, sizeof(STileVertex), 2, STILEVERTEX_ATTRIBFORMAT);
        gpu3dsAllocVertexList(&GPU3DSExt.mode7LineVertexes, CITRA_MODE7_LINE_BUFFER_SIZE, sizeof(SMode7LineVertex), 2, SMODE7LINEVERTEX_ATTRIBFORMAT);
    }

    if (GPU3DSExt.quadVertexes.ListBase == NULL ||
        GPU3DSExt.tileVertexes.ListBase == NULL ||
        GPU3DSExt.rectangleVertexes.ListBase == NULL ||
        GPU3DSExt.mode7TileVertexes.ListBase == NULL ||
        GPU3DSExt.mode7LineVertexes.ListBase == NULL)
    {
        printf ("Unable to allocate vertex list buffers \n");
        return false;
    }

	// Initialize the vertex list for mode 7.
	//
    gpu3dsInitializeMode7Vertexes();



	// Initialize our SNES core
	//
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
        printf ("Unable to initialize memory.\n");
        return false;
    }

    if(!S9xInitAPU())
    {
        printf ("Unable to initialize APU.\n");
        return false;
    }

    if(!S9xGraphicsInit())
    {
        printf ("Unable to initialize graphics.\n");
        return false;
    }


    if(!S9xInitSound (
        7, Settings.Stereo,
        Settings.SoundBufferSize))
    {
        printf ("Unable to initialize sound.\n");
        return false;
    }
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
	// Frees up all vertex lists
	//
    gpu3dsDeallocVertexList(&GPU3DSExt.mode7TileVertexes);
    gpu3dsDeallocVertexList(&GPU3DSExt.rectangleVertexes);
    gpu3dsDeallocVertexList(&GPU3DSExt.quadVertexes);
    gpu3dsDeallocVertexList(&GPU3DSExt.tileVertexes);
    gpu3dsDeallocVertexList(&GPU3DSExt.mode7LineVertexes);
	
	// Frees up all textures.
	//
    gpu3dsDestroyTextureFromLinearMemory(snesTileCacheTexture);
    gpu3dsDestroyTextureFromLinearMemory(snesMode7TileCacheTexture);

    gpu3dsDestroyTextureFromVRAM(snesMode7Tile0Texture);
    gpu3dsDestroyTextureFromVRAM(snesMode7FullTexture);
    gpu3dsDestroyTextureFromVRAM(snesMainScreenTarget);
    gpu3dsDestroyTextureFromVRAM(snesSubScreenTarget);

    gpu3dsDestroyTextureFromVRAM(snesDepthForOtherTextures);
    gpu3dsDestroyTextureFromVRAM(snesDepthForScreens);
	if (borderTexture)
    	gpu3dsDestroyTextureFromVRAM(borderTexture);

#ifndef RELEASE
    printf("S9xGraphicsDeinit:\n");
#endif
    S9xGraphicsDeinit();

#ifndef RELEASE
    printf("S9xDeinitAPU:\n");
#endif
    S9xDeinitAPU();
    
#ifndef RELEASE
    printf("Memory.Deinit:\n");
#endif
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

void impl3dsUpdateBorderTexture(StoredFile borderImage, float alpha, GPU_TEXCOLOR pixelFormat = GPU_RGB8) {
	int width, height, n;
	int channels = (pixelFormat == GPU_RGBA8) ? 4 : 3;
    unsigned char *imageData = stbi_load_from_memory(borderImage.Buffer.data(), borderImage.Buffer.size(), &width, &height, &n, channels);

	u32 pow2Width = screen_next_pow_2(width);
	u32 pow2Height = screen_next_pow_2(height);
    size_t bufferSize = pow2Width * pow2Height * channels;

	u8* pow2Tex = (u8*)linearAlloc(bufferSize);
	memset(pow2Tex, 0, bufferSize);

	for(int x = 0; x < width; x++) {
		for(int y = 0; y < height; y++) {
            int si = (y * width + x) * channels;
            int di =(x + y * pow2Width) * channels;

			for (int i = 0; i < channels; i++) {
				pow2Tex[di + i] = (((u8*) imageData)[si + channels - i - 1] * (int)(alpha * 255)) >> 8;
			}
		}
	}

	GSPGPU_FlushDataCache(pow2Tex, bufferSize);

	if (!borderTexture) {
		borderTexture = gpu3dsCreateTextureInVRAM(pow2Width, pow2Height, pixelFormat);
	}

	GX_DisplayTransfer((u32*)pow2Tex,GX_BUFFER_DIM(pow2Width, pow2Height),(u32*)borderTexture->PixelData,GX_BUFFER_DIM(pow2Width, pow2Height),
	GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | GX_TRANSFER_IN_FORMAT(pixelFormat) |
	GX_TRANSFER_OUT_FORMAT((u32) pixelFormat) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));

	gspWaitForPPF();
	
	linearFree(pow2Tex);
		
	if (imageData) {
        stbi_image_free(imageData);
    }
}

//---------------------------------------------------------
// Border image for game screen
//---------------------------------------------------------
int x = 0;
void impl3dsSetBorderImage() {
	if (settings3DS.GameBorder == 0) {
		if (borderTexture) {
			gpu3dsDestroyTextureFromVRAM(borderTexture);
			borderTexture = NULL;
		}

		return;
	}
	
	std::string borderFilename;
	
	if (settings3DS.GameBorder == 1) {
		if (settings3DS.RomFsLoaded) 
			borderFilename = "romfs:/border.png";
	} else {
		borderFilename = file3dsGetAssociatedFilename(Memory.ROMFilename, ".png", "borders", true);
	}

	if (borderFilename.empty()) {
		return;
	}
	
	float borderAlpha = (float)(settings3DS.GameBorderOpacity) / OPACITY_STEPS;

	StoredFile currentBorder = file3dsGetStoredFileById("gameBorder");
	bool imageChanged = currentBorder.Filename != borderFilename || !borderTexture;
	bool alphaChanged = currentBorderAlpha != borderAlpha;

	if (!imageChanged && !alphaChanged) {
		return;
	}

	StoredFile border = file3dsAddFileBufferToMemory("gameBorder", borderFilename);
	currentBorderAlpha = borderAlpha;

	if (border.Buffer.empty()) {
		if (borderTexture) {
			gpu3dsDestroyTextureFromVRAM(borderTexture);
			borderTexture = NULL;
		}

		return;
	}
	
	impl3dsUpdateBorderTexture(border, borderAlpha);
}

//---------------------------------------------------------
// This is called when a ROM needs to be loaded and the
// emulator engine initialized.
//---------------------------------------------------------
bool impl3dsLoadROM(char *romFilePath)
{
    bool loaded = Memory.LoadROM(romFilePath);

	if(loaded) {
		std::string path = file3dsGetAssociatedFilename(romFilePath, ".srm", "saves");

		if (!path.empty()) {
    		Memory.LoadSRAM (path.c_str());
		}

        // ensure controller is always set to player 1 when rom has loaded
        Settings.SwapJoypads = 0;
    	
		gpu3dsInitializeMode7Vertexes();
    	gpu3dsCopyVRAMTilesIntoMode7TileVertexes(Memory.VRAM);
    	cache3dsInit();
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
	cache3dsInit();
	gpu3dsInitializeMode7Vertexes();
	gpu3dsCopyVRAMTilesIntoMode7TileVertexes(Memory.VRAM);
}


//---------------------------------------------------------
// This is called when preparing to start emulating
// a new frame. Use this to do any preparation of data
// and the hardware before the frame is emulated.
//---------------------------------------------------------
void impl3dsPrepareForNewFrame()
{
    gpu3dsSwapVertexListForNextFrame(&GPU3DSExt.quadVertexes);
    gpu3dsSwapVertexListForNextFrame(&GPU3DSExt.tileVertexes);
    gpu3dsSwapVertexListForNextFrame(&GPU3DSExt.rectangleVertexes);
    gpu3dsSwapVertexListForNextFrame(&GPU3DSExt.mode7LineVertexes);	
}


//---------------------------------------------------------
// Executes one frame.
//---------------------------------------------------------
void impl3dsRunOneFrame(bool firstFrame, bool skipDrawingFrame)
{
	Memory.ApplySpeedHackPatches();
	gpu3dsEnableAlphaBlending();

	if (GPU3DS.emulatorState != EMUSTATE_EMULATE)
		return;

	IPPU.RenderThisFrame = !skipDrawingFrame;

	gpu3dsSetRenderTargetToMainScreenTexture();
	gpu3dsUseShader(2);             // for drawing tiles

#ifdef RELEASE
	if (!Settings.SA1)
		S9xMainLoop();
	else
		S9xMainLoopWithSA1();
#else
	if (!Settings.Paused)
	{
		if (!Settings.SA1)
			S9xMainLoop();
		else
			S9xMainLoopWithSA1();
	}
#endif

	// ----------------------------------------------
	// Copy the SNES main/sub screen to the 3DS frame
	// buffer
	// (Can this be done in the V_BLANK?)
	t3dsStartTiming(3, "CopyFB");
	gpu3dsSetRenderTargetToFrameBuffer(screenSettings.GameScreen);
	if (firstFrame)
	{
		// Clear the entire frame buffer to black, including the borders
		//
		gpu3dsDisableAlphaBlending();
		gpu3dsSetTextureEnvironmentReplaceColor();
		gpu3dsDrawRectangle(0, 0, screenSettings.GameScreenWidth, SCREEN_HEIGHT, 0, 0x000000ff);
		gpu3dsEnableAlphaBlending();
	}

	gpu3dsUseShader(1);             // for copying to screen.
	gpu3dsDisableAlphaBlending();
	gpu3dsDisableDepthTest();
	gpu3dsDisableAlphaTest();
	
	if(settings3DS.GameBorder > 0 && borderTexture)
	{
		// Copy the border texture  to the 3DS frame
		gpu3dsBindTexture(borderTexture, GPU_TEXUNIT0);
		gpu3dsSetTextureEnvironmentReplaceTexture0();
		gpu3dsDisableStencilTest();
		
		int bx0 = (screenSettings.GameScreenWidth - SCREEN_TOP_WIDTH) / 2;
		int bx1 = bx0 + SCREEN_TOP_WIDTH;
		gpu3dsAddQuadVertexes(bx0, 0, bx1, SCREEN_HEIGHT, 0, 0, SCREEN_TOP_WIDTH, SCREEN_HEIGHT, 0.1f);
	
		gpu3dsDrawVertexes();
	}
	
	gpu3dsBindTextureMainScreen(GPU_TEXUNIT0);
	gpu3dsSetTextureEnvironmentReplaceTexture0();
	gpu3dsDisableStencilTest();

    int sWidth = settings3DS.StretchWidth;

	// PPU.ScreenHeight - 1 seems necessary for pixel perfect image. 224px height causes blurryness otherwise
    int sHeight = (settings3DS.StretchHeight == -1 ? PPU.ScreenHeight - 1 : settings3DS.StretchHeight);

	int sx0 = (screenSettings.GameScreenWidth - sWidth) / 2;
	int sx1 = sx0 + sWidth;
	int sy0 = (SCREEN_HEIGHT - sHeight) / 2;
	int sy1 = sy0 + sHeight;

	gpu3dsAddQuadVertexes(
		sx0, sy0, sx1, sy1,
		settings3DS.CropPixels, settings3DS.CropPixels ? settings3DS.CropPixels : 1, 
		256 - settings3DS.CropPixels, PPU.ScreenHeight - settings3DS.CropPixels, 
		0.1f);
	gpu3dsDrawVertexes();

	t3dsEndTiming(3);

	if (!firstFrame)
	{
		// ----------------------------------------------
		// Wait for the rendering to the SNES
		// main/sub screen for the previous frame
		// to complete
		//
		t3dsStartTiming(5, "Transfer");
		gpu3dsTransferToScreenBuffer(screenSettings.GameScreen);
		gpu3dsSwapScreenBuffers();
		t3dsEndTiming(5);

	}
	else
	{
		firstFrame = false;
	}

	// ----------------------------------------------
	// Flush all draw commands of the current frame
	// to the GPU.
	t3dsStartTiming(4, "Flush");
	gpu3dsFlush();
	t3dsEndTiming(4);

	t3dsEndTiming(1);


	// For debugging only.
	/*if (!GPU3DS.isReal3DS)
	{
		snd3dsMixSamples();
		//snd3dsMixSamples();
		//printf ("---\n");
	}*/

	/*
	// Debugging only
	snd3dsMixSamples();
	printf ("\n");

	S9xPrintAPUState ();
	printf ("----\n");*/
	
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
		// reset last slot
		if (settings3DS.CurrentSaveSlot != slotNumber && settings3DS.CurrentSaveSlot > 0)
			impl3dsUpdateSlotState(settings3DS.CurrentSaveSlot);

		impl3dsUpdateSlotState(slotNumber, false, true);
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
		// reset last slot
		if (settings3DS.CurrentSaveSlot != slotNumber && settings3DS.CurrentSaveSlot > 0)
			impl3dsUpdateSlotState(settings3DS.CurrentSaveSlot);
			
		impl3dsUpdateSlotState(slotNumber, false, true);
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
		gpu3dsCopyVRAMTilesIntoMode7TileVertexes(Memory.VRAM);
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
			dialogBackgroundColor = DIALOGCOLOR_CYAN;
			snprintf(message, _MAX_PATH, "%s slot #%d...", saveMode ? "Saving into" : "Loading from", settings3DS.CurrentSaveSlot);
			break;
		case SAVELOAD_SUCCEEDED:
			dialogBackgroundColor = DIALOGCOLOR_GREEN;
			snprintf(message, _MAX_PATH, "Slot %d %s.", settings3DS.CurrentSaveSlot, saveMode ? "save completed" : "loaded");
			break;
		case SAVELOAD_FAILED:
			dialogBackgroundColor = DIALOGCOLOR_RED;
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
	menu3dsSetSecondScreenContent(message, DIALOGCOLOR_GREEN);
}

void impl3dsSwapJoypads() {
    Settings.SwapJoypads = Settings.SwapJoypads ? false : true;

    char message[_MAX_PATH];
	snprintf(message, _MAX_PATH - 1, "Controllers Swapped.\nPlayer #%d active.", Settings.SwapJoypads ? 2 : 1);
	menu3dsSetSecondScreenContent(message, DIALOGCOLOR_GREEN);
}

bool impl3dsTakeScreenshot(const char*& path, bool menuOpen) {
	if (snd3DS.generateSilence || ui3dsGetSecondScreenDialogState() != HIDDEN) return false;
	
	snd3DS.generateSilence = true;

	if (!menuOpen) {
		menu3dsSetSecondScreenContent("Now taking a screenshot...\nThis may take a while.", DIALOGCOLOR_CYAN);
	}


	// Loop through and look for an non-existing file name.
	int i = 1;
	std::string ext;
	static char	tmp[_MAX_PATH];

	while (i <= 99) {
		ext = "." + std::to_string(i) + ".png";
		std::string filename = file3dsGetAssociatedFilename(Memory.ROMFilename, ext.c_str(), "screenshots");
		snprintf(tmp, _MAX_PATH - 1, "%s", filename.c_str());
		
		if (!filename.empty() && !IsFileExists(tmp)) {
			path = tmp;
			break;
		}
		i++;
	}

	bool success = false;
	if (path) {
		success = menu3dsTakeScreenshot(path);
	}
	
	snd3DS.generateSilence = false;

	if (menuOpen)
		return success;

	char message[_MAX_PATH];

	if (success)
		snprintf(message, _MAX_PATH - 1, "Done! File saved to\n%s", path);
	else
		snprintf(message, _MAX_PATH - 1, "%s", "Oops. Unable to take screenshot!");
	
	
	menu3dsSetSecondScreenContent(message, (success ? DIALOGCOLOR_GREEN : DIALOGCOLOR_RED));

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
