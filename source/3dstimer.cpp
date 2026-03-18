#include <stdio.h>
#include <string.h>

#include "3dslog.h"
#include "3dsgpu.h"
#include "3dstimer.h"

T3dsTimer t3dsTimers[TIMER_COUNT];

TimerConfig TimerConfigs[TIMER_COUNT] = {
    [TIMER_S9X_MAIN_LOOP]       = { "S9xMainLoop     ", true },
    [TIMER_S9X_SUPER_FX]        = { "S9xSuperFX      ", false },
    [TIMER_S9X_UPDATE_SCREEN]   = { "S9xUpdateScreen ", false },
    [TIMER_DRAW]                = { "gpu3dsDraw      ", false },
    [TIMER_DRAW_M7_TEXTURE]     = { "draw M7 texture ", false },
    [TIMER_DRAW_SNES_SCREEN]    = { "draw SNES screen", true },
    [TIMER_DRAW_SCENE]          = { "draw scene      ", false },
    [TIMER_GPU_WAIT]            = { "gpuWait         ", false },
    [TIMER_FLUSH]               = { "flush           ", false },
    [TIMER_RUN_ONE_FRAME]       = { "runOneFrame     ", true }
};

static const TimerBucket coreTimers[] = {
    TIMER_S9X_MAIN_LOOP,
    TIMER_S9X_SUPER_FX,
    TIMER_S9X_UPDATE_SCREEN,
};

void t3dsResetTimers() {
    #ifndef PROFILING_DISABLED
        for (int i = 0; i < TIMER_COUNT; ++i) {
            t3dsTimers[i].calls = 0;
            t3dsTimers[i].totalMs = 0.0;
            t3dsTimers[i].name = TimerConfigs[i].name;
            t3dsTimers[i].isEnabled = false;
            memset(&t3dsTimers[i].timer, 0, sizeof(TickCounter));
        }

        switch (GPU3DS.profilingMode) {
            case PROFILING_ALL:
                for (int i = 0; i < TIMER_COUNT; ++i)
                    t3dsTimers[i].isEnabled = TimerConfigs[i].isEnabled;
                break;
            case PROFILING_CORE:
                for (int i = 0; i < (int)(sizeof(coreTimers) / sizeof(coreTimers[0])); ++i)
                    t3dsTimers[coreTimers[i]].isEnabled = TimerConfigs[coreTimers[i]].isEnabled;
                break;
            default: break;
        }
    #endif
}

void t3dsPrintTimer(TimerBucket bucket, int totalFrames) {
    #ifndef PROFILING_DISABLED
        if (bucket >= TIMER_COUNT || !t3dsTimers[bucket].isEnabled) return;

        char logBuffer[256];

        if (t3dsTimers[bucket].calls == 0)
        {
            snprintf(logBuffer, sizeof(logBuffer), "%s: avg:n/a calls:0 ", t3dsTimers[bucket].name);
        }
        else
        {
            if (totalFrames <= 0) totalFrames = 1;

            double avg = t3dsTimers[bucket].totalMs / t3dsTimers[bucket].calls;
            double msPerFrame = t3dsTimers[bucket].totalMs / totalFrames;
            bool highFreq = t3dsTimers[bucket].calls > totalFrames * 2;

            if (bucket == TIMER_RUN_ONE_FRAME) {
                // paceFrame() is called after t3dsStopTimer(TIMER_RUN_ONE_FRAME)
                // so TIMER_RUN_ONE_FRAME represents the max potential fps here
                snprintf(logBuffer, sizeof(logBuffer),
                    "%s: %.2fmaxfps, %.3fms",
                    t3dsTimers[bucket].name, 1000.0 / avg, avg);
            } else if (highFreq) {
                snprintf(logBuffer, sizeof(logBuffer),
                    "%s: ms/f:%.2f  calls:%d",
                    t3dsTimers[bucket].name, msPerFrame, t3dsTimers[bucket].calls);
            } else {
                snprintf(logBuffer, sizeof(logBuffer),
                    "%s: avg:%.3fms  calls:%d",
                    t3dsTimers[bucket].name, avg, t3dsTimers[bucket].calls);
            }
        }

        if (writeToLogFile)
            log3dsWrite("%s", logBuffer);
        else
            printf("%s\n", logBuffer);
    #endif
}

void t3dsPrintTimers(int totalFrames) {
    #ifndef PROFILING_DISABLED
        for (int i = 0; i < TIMER_COUNT; ++i) {
            t3dsPrintTimer((TimerBucket)i, totalFrames);
        }
    #endif
}
