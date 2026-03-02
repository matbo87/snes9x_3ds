#include "3dsexit.h"
#include "3dsgpu.h"

aptHookCookie hookCookie;

void handleAptHook(APT_HookType hook, void* param)
{
    switch (hook) {
        case APTHOOK_ONEXIT:
            GPU3DS.emulatorState = EMUSTATE_END;
            break;
        case APTHOOK_ONSUSPEND:
        case APTHOOK_ONSLEEP:
            GPU3DS.emulatorState = EMUSTATE_PAUSEMENU;
            break;
    }
}

void enableAptHooks() {
    aptHook(&hookCookie, handleAptHook, NULL);
}

void disableAptHooks() {
    aptUnhook(&hookCookie);
}
