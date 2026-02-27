#include "3dstimer.h"
#include <stdio.h>
#include <string.h>

T3dsTimer t3dsTimers[TIMER_COUNT];
static bool fpsOnly;

TimerConfig TimerConfigs[TIMER_COUNT] = {
    [TIMER_S9X_MAIN_LOOP]       = { "S9xMainLoop     ", true },
    [TIMER_S9X_SUPER_FX]        = { "S9xSuperFX      ", false },
    [TIMER_S9X_UPDATE_SCREEN]   = { "S9xUpdateScreen ", false },
    [TIMER_DRAW]                = { "gpu3dsDraw      ", false },
    [TIMER_DRAW_M7_TEXTURE]     = { "draw M7 texture ", false },
    [TIMER_DRAW_SNES_SCREEN]    = { "draw SNES screen", true },
    [TIMER_DRAW_SCENE]          = { "draw scene      ", true },
    [TIMER_GPU_WAIT]            = { "gpuWait         ", true },
    [TIMER_FLUSH]               = { "flush           ", true },
    [TIMER_RUN_ONE_FRAME]       = { "runOneFrame     ", true }
};

void t3dsResetTimers(bool _fpsOnly) {
    #ifndef PROFILING_DISABLED

        int start = 0; 
        int end = TIMER_COUNT;

        fpsOnly = _fpsOnly;

        if (fpsOnly) {
            start = TIMER_RUN_ONE_FRAME;
            end = TIMER_RUN_ONE_FRAME + 1;
        }

        for (int i = start; i < end; ++i) {
            t3dsTimers[i].calls = 0;
            t3dsTimers[i].totalMs = 0.0;
            t3dsTimers[i].name = TimerConfigs[i].name;
            memset(&t3dsTimers[i].timer, 0, sizeof(TickCounter));
        }
    #endif
}

void t3dsPrintTimer(TimerBucket bucket) {
    #ifndef PROFILING_DISABLED
        if (bucket == TIMER_COUNT || t3dsTimers[bucket].calls == 0) return;

        if (bucket == TIMER_RUN_ONE_FRAME) {
            double avg = t3dsTimers[bucket].totalMs / t3dsTimers[bucket].calls;

            if (fpsOnly) {
                printf("\x1b[1;1H"); // always top line
            }

            printf("%s: %.3ffps ms:%.3f\n",
                t3dsTimers[bucket].name,
                1.0f / avg * 1000.0f,
                avg);
        } else {
            printf("%s: count:%d ms:%.3f\n",
                t3dsTimers[bucket].name,
                t3dsTimers[bucket].calls,
                t3dsTimers[bucket].totalMs);
        }
    #endif
}

void t3dsPrintAllTimers(int totalFrames) {
    #ifndef PROFILING_DISABLED
        for (int i = 0; i < TIMER_COUNT; ++i) {
            if (t3dsTimers[i].calls) {
                t3dsPrintTimer((TimerBucket)i);
            }
        }

        printf("--- %d\n", totalFrames);
    #endif
}