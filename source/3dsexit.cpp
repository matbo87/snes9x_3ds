#include <stdlib.h>
#include <3ds.h>

#include "3dsexit.h"
#include "3dsgpu.h"
#include "3dssettings.h"
#include "3dssound.h"
#include "memmap.h"
#include "snes9x.h"
#include "3dsimpl.h"

aptHookCookie hookCookie;
int appExiting = 0;
int appSuspended = 0;

extern S9xSettings3DS settings3DS;

void handleAptHook(APT_HookType hook, void* param)
{
    switch (hook) {
        case APTHOOK_ONEXIT:
            // Let's turn on the bottom screen just in case it's turned off
            turn_bottom_screen(TURN_ON);
            appExiting = 1;
            break;
        case APTHOOK_ONSUSPEND:
            // Let's turn on the bottom screen just in case it's turned off
            turn_bottom_screen(TURN_ON);
            appSuspended = 1;
            if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
                snd3dsStopPlaying();
            }
            break;        
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
            if (bottom_screen_buffer == NULL && GPU3DS.emulatorState == EMUSTATE_EMULATE)
            {
                // There's no bottom screen image AND the menu is closed, let's turn off the bottom screen
                turn_bottom_screen(TURN_OFF);
            }
            else
            {
                turn_bottom_screen(TURN_ON);
            }        
            appSuspended = 1;
            break;
    }
}

void enableAptHooks() {
    aptHook(&hookCookie, handleAptHook, NULL);
}
