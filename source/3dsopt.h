#ifndef _3DSOPT_H_
#define _3DSOPT_H_

#include <stdint.h>
#include <stdbool.h>
#include <3ds.h>

/*        T3DS Timer
 * An end-to-end timing system with rudimentary multi-threading support.
 *
 * At thread start, you should:
 * - Call t3dsReset. This is mandatory
 * - Set your thread name with t3dsSetThreadName 
 * - Set your clock and counter names
 * 
 * For each frame, you should:
 * - Call t3dsLog for your MISC category
 * - Call t3dsAdvanceFrame
 * - Run your logic
 * - Call t3dsPrintClocks and t3dsPrintCounters to print to stdout
 * 
 * To use the timers:
 * - Call t3dsLog for your MISC category
 * - Run the code you wish to profile
 * - Call t3dsLog for whichever category you wish to log
 * 
 * t3dsLog essentially assigns all time between the prior t3dsLog
 * call and the current one to the given category.
 * For example:
 * 
 * ...          // Time before is category 1
 * t3dsLog(1);
 * someCode();  // Category 2
 * t3dsLog(2);
 * someCode2(); // Category 3
 * t3dsLog(3);
 * 
 * You can (and should) chain non-misc categories if you know
 * what was called before. Be careful not to nest categories
 * unless you really know what you're doing.
 * 
 * To use the counters, simply call t3dsCount at any time.
 * 
 * Notes:
 * - If any total goes above ~4.2 seconds, including the grand total,
 *    percentage calculation will overflow. If that's a problem,
 *    replace it with a u64 to get up to ~4200 seconds, or remove
 *    rounding to get up to ~42 seconds.
 * - If a t3dsLog call's elapsed time exceeds ~16 seconds, its elapsed
 *    time will overflow. If that's a problem, replace it with a u64.
 * - Multithreading is not synchronized, but it's probably good enough
 *    for your usage. Calling print on any thread will never corrupt
 *    any other thread, but could print incorrect timings.
 * - Time precision is 1 microsecond, truncated.
 * - Percentages are rounded to nearest.
 */

// See the bottom of this file for currently used buckets and names.

#define T3DS_STACK_SIZE 4
#define T3DS_NUM_CLOCKS 100
#define T3DS_WINDOW 61 /* +1 because printing does not include the current frame */

typedef struct
{
    uint32_t time; // Total time this clock has logged, in microseconds
    uint16_t count; // Number of times this clock has been logged
    uint16_t pad;
} T3DS_ClockFrame;

typedef struct
{
    uint8_t clockType;            // T3DS_ClockType
    char *name;                   // Printed name
    uint32_t count;               // Sum of counts.
    uint32_t sum;                 // Sum of times, in microseconds
    T3DS_ClockFrame frames[T3DS_WINDOW];
} T3DS_Clock;

typedef struct
{
    uint8_t curFrame, nextFrame;        // Internal use
    uint8_t maxClock;                   // Internal use
    char* name;                         // Thread name
    uint64_t tickReference;             // The currently logged time
    T3DS_Clock clocks[T3DS_NUM_CLOCKS]; // Clocks
} T3DS_Thread;

typedef struct
{
    uint8_t bucket;
    char* name;
} T3DS_NameDef;

typedef enum
{
    T3DS_CLOCK   = 1 << 0,
    T3DS_COUNTER = 1 << 1,
    T3DS_BOTH    = T3DS_CLOCK | T3DS_COUNTER,
} T3DS_ClockType;

#ifndef RELEASE

extern T3DS_Thread t3dsMain, t3dsSnd;
extern T3DS_NameDef t3dsNamesMain[], t3dsNamesSnd[];
extern size_t t3dsNameCountMain, t3dsNameCountSnd;

void t3dsReset(T3DS_Thread* thread);
void t3dsSetThreadName(T3DS_Thread* thread, char* name);
void t3dsSetClockNames(T3DS_Thread* thread, size_t numNames, T3DS_NameDef names[]);
void t3dsSetClockName(T3DS_Thread* thread, uint8_t bucket, char* name);
void t3dsAdvanceFrame(T3DS_Thread* thread);
void t3dsPrint(T3DS_Thread* thread, T3DS_ClockType printFlags);
void t3dsCount(T3DS_Thread* thread, uint8_t bucket); // Increment a category's counter
void t3dsLog(T3DS_Thread* thread, uint8_t bucket); // Update a category's timer

