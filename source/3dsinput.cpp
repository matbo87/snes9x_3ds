
#include <3ds.h>

#include "3dsimpl.h"
#include "3dssave.h"
#include "3dstimer.h"
#include "3dssettings.h"
#include "3dsinput.h"
#include "3dsui_notif.h"
#include "3dsutils.h"
#include "memmap.h"

static u32 currKeysHeld = 0;
static u32 lastKeysHeld = 0;
static bool ignoreInput = false;
static bool turboModeToggle = false;
static bool menuOpenPending = false;

static const ::ButtonMapping<1>& input3dsGetHotkey(int hotkey)
{
    return settings3DS.UseGlobalEmuControlKeys ? settings3DS.GlobalButtonHotkeys[hotkey] : settings3DS.ButtonHotkeys[hotkey];
}

static bool input3dsIsHotkeyEnableHeld()
{
    return input3dsGetHotkey(HOTKEY_ENABLE).IsHeld(currKeysHeld);
}

// Without a Hotkey Enable mapping, hotkeys always work.
static bool input3dsIsHotkeyPressed(int hotkey, u32 keys)
{
    if (input3dsGetHotkey(HOTKEY_ENABLE).MappingBitmasks[0] != 0 && !input3dsIsHotkeyEnableHeld())
        return false;
    return input3dsGetHotkey(hotkey).IsHeld(keys);
}

static bool input3dsIsFastForwardHoldPressed()
{
    return input3dsIsHotkeyPressed(HOTKEY_FAST_FORWARD_HOLD, currKeysHeld);
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

    // Quick save takes priority over other hotkeys in this scan.
    bool skipHotkeys = sav3dsIsQuickSaveOrScreenshotInProgress();
    if (isInGame && !skipHotkeys && input3dsIsHotkeyPressed(HOTKEY_QUICK_SAVE, keysDown))
    {
        sav3dsQuickSave();
        skipHotkeys = true;
    }

    if (keysDown & KEY_TOUCH || input3dsIsHotkeyPressed(HOTKEY_OPEN_MENU, keysDown))
    {
        menuOpenPending = true;
    }

    if (menuOpenPending && !sav3dsIsQuickSaveOrScreenshotInProgress())
    {
        menuOpenPending = false;
        if (isInGame)
        {
            if (sav3dsSRAMNeedsPauseSave()) {
                CPU.SRAMModified = TRUE;
                sav3dsRequestSRAMSave();
            }
            session3DS.emulatorState = EMUSTATE_PAUSEMENU;
            notif3dsHide();

            // Opening the menu takes the scan. Discard captures that would
            // otherwise run after leaving the menu.
            skipHotkeys = true;
            screenshot.pending = false;
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
        if (input3dsIsHotkeyPressed(HOTKEY_QUICK_LOAD, keysDown))
            sav3dsQuickLoad();
        else if (input3dsIsHotkeyPressed(HOTKEY_SAVE_SLOT_NEXT, keysDown))
            sav3dsSelectSlot(1);
        else if (input3dsIsHotkeyPressed(HOTKEY_SAVE_SLOT_PREV, keysDown))
            sav3dsSelectSlot(-1);
        else if (input3dsIsHotkeyPressed(HOTKEY_SCREENSHOT, keysDown))
            impl3dsPrepareScreenshot();
        else if (input3dsIsHotkeyPressed(HOTKEY_SWAP_CONTROLLERS, keysDown))
            impl3dsSwapJoypads();
        else if (input3dsIsHotkeyPressed(HOTKEY_FAST_FORWARD_TOGGLE, keysDown))
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
    // Suspend may bypass a deferred menu request.
    menuOpenPending = false;
}

//---------------------------------------------------------
// Get the bitmap of keys currently held on by the user.
// Empty while Hotkey Enable is held, so the game ignores hotkey presses.
//---------------------------------------------------------
u32 input3dsGetCurrentKeysHeld()
{
    if (input3dsIsHotkeyEnableHeld())
        return 0;
    return currKeysHeld;
}
