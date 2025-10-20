
#include <3ds.h>
#include "3dsimpl.h"
#include "3dsgpu.h"
#include "3dssettings.h"
#include "3dstimer.h"

static u32 currKeysHeld = 0;
static u32 lastKeysHeld = 0;

//int adjustableValue = 0x70;

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

        // toggle debug performance mode
        #ifndef PROFILING_DISABLED

            if ((lastKeysHeld & KEY_DOWN) && (keysDown & KEY_R)) {
                GPU3DS.profilingMode = (SGPU_PROFILING_MODE)((GPU3DS.profilingMode + 1) % 3);
            }

            if ((lastKeysHeld & KEY_DOWN) && (keysDown & KEY_L)) {
                GPU3DS.emulatorState = EMUSTATE_END;
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
    }


    lastKeysHeld = currKeysHeld;
    return keysDown;

}


//---------------------------------------------------------
// Get the bitmap of keys currently held on by the user
//---------------------------------------------------------
u32 input3dsGetCurrentKeysHeld()
{
    return currKeysHeld;
}