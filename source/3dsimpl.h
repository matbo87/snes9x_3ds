#include "3dssnes9x.h"
#include "3dsgpu.h"

#ifndef _3DSIMPL_H
#define _3DSIMPL_H

#define BTN3DS_A        0
#define BTN3DS_B        1
#define BTN3DS_X        2
#define BTN3DS_Y        3
#define BTN3DS_L        4
#define BTN3DS_R        5
#define BTN3DS_ZL       6
#define BTN3DS_ZR       7
#define BTN3DS_SELECT   8
#define BTN3DS_START    9

// ensure to update HOTKEYS_COUNT in 3dssettings
#define HOTKEY_OPEN_MENU            0
#define HOTKEY_DISABLE_FRAMELIMIT   1
#define HOTKEY_SWAP_CONTROLLERS     2
#define HOTKEY_QUICK_SAVE           3
#define HOTKEY_QUICK_LOAD           4
#define HOTKEY_SAVE_SLOT_NEXT       5
#define HOTKEY_SAVE_SLOT_PREV       6
#define HOTKEY_SCREENSHOT           7


typedef enum
{
	SAVELOAD_IN_PROGRESS = 0,
    SAVELOAD_SUCCEEDED = 1,
    SAVELOAD_FAILED = 2,
} saveLoad_state;

typedef enum
{
    HIDDEN = 0,
	VISIBLE = 1,
	WAIT = 2,
 } dialog_state;

typedef struct
{
	uint32_t*       Buffer;
	char*           File;
	int             Width;
	int             Height;
    int             Bounds[4];
} RGB8Image;

typedef struct
{
	int             Padding = 20;
	int             MaxLines = 2;
	int             Height = 13 * MaxLines + Padding * 2;
	int             Bounds[4];
    int             Color = 0x4CAF50;
    float           Alpha = 0.8f;
    dialog_state    State = HIDDEN;
} SecondScreenDialog;

extern SecondScreenDialog secondScreenDialog;

//---------------------------------------------------------
// 3DS textures
//---------------------------------------------------------

extern SGPUTexture *snesMainScreenTarget;
extern SGPUTexture *snesSubScreenTarget;

extern SGPUTexture *snesTileCacheTexture;
extern SGPUTexture *snesMode7FullTexture;
extern SGPUTexture *snesMode7TileCacheTexture;
extern SGPUTexture *snesMode7Tile0Texture;

extern SGPUTexture *snesDepthForScreens;
extern SGPUTexture *snesDepthForOtherTextures;

//---------------------------------------------------------
// Initializes the emulator core.
//
// You must call snd3dsSetSampleRate here to set 
// the CSND's sampling rate.
//---------------------------------------------------------
bool impl3dsInitializeCore();

//---------------------------------------------------------
// Finalizes and frees up any resources.
//---------------------------------------------------------
void impl3dsFinalize();


//---------------------------------------------------------
// Mix sound samples into a temporary buffer.
//
// This gives time for the sound generation to execute
// from the 2nd core before copying it to the actual
// output buffer.
//---------------------------------------------------------
void impl3dsGenerateSoundSamples();


//---------------------------------------------------------
// Mix sound samples into a temporary buffer.
//
// This gives time for the sound generation to execute
// from the 2nd core before copying it to the actual
// output buffer.
//---------------------------------------------------------
void impl3dsOutputSoundSamples(short *leftSamples, short *rightSamples);


//---------------------------------------------------------
// This is called when a ROM needs to be loaded and the
// emulator engine initialized.
//---------------------------------------------------------
bool impl3dsLoadROM(char *romFilePath);


//---------------------------------------------------------
// This is called when the user chooses to reset the
// console
//---------------------------------------------------------
void impl3dsResetConsole();


//---------------------------------------------------------
// This is called when preparing to start emulating
// a new frame. Use this to do any preparation of data
// and the hardware before the frame is emulated.
//---------------------------------------------------------
void impl3dsPrepareForNewFrame();


//---------------------------------------------------------
// Executes one frame.
//
// Note: TRUE will be passed in the firstFrame if this
// frame is to be run just after the emulator has booted
// up or returned from the menu.
//---------------------------------------------------------
void impl3dsRunOneFrame(bool firstFrame, bool skipDrawingFrame);


//---------------------------------------------------------
// This is called when the bottom screen is touched
// during emulation, and the emulation engine is ready
// to display the pause menu.
//---------------------------------------------------------
void impl3dsTouchScreenPressed();


//---------------------------------------------------------
// This is called when the user chooses to save the state.
// This function should save the state into a file whose
// name contains the slot number. This will return
// true if the state is saved successfully.
//---------------------------------------------------------
bool impl3dsSaveStateSlot(int slotNumber);


//---------------------------------------------------------
// This is called when a game or the emulator is exiting
// and the user has enabled the auto-savestate option.
// This saves the current game's state into a game-specific
// file whose name indicates that it's an automatic state.
// Returns true if the state has been saved successfully.
//---------------------------------------------------------
bool impl3dsSaveStateAuto();


//---------------------------------------------------------
// Saves the current game's state to the given filename.
// Returns true if the state has been saved successfully.
//---------------------------------------------------------
bool impl3dsSaveState(const char* filename);


//---------------------------------------------------------
// This is called when the user chooses to load the state.
// This function should load the state from the file that
// impl3dsSaveStateSlot() saves to when called with the
// same slotNumber.
// Returns true if the state has been loaded successfully.
//---------------------------------------------------------
bool impl3dsLoadStateSlot(int slotNumber);


//---------------------------------------------------------
// This is called when on game boot when the user has
// enabled the auto-savestate option.
// This loads the the state from the file that
// impl3dsSaveStateAuto() saves to.
// Returns true if the state has been saved successfully.
//---------------------------------------------------------
bool impl3dsLoadStateAuto();


//---------------------------------------------------------
// Loads the state from the given filename.
// Returns true if the state has been loaded successfully.
//---------------------------------------------------------
bool impl3dsLoadState(const char* filename);


const char *S9xGetFilename (const char *extension);
const char *S9xGetFilenameInc (const char *);
const char *S9xBasename (const char *f);
uint32 S9xReadJoypad (int which1_0_to_4);
bool8 S9xReadMousePosition (int which1_0_to_1, int &x, int &y, uint32 &buttons);
bool8 S9xReadSuperScopePosition (int &x, int &y, uint32 &buttons);
void S9xNextController ();
void impl3dsQuickSaveLoad(bool saveMode);
void impl3dsSelectSaveSlot(int direction);
void impl3dsSwapJoypads();
bool impl3dsTakeScreenshot();
void impl3dsSaveLoadShowMessage(bool saveMode, saveLoad_state state);
void impl3dsRenderScreenImage(gfxScreen_t targetScreen, const char *imgFilePath);

inline void clearScreen(gfxScreen_t targetScreen) {
    uint bytes = 0;
    switch (gfxGetScreenFormat(targetScreen))
    {
        case GSP_RGBA8_OES:
            bytes = 4;
            break;

        case GSP_BGR8_OES:
            bytes = 3;
            break;

        case GSP_RGB565_OES:
        case GSP_RGB5_A1_OES:
        case GSP_RGBA4_OES:
            bytes = 2;
            break;
    }

    u8 *frame = gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL);
    memset(frame, 0, (targetScreen == GFX_TOP ? 400 : 320) * 240 * bytes);
}

#endif