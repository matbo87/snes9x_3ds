#ifndef _3DSSAVE_H_
#define _3DSSAVE_H_

#include <stddef.h>

// Allocates the save worker reserve and starts its thread; failure is fatal.
bool sav3dsInitialize();
void sav3dsFinalize();

// Slot N or the auto state of the loaded ROM; true on success.
bool sav3dsSaveSlot(int slotNumber);
bool sav3dsLoadSlot(int slotNumber);
// No-ops without a loaded ROM or with auto savestates off.
void sav3dsSaveAuto();
void sav3dsLoadAuto();

// Hotkeys on settings3DS.CurrentSaveSlot. A quick save serializes on the main
// thread and writes on the save worker; HOME and sleep stay disabled until it completes.
void sav3dsQuickSave();
void sav3dsQuickLoad();

bool sav3dsIsSaveInProgress();
// Processes completion on the main thread; waitForCompletion blocks until the worker finishes.
void sav3dsProcessSaveCompletion(bool waitForCompletion);

bool sav3dsSlotHasState(int slotNumber);
void sav3dsUpdateSlotState(int slotNumber);
void sav3dsSelectSlot(int direction);

void sav3dsGetStateScreenshotPath(int slotNumber, char* out, size_t bufferSize);
void sav3dsEnsureStateScreenshotDir();
void sav3dsDeleteStateScreenshots();

#endif
