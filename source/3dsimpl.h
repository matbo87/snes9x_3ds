
#ifndef _3DSIMPL_H
#define _3DSIMPL_H

#include <3ds.h>

#include "snes9x.h"
#include "3dssettings.h"

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

typedef enum
{
    SCREENSHOT_DEFAULT = 0,   // manual screenshot -> screenshots/<rom>.<timestamp>.png
    SCREENSHOT_SAVESTATE,     // savestate screenshot -> savestates/<rom>.<slot>.png
} ScreenshotType;

typedef struct
{
    u16                         x;
    u16                         y;
    u16                         width;
    u16                         height;
    float                       scale;
    bool                        dirty;
    ScreenshotType              type;       // what file the next capture writes
    int                         slot;       // target slot when type == SCREENSHOT_SAVESTATE
} S9xScreenshot;

extern S9xScreenshot screenshot;
extern bool skipNextFpsUpdate;

//---------------------------------------------------------
// Initializes the emulator core.
//---------------------------------------------------------
bool impl3dsInitialize();

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
//---------------------------------------------------------
// Executes one frame.
//
// Note: TRUE will be passed in the firstFrame if this
// frame is to be run just after the emulator has booted
// up or returned from the menu.
//---------------------------------------------------------
void impl3dsRunOneFrame(bool firstFrame, bool skipDrawingFrame);


const char *S9xGetFilename ();
const char *S9xGetFilenameInc (const char *);
const char *S9xBasename (const char *f);
uint32 S9xReadJoypad (int which1_0_to_4);
bool8 S9xReadMousePosition (int which1_0_to_1, int &x, int &y, uint32 &buttons);
bool8 S9xReadSuperScopePosition (int &x, int &y, uint32 &buttons);
void S9xNextController ();
void impl3dsSaveCheats();

void impl3dsSwapJoypads();

void impl3dsPrepareScreenshot(float scale = 1.0f, bool centered = true);
// With pixels, captures RGB8 into it instead of writing path.
bool impl3dsTakeScreenshot(char *path, size_t bufferSize, bool renderFrame, u8* pixels = nullptr, size_t pixelsCapacity = 0);
void impl3dsGetScreenshotPath(ScreenshotType type, int slotNumber, char* out, size_t bufferSize);

void impl3dsUpdateUiAssets();

void impl3dsLoadCheats();

// Restore patched bytes before reloading cheats.
void impl3dsReloadCheats();

void impl3dsFlushScreen(gfxScreen_t screen, bool isTopStereo = false, bool isWide = false);
void impl3dsInvalidateScreen(gfxScreen_t screen, bool isTopStereo = false, bool isWide = false);
void impl3dsClearTopFramebuffers();

void impl3dsSceneRender(bool firstFrame, bool paused = false);
bool impl3dsPauseAnimationRunning();

#endif
