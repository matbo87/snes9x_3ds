
#ifndef _3DSINPUT_H
#define _3DSINPUT_H

//---------------------------------------------------------
// Reads and processes Joy Pad buttons.
//
// This should be called only once every frame only in the
// emulator loop. For all other purposes, you should
// use the standard hidScanInput.
//---------------------------------------------------------
u32 input3dsScanInputForEmulation();

//---------------------------------------------------------
// Get the bitmap of keys currently held on by the user
//---------------------------------------------------------
u32 input3dsGetCurrentKeysHeld();

// Recomputes TurboMode from toggle + hold state.
// When game is not running, hold is ignored and notifications are suppressed.
void input3dsRefreshTurboMode(bool isInGame);

// Call when resuming from the menu: ignores the button used to exit it
// and drops a menu request left over from before.
void input3dsWaitForRelease();

#endif