#else // RELEASE
#define t3dsReset(thread)                             do {} while(0) // Stub
#define t3dsSetThreadName(thread, name)               do {} while(0) // Stub
#define t3dsSetClockNames(thread, numNames, names)    do {} while(0) // Stub
#define t3dsSetClockName(thread, bucket, name)        do {} while(0) // Stub
#define t3dsAdvanceFrame(thread)                      do {} while(0) // Stub
#define t3dsPrint(thread, printClocks, printCounters) do {} while(0) // Stub
#define t3dsCount(thread, bucket)                     do {} while(0) // Stub
#define t3dsLog(thread, bucket)                       do {} while(0) // Stub
#endif // RELEASE

#define T3DS_ENUM(id_, enum_, str_) enum_ = id_                     /* Higher-order macro to generate an enum */
#define T3DS_CATEGORY(id_, enum_, str_) (T3DS_NameDef) {id_, str_} /* Higher-order macro to generate a T3DS_NameDef */

// Main thread clock names and IDs
#define T3DS_MAIN_THREAD(c_)                                      \
    c_(  0, Snx_Misc               , "Misc"                   ),  \
    c_(  1, Snx_Vsync              , "Vsync"                  ),  \
    c_(  2, Snx_APT                , "APT"                    ),  \
    c_(  3, Snx_SuperFX            , "SuperFX"                ),  \
    c_(  4, Snx_CopyFB             , "CopyFB"                 ),  \
    c_(  5, Snx_Flush              , "Flush"                  ),  \
    c_(  6, Snx_Transfer           , "Transfer"               ),  \
    c_(  7, Snx_EmulatorTasks      , "EmulatorTasks"          ),  \
    c_( 11, Snx_UpdateScreen       , "S9xUpdateScreen"        ),  \
    c_( 21, Snx_DrawBG0            , "DrawBG0"                ),  \
    c_( 22, Snx_DrawBG1            , "DrawBG1"                ),  \
    c_( 23, Snx_DrawBG2            , "DrawBG2"                ),  \
    c_( 24, Snx_DrawBG3            , "DrawBG3"                ),  \
    c_( 25, Snx_DrawBKClr          , "DrawBKClr"              ),  \
    c_( 26, Snx_DrawOBJS           , "DrawOBJS"               ),  \
    c_( 27, Snx_DrawBG0_M7         , "DrawBG0_M7"             ),  \
    c_( 28, Snx_SetupOBJ           , "S9xSetupOBJ"            ),  \
    c_( 29, Snx_Colormath          , "Colormath"              ),  \
    c_( 30, Snx_DrawWindowStencils , "DrawWindowStencils"     ),  \
    c_( 31, Snx_RenderScnHW        , "RenderScnHW"            ),  \
    c_( 32, Snx_PrepM7_Cleanup     , "PrepM7_Cleanup"         ),  \
    c_( 33, Snx_PrepM7_Palette     , "PrepM7_Palette"         ),  \
    c_( 34, Snx_PrepM7_FullTile    , "PrepM7_FullTile"        ),  \
    c_( 35, Snx_PrepM7_CharFlag    , "PrepM7_CharFlag"        ),  \

// Audio thread clock names and IDs
#define T3DS_SND_THREAD(c_)                                       \
    c_(  0, Mix_Misc               ,  "Misc"                  ),  \
    c_(  1, Mix_Sleep              ,  "Sleep"                 ),  \
    c_(  2, Mix_Replay_S9xMix      ,  "Replay_S9xMix"         ),  \
    c_(  3, Mix_Timing             ,  "Timing"                ),  \
    c_(  4, Mix_ApplyMstVol        ,  "ApplyMstVol"           ),  \
    c_(  5, Mix_Flush              ,  "Flush"                 ),  \


typedef enum {
    T3DS_MAIN_THREAD(T3DS_ENUM)
} T3DS_MainThread;

typedef enum {
    T3DS_SND_THREAD(T3DS_ENUM)
} T3DS_SoundThread;

#endif
