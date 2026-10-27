
#include <3ds.h>

#include "3dsimpl.h"
#include "3dssave.h"
#include "3dstimer.h"
#include "3dssettings.h"
#include "3dsinput.h"
#include "3dsui_notif.h"
#include "3dsutils.h"
#include "memmap.h"
#include "3dssound.h"

static u32 currKeysHeld = 0;
static u32 lastKeysHeld = 0;
static bool ignoreInput = false;
static bool turboModeToggle = false;
static bool menuOpenPending = false;

static bool input3dsIsFastForwardHoldPressed()
{
    return (!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_FAST_FORWARD_HOLD].IsHeld(currKeysHeld)) ||
           (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_FAST_FORWARD_HOLD].IsHeld(currKeysHeld));
}

static void input3dsSetTurboMode(bool turboModeActive, bool showNotification)
{
    if (session3DS.turboMode == turboModeActive) {
        return;
    }

    session3DS.turboMode = turboModeActive;
    if (!showNotification) {
        return;
    }

    notif3dsTrigger(Notif::FastForward, Notif::Type::Info);
}

void input3dsRefreshTurboMode(bool isInGame)
{
    bool fastForwardHeld = isInGame && input3dsIsFastForwardHoldPressed();
    input3dsSetTurboMode(turboModeToggle || fastForwardHeld, isInGame);
}

#ifndef PROFILING_DISABLED
    static void input3dsToggleProfilingMode(bool cycleUp) {
        static const char *profilingModeNames[] = { "Profiling: Off ", "Profiling: Core", "Profiling: All " };
        if (cycleUp)
            t3dsProfilingMode = static_cast<ProfilingMode>((t3dsProfilingMode + 1) % (PROFILING_ALL + 1));
        else
            t3dsProfilingMode = PROFILING_OFF;
            
        notif3dsTrigger(Notif::Misc, Notif::Type::Info, 1000, profilingModeNames[t3dsProfilingMode]);
    }
#endif

//---------------------------------------------------------
// Reads and processes Joy Pad buttons.
//
// This should be called only once every frame only in the
// emulator loop. For all other purposes, you should
// use the standard hidScanInput.
//---------------------------------------------------------
u32 input3dsScanInputForEmulation()
{
    hidScanInput();
    currKeysHeld = hidKeysHeld();
    u32 currentKeysUp = hidKeysUp();

    if (ignoreInput) {
        if (currentKeysUp != 0 || currKeysHeld == 0) {
            ignoreInput = false;
        } else {
            // no keys are pressed
            currKeysHeld = 0;
        }
    }
    
    u32 keysDown = (~lastKeysHeld) & currKeysHeld;
    bool isInGame = session3DS.emulatorState == EMUSTATE_EMULATE;

    // Quick save takes priority over every other hotkey in this scan.
    // Keep blocking hotkeys while the save is running.
    bool skipHotkeys = sav3dsIsQuickSaveInProgress();
    if (isInGame && !skipHotkeys &&
        ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_QUICK_SAVE].IsHeld(keysDown)) ||
         (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_QUICK_SAVE].IsHeld(keysDown))))
    {
        sav3dsQuickSave();
        skipHotkeys = true;
    }

    if (keysDown & KEY_TOUCH ||
        (!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_OPEN_MENU].IsHeld(keysDown)) ||
        (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_OPEN_MENU].IsHeld(keysDown))
        )
    {
        menuOpenPending = true;
    }

    // A quick save in progress holds the menu request until it completes.
    if (menuOpenPending && !sav3dsIsQuickSaveInProgress())
    {
        menuOpenPending = false;
        if (isInGame)
        {
            if (settings3DS.ForceSRAMWriteOnPause)
                CPU.SRAMModified = TRUE;
            if (CPU.SRAMModified)
                sav3dsRequestSRAMSave();
            session3DS.emulatorState = EMUSTATE_PAUSEMENU;
            snd3dsDrainMixing();
            notif3dsHide();
        }
    }
    
    if (isInGame && !skipHotkeys) {
        #ifndef PROFILING_DISABLED
        if ((currKeysHeld & KEY_SELECT) && (currKeysHeld & KEY_L) && (currKeysHeld & KEY_RIGHT) && (keysDown & KEY_RIGHT)) {
            input3dsToggleProfilingMode(true);
        }
        if ((currKeysHeld & KEY_SELECT) && (currKeysHeld & KEY_L) && (currKeysHeld & KEY_LEFT) && (keysDown & KEY_LEFT)) { 
            input3dsToggleProfilingMode(false);
        }
        #endif

        // One hotkey action per scan, in priority order; quick save is checked above.
        if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_QUICK_LOAD].IsHeld(keysDown)) ||
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_QUICK_LOAD].IsHeld(keysDown)))
            sav3dsQuickLoad();
        else if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_SAVE_SLOT_NEXT].IsHeld(keysDown)) ||
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_SAVE_SLOT_NEXT].IsHeld(keysDown)))
            sav3dsSelectSlot(1);
        else if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_SAVE_SLOT_PREV].IsHeld(keysDown)) ||
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_SAVE_SLOT_PREV].IsHeld(keysDown)))
            sav3dsSelectSlot(-1);
        else if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_SCREENSHOT].IsHeld(keysDown)) ||
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_SCREENSHOT].IsHeld(keysDown)))
            impl3dsPrepareScreenshot();
        else if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_SWAP_CONTROLLERS].IsHeld(keysDown)) ||
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_SWAP_CONTROLLERS].IsHeld(keysDown)))
            impl3dsSwapJoypads();
        else if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_FAST_FORWARD_TOGGLE].IsHeld(keysDown)) ||
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_FAST_FORWARD_TOGGLE].IsHeld(keysDown)))
            turboModeToggle = !turboModeToggle;
    }
    if (!skipHotkeys)
        input3dsRefreshTurboMode(isInGame);


    lastKeysHeld = currKeysHeld;
    return keysDown;

}

void input3dsWaitForRelease()
{
    ignoreInput = true;
}

//---------------------------------------------------------
// Get the bitmap of keys currently held on by the user
//---------------------------------------------------------
u32 input3dsGetCurrentKeysHeld()
{
    return currKeysHeld;
}
