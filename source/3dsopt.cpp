#include "3dsopt.h"

#include <3ds.h>
#include <stdio.h>

// WYATT_TODO This should use a stack of some sort to allow for nesting

#ifndef RELEASE

#define WRAP_INCREMENT(v_, max_) (((((v_)+ 1) >= max_) ? 0 : ((v_) + 1))) // v_ % max_, but only once.
#define MIN(a_, b_) ((a_) < (b_) ? (a_) : (b_))
#define MAX(a_, b_) ((a_) > (b_) ? (a_) : (b_))
#define ARRAY_COUNT(arr_) (sizeof(arr_) / sizeof(arr_[0]))
#define DEFAULT_NAME "???"

T3DS_Thread t3dsMain, t3dsSnd;

T3DS_NameDef t3dsNamesMain[] = {T3DS_MAIN_THREAD(T3DS_CATEGORY)},
             t3dsNamesSnd[] = {T3DS_SND_THREAD(T3DS_CATEGORY)};

size_t t3dsNameCountMain = (ARRAY_COUNT(t3dsNamesMain)),
       t3dsNameCountSnd = (ARRAY_COUNT(t3dsNamesSnd));

void t3dsReset(T3DS_Thread* thread)
{
    *thread = (T3DS_Thread) {.nextFrame = 1, .name = DEFAULT_NAME};
}

void t3dsSetThreadName(T3DS_Thread* thread, char* name)
{
    if (name == NULL)
        name = DEFAULT_NAME;
    thread->name = name;
}

void t3dsAdvanceFrame(T3DS_Thread* thread)
{
    for (uint8_t id = 0; id <= thread->maxClock; id++) {
        T3DS_Clock* c = &thread->clocks[id];
        T3DS_ClockFrame* curFrame = &c->frames[thread->curFrame];
        T3DS_ClockFrame* nextFrame = &c->frames[thread->nextFrame];

        c->sum -= nextFrame->time;
        c->sum += curFrame->time;

        c->count -= nextFrame->count;
        c->count += curFrame->count;
    
        *nextFrame = (T3DS_ClockFrame) {0};
    }

    thread->curFrame = thread->nextFrame;
    thread->nextFrame = WRAP_INCREMENT(thread->nextFrame, T3DS_WINDOW);
}

static int t3dsCalculatePercentage(uint32_t time, uint32_t total)
{
    uint32_t percentage = time * 1000 / total; // Multiply overflows at ~4.29 seconds
    uint32_t remainder = percentage % 10;
    percentage /= 10;

    // Round with a precision of 1 decimal place
    if (remainder >= 5)
        percentage++;
    
    return percentage;
}

static void formatTime(char pBuf[6], uint32_t time)
{
    snprintf(pBuf, 6, "%f", ((float) time) * (1.0f / (1000.0f * T3DS_WINDOW)));
}

void t3dsPrint(T3DS_Thread* thread, T3DS_ClockType printFlags)
{
    uint32_t totalTime = 0;

    for (uint8_t id = 0; id <= thread->maxClock; id++)
        totalTime += thread->clocks[id].sum;
    
    if (totalTime == 0)
        totalTime = 1;

    char pBuf[6];
    formatTime(pBuf, totalTime);
    printf("%-20s:100%% %sms\n", thread->name, pBuf);

    for (uint8_t id = 0; id <= thread->maxClock; id++) {
        T3DS_Clock* c = &thread->clocks[id];

        if ((c->clockType & printFlags) == 0)
            continue;

        // Only print clocks with times > 0
        if (c->sum > 0)
        {
            formatTime(pBuf, c->sum);
            printf("%-20s:%3d%% %sms %lu\n", c->name, t3dsCalculatePercentage(c->sum, totalTime), pBuf, c->count);
        }

        // If our time is 0, treat it as a counter
        else if (c->count > 0)
        {
            printf("%-20s:        %lu\n", c->name, c->count);
        }
    }
}

void t3dsCount(T3DS_Thread* thread, uint8_t bucket)
{
    T3DS_Clock* c = &thread->clocks[bucket];
    c->frames[thread->curFrame].count++;
    c->clockType = T3DS_COUNTER;
    thread->maxClock = MAX(thread->maxClock, bucket);
}

void t3dsLog(T3DS_Thread* thread, uint8_t bucket)
{
    uint64_t system_tick = svcGetSystemTick();
    uint32_t elapsed = system_tick - thread->tickReference; // Will overflow at ~16 seconds
	thread->tickReference = system_tick;

    T3DS_Clock* c = &thread->clocks[bucket];
    c->frames[thread->curFrame].time += elapsed * (float) (1 / CPU_TICKS_PER_USEC);
    c->frames[thread->curFrame].count++;
    c->clockType = T3DS_CLOCK;
    thread->maxClock = MAX(thread->maxClock, bucket);
}

void t3dsSetClockNames(T3DS_Thread* thread, size_t numNames, T3DS_NameDef names[])
{
    for (size_t i = 0; i < numNames; i++)
    {
        T3DS_NameDef* n = &names[i];
        t3dsSetClockName(thread, n->bucket, n->name);
    }
}

void t3dsSetClockName(T3DS_Thread* thread, uint8_t bucket, char* name)
{
    if (name == NULL)
        name = DEFAULT_NAME;

    thread->clocks[bucket].name = name;
}

#endif // RELEASE
