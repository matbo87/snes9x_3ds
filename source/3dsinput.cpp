
#include <3ds.h>

#include "3dsimpl.h"
#include "3dsgpu.h"
#include "3dssettings.h"
#include "3dsinput.h"
#include "3dsui_notif.h"
#include "3dsutils.h"

static u32 currKeysHeld = 0;
static u32 lastKeysHeld = 0;
static bool ignoreInput = false;

#ifndef PROFILING_DISABLED
    static void input3dsToggleProfilingMode(bool cycleUp) {
        static const char *profilingModeNames[] = { "Profiling: Off ", "Profiling: Core", "Profiling: All " };
        if (cycleUp)
            GPU3DS.profilingMode = static_cast<SGPU_PROFILING_MODE>((GPU3DS.profilingMode + 1) % (PROFILING_ALL + 1));
        else
            GPU3DS.profilingMode = PROFILING_OFF;
            
        notif3dsTrigger(Notif::Misc, Notif::Type::Info, settings3DS.GameScreen, 1000, profilingModeNames[GPU3DS.profilingMode]);
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

    if (keysDown & KEY_TOUCH || 
        (!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_OPEN_MENU].IsHeld(keysDown)) ||
        (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_OPEN_MENU].IsHeld(keysDown))
        )
    {
        impl3dsTouchScreenPressed();

        if (GPU3DS.emulatorState == EMUSTATE_EMULATE)
            GPU3DS.emulatorState = EMUSTATE_PAUSEMENU;
    }
    
    if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
        #ifndef PROFILING_DISABLED
        if ((currKeysHeld & KEY_SELECT) && (currKeysHeld & KEY_L) && (currKeysHeld & KEY_RIGHT) && (keysDown & KEY_RIGHT)) {
            input3dsToggleProfilingMode(true);
        }
        if ((currKeysHeld & KEY_SELECT) && (currKeysHeld & KEY_L) && (currKeysHeld & KEY_LEFT) && (keysDown & KEY_LEFT)) { 
            input3dsToggleProfilingMode(false);
        }
        #endif

        if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_SWAP_CONTROLLERS].IsHeld(keysDown)) ||
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_SWAP_CONTROLLERS].IsHeld(keysDown)))
            impl3dsSwapJoypads();
            
        if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_QUICK_SAVE].IsHeld(keysDown)) || 
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_QUICK_SAVE].IsHeld(keysDown)))
            impl3dsQuickSaveLoad(true);

        if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_QUICK_LOAD].IsHeld(keysDown)) || 
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_QUICK_LOAD].IsHeld(keysDown)))
            impl3dsQuickSaveLoad(false);
        
        if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_SAVE_SLOT_NEXT].IsHeld(keysDown)) || 
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_SAVE_SLOT_NEXT].IsHeld(keysDown)))
            impl3dsSelectSaveSlot(1);

        if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_SAVE_SLOT_PREV].IsHeld(keysDown)) || 
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_SAVE_SLOT_PREV].IsHeld(keysDown)))
            impl3dsSelectSaveSlot(-1);
            
        if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_SCREENSHOT].IsHeld(keysDown)) || 
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_SCREENSHOT].IsHeld(keysDown))) {
            impl3dsPrepareScreenshot();
        }

        if ((!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_DISABLE_FRAMELIMIT].IsHeld(keysDown)) ||
            (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_DISABLE_FRAMELIMIT].IsHeld(keysDown))) {
    	        settings3DS.TurboMode = !settings3DS.TurboMode;

                if (settings3DS.TurboMode) {
                    notif3dsTrigger(Notif::FastForward, Notif::Type::Info, settings3DS.GameScreen);
                } else {
                    notif3dsTrigger(Notif::Misc, Notif::Type::Info, settings3DS.GameScreen, NOTIF_DEFAULT_DURATION, "Fast Forward disabled");
                }
            }
    }


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