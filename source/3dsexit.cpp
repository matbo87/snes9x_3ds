#include "3dsexit.h"
#include "3dsgpu.h"
#include "3dssettings.h"
#include "3dssound.h"
#include "memmap.h"
#include "snes9x.h"
#include "3dsimpl.h"

aptHookCookie hookCookie;
int appSuspended = 0;

void handleAptHook(APT_HookType hook, void* param)
{
    switch (hook) {
        case APTHOOK_ONEXIT:
            GPU3DS.emulatorState = EMUSTATE_END;
            break;
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            appSuspended = 1;
            if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
                snd3dsStopPlaying();
                if (settings3DS.ForceSRAMWriteOnPause || CPU.SRAMModified || CPU.AutoSaveTimer) {
                    S9xAutoSaveSRAM();
                }
            }
            break;
        case APTHOOK_ONRESTORE:
        case APTHOOK_ONWAKEUP:
            appSuspended = 1;
            break;
    }
}

void enableAptHooks() {
    aptHook(&hookCookie, handleAptHook, NULL);
}

void disableAptHooks() {
    aptUnhook(&hookCookie);
}
