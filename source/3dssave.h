#ifndef _3DSSAVE_H_
#define _3DSSAVE_H_

#include <stddef.h>
#include <3ds/types.h>

bool sav3dsInitialize();
// Call only after all saves have finished.
void sav3dsFinalize();

bool sav3dsSaveSlot(int slotNumber);
bool sav3dsLoadSlot(int slotNumber);
// No-op without a loaded ROM or with AutoSavestate disabled.
void sav3dsSaveAuto();
void sav3dsLoadAuto();

// Use CurrentSaveSlot; quick saves are refused while an SRAM write runs.
// Async saves disable HOME/sleep until completion is processed.
void sav3dsQuickSave();
void sav3dsQuickLoad();

// Defers until the worker is idle; SPC7110 RTC saves synchronously.
void sav3dsRequestSRAMSave();

// Any job (quick save or SRAM).
bool sav3dsIsSaveInProgress();
bool sav3dsIsQuickSaveInProgress();
// Main thread only; waitForCompletion also drains deferred SRAM writes.
void sav3dsProcessSaveCompletion(bool waitForCompletion);

bool sav3dsSlotHasState(int slotNumber);
void sav3dsUpdateSlotState(int slotNumber);
void sav3dsSelectSlot(int direction);

void sav3dsGetSlotPath(int slotNumber, char* out, size_t bufferSize);
void sav3dsGetSRAMPath(char* out, size_t bufferSize);
// Reads the thumbnail block of a slot savestate into pixels (swizzled RGB565).
bool sav3dsReadThumbnail(const char* path, u16* pixels, size_t capacity, int* width, int* height);

#endif
