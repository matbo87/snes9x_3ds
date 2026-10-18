
#include "memmap.h"
#include "3dsgpu.h"
#include "3dslcd.h"
#include "3dssound.h"
#include "3dsinput.h"
#include "3dsmenu.h"
#include "3dsexit.h"
#include "3dsutils.h"

aptHookCookie hookCookie;

void handleAptHook(APT_HookType hook, void* param)
{
    switch (hook) {
        case APTHOOK_ONEXIT:
            lcd3dsRestoreDefaultRate();
            session3DS.emulatorState = EMUSTATE_END;
            break;
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            snd3dsRestoreCpuLimit();
            if (session3DS.emulatorState == EMUSTATE_EMULATE) {
                snd3dsStopPlaying(); // avoid hanging looped sample while HOME menu is open
                lcd3dsRestoreDefaultRate();
                if (settings3DS.ForceSRAMWriteOnPause || CPU.SRAMModified || CPU.AutoSaveTimer) {
                    S9xAutoSaveSRAM();
                }

                session3DS.emulatorState = EMUSTATE_PAUSEMENU;
                input3dsRefreshTurboMode(false);
            }

            break;
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONWAKEUP:
            snd3dsApplyCpuLimit();
            GPU3DS.gameScreenBufferDesync = true;
            menu3dsSetScreenDirty(true, true);
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
