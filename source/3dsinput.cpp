
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
static bool frameAdvanceActive = false;
static bool frameAdvanceRequested = false;
// Scans since the press that advanced; -1 when no advancing press is held.
static int frameAdvanceHeldScans = -1;

// Units are input scans, one per vblank while waiting:
// repeat after ~330 ms, then at ~15 fps.
#define FRAME_ADVANCE_REPEAT_DELAY      20
#define FRAME_ADVANCE_REPEAT_INTERVAL   4

bool input3dsIsFrameAdvanceActive()
{
    return frameAdvanceActive;
}

bool input3dsShouldRunFrame()
{
    return !frameAdvanceActive || frameAdvanceRequested;
}

void input3dsResetFrameAdvance()
{
    frameAdvanceActive = false;
    frameAdvanceRequested = false;
    frameAdvanceHeldScans = -1;
}

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

static void input3dsRequestFrameAdvance(u32 keysDown, bool hotkeyHandled)
{
    // Game input is blanked while Hotkey Enable is held.
    if (hotkeyHandled || input3dsIsHotkeyEnableHeld()) {
        frameAdvanceHeldScans = -1;
        return;
    }

    u32 advanceKeys = ~KEY_TOUCH;
    // Hotkey buttons must not advance, so starting a hotkey never does.
    if (input3dsGetHotkey(HOTKEY_ENABLE).MappingBitmasks[0] == 0) {
        const auto& hotkeys = settings3DS.UseGlobalEmuControlKeys
            ? settings3DS.GlobalButtonHotkeys : settings3DS.ButtonHotkeys;
        for (const auto& hotkey : hotkeys)
            advanceKeys &= ~hotkey.MappingBitmasks[0];
    }

    if (keysDown & advanceKeys) {
        frameAdvanceRequested = true;
        frameAdvanceHeldScans = 0;
    } else if ((currKeysHeld & advanceKeys) && frameAdvanceHeldScans >= 0) {
        int repeat = ++frameAdvanceHeldScans - FRAME_ADVANCE_REPEAT_DELAY;
        frameAdvanceRequested = repeat >= 0 && repeat % FRAME_ADVANCE_REPEAT_INTERVAL == 0;
    } else {
        frameAdvanceHeldScans = -1;
    }
}

static void input3dsToggleFrameAdvance()
{
    frameAdvanceActive = !frameAdvanceActive;
    frameAdvanceHeldScans = -1;
    notif3dsTrigger(Notif::Misc, Notif::Type::Info, 1500,
        frameAdvanceActive ? "Frame advance on" : "Frame advance off");
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
    input3dsSetTurboMode(!frameAdvanceActive && (turboModeToggle || fastForwardHeld), isInGame);
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

u32 input3dsScanInputForEmulation()
{
    frameAdvanceRequested = false;
    hidScanInput();
    currKeysHeld = hidKeysHeld();

    if (ignoreInput) {
        if (currKeysHeld == 0) {
            ignoreInput = false;
        } else {
            // Wait for all keys to be released so a partial release cannot create new presses.
            currKeysHeld = 0;
        }
    }
    
    u32 keysDown = (~lastKeysHeld) & currKeysHeld;
    bool isInGame = session3DS.emulatorState == EMUSTATE_EMULATE;

    // Quick save takes priority over other hotkeys in this scan.
    bool skipHotkeys = sav3dsIsQuickSaveOrScreenshotInProgress();
    bool hotkeyHandled = false;
    if (isInGame && !skipHotkeys && input3dsIsHotkeyPressed(HOTKEY_QUICK_SAVE, keysDown))
    {
        sav3dsQuickSave();
        skipHotkeys = true;
        hotkeyHandled = true;
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

            skipHotkeys = true;
            hotkeyHandled = true;
            // The caller checks for a pending capture as soon as this scan returns.
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

        // Run only the highest-priority hotkey. Cleared by the final else when none fires.
        hotkeyHandled = true;
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
        else if (input3dsIsHotkeyPressed(HOTKEY_FRAME_ADVANCE_TOGGLE, keysDown))
            input3dsToggleFrameAdvance();
        else
            hotkeyHandled = false;
    }
    if (isInGame && frameAdvanceActive)
        input3dsRequestFrameAdvance(keysDown, hotkeyHandled);
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

u32 input3dsGetCurrentKeysHeld()
{
    if (input3dsIsHotkeyEnableHeld())
        return 0;
    return currKeysHeld;
}
