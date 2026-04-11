
#include "memmap.h"
#include "3dsgpu.h"
#include "3dslcd.h"
#include "3dssound.h"
#include "3dsinput.h"
#include "3dsmenu.h"
#include "3dsexit.h"

aptHookCookie hookCookie;

void handleAptHook(APT_HookType hook, void* param)
{
    switch (hook) {
        case APTHOOK_ONEXIT:
            lcd3dsRestoreDefaultRate();
            GPU3DS.emulatorState = EMUSTATE_END;
            break;
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
                snd3dsStopPlaying(); // avoid hanging looped sample while HOME menu is open
                lcd3dsRestoreDefaultRate();
                if (settings3DS.ForceSRAMWriteOnPause || CPU.SRAMModified || CPU.AutoSaveTimer) {
                    S9xAutoSaveSRAM();
                }
                
                GPU3DS.emulatorState = EMUSTATE_PAUSEMENU;
                input3dsRefreshTurboMode(false);
            }

            break;
        case APTHOOK_ONRESTORE:
            // Render both buffers on resume to ensure correct display.
            GPU3DS.doubleBufferDesync = true;
            menu3dsSetScreenDirty(true, true);
            break;
        case APTHOOK_ONWAKEUP:
            break;
        default:
            break;
    }
}

void enableAptHooks() {
    aptHook(&hookCookie, handleAptHook, NULL);
}

void disableAptHooks() {
    aptUnhook(&hookCookie);
}
