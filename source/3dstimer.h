#ifndef _3DS_TIMER_H
#define _3DS_TIMER_H

#include <3ds.h>

// !!! always uncomment PROFILING_DISABLED before releasing to disable any profiling !!!
// for disabling/enabling specific profiling adjust `TimerConfigs`
#define PROFILING_DISABLED

typedef enum {
    TIMER_S9X_SUPER_FX,
    TIMER_S9X_MODE7_TEXTURE,
    TIMER_S9X_UPDATE_SCREEN,
    TIMER_S9X_MAIN_LOOP,
    TIMER_DRAW,
    TIMER_FLUSH,
    TIMER_RUN_ONE_FRAME,
    TIMER_COUNT // marker for the number of valid timers, do not use it as an actual timer index
} TimerBucket;

typedef struct {
    const char *name;
    bool isEnabled;
} TimerConfig;

typedef struct {
    const char *name;
    TickCounter timer;
    int calls;
    double totalMs;
} T3dsTimer;

extern TimerConfig TimerConfigs[TIMER_COUNT];
extern T3dsTimer t3dsTimers[TIMER_COUNT];


static inline void t3dsStartTimer(TimerBucket bucket) {
    #ifndef PROFILING_DISABLED
        if (bucket == TIMER_COUNT || !TimerConfigs[bucket].isEnabled) return;

        osTickCounterStart(&t3dsTimers[bucket].timer);
    #endif
}

static inline void t3dsStopTimer(TimerBucket bucket) {
    #ifndef PROFILING_DISABLED
        if (bucket == TIMER_COUNT || !TimerConfigs[bucket].isEnabled) return;

        osTickCounterUpdate(&t3dsTimers[bucket].timer);
        t3dsTimers[bucket].totalMs += osTickCounterRead(&t3dsTimers[bucket].timer);
        t3dsTimers[bucket].calls++;
    #endif
}

void t3dsResetTimers(bool _fpsOnly = false);
void t3dsPrintTimer(TimerBucket bucket);
void t3dsPrintAllTimers(int totalFrames);

#endif
