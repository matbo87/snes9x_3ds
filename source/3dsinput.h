
#ifndef _3DSINPUT_H
#define _3DSINPUT_H

//---------------------------------------------------------
// Reads and processes Joy Pad buttons.
//
// Call from the emulator loop, including while waiting to advance a frame.
// For all other purposes, use hidScanInput.
//---------------------------------------------------------
u32 input3dsScanInputForEmulation();

//---------------------------------------------------------
// Get the bitmap of keys currently held on by the user.
// Empty while Hotkey Enable is held, so the game ignores hotkey presses.
//---------------------------------------------------------
u32 input3dsGetCurrentKeysHeld();

// Frame advance stays on across menu visits, but not ROM loads.
bool input3dsIsFrameAdvanceActive();
bool input3dsShouldRunFrame();
void input3dsResetFrameAdvance();

// Recomputes TurboMode from toggle + hold state.
// When game is not running, hold is ignored and notifications are suppressed.
void input3dsRefreshTurboMode(bool isInGame);

// Call when resuming from the menu: ignores the button used to exit it
// and drops a menu request left over from before.
void input3dsWaitForRelease();

#endif
