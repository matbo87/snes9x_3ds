#ifndef _3DS_TIMER_H
#define _3DS_TIMER_H

#include <3ds.h>

// !!! always uncomment PROFILING_DISABLED before releasing to disable any profiling !!!
// for disabling/enabling specific profiling adjust `TimerConfigs`
#define PROFILING_DISABLED

typedef enum {
    TIMER_S9X_MAIN_LOOP,
    TIMER_S9X_SUPER_FX,
    TIMER_S9X_UPDATE_SCREEN,
    TIMER_DRAW,
    TIMER_DRAW_M7_TEXTURE,
    TIMER_DRAW_SNES_SCREEN,
    TIMER_DRAW_SCENE,
    TIMER_GPU_WAIT,
    TIMER_FLUSH,
    TIMER_RUN_ONE_FRAME,
    TIMER_COUNT
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
    bool isEnabled;
} T3dsTimer;

extern TimerConfig TimerConfigs[TIMER_COUNT];
extern T3dsTimer t3dsTimers[TIMER_COUNT];


static inline void t3dsStartTimer(TimerBucket bucket) {
    #ifndef PROFILING_DISABLED
        if (bucket == TIMER_COUNT || !t3dsTimers[bucket].isEnabled) return;

        osTickCounterStart(&t3dsTimers[bucket].timer);
    #endif
}

static inline void t3dsStopTimer(TimerBucket bucket) {
    #ifndef PROFILING_DISABLED
        if (bucket == TIMER_COUNT || !t3dsTimers[bucket].isEnabled) return;

        osTickCounterUpdate(&t3dsTimers[bucket].timer);
        t3dsTimers[bucket].totalMs += osTickCounterRead(&t3dsTimers[bucket].timer);
        t3dsTimers[bucket].calls++;
    #endif
}

void t3dsResetTimers();
void t3dsPrintTimer(TimerBucket bucket, int totalFrames = 1);
void t3dsPrintTimers(int totalFrames);

#endif
