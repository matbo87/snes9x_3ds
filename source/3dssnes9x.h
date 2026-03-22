

// Uncomment this to convert before releasing this to remove
// all the debugging stuff.
//
// #define RELEASE

// This prevents these flags from accidentally leaking into release builds.
#ifndef RELEASE

// If 1, gfxhw.cpp will run detailed profiler measurements.
// If 0, Snx_UpdateScreen will be profiled as a whole.
#define GFXHW_DETAILED_PROFILER 0

// If 1, CPU, SA-1, and GSU instructions will be counted by the profiler.
// If 0, they will not be counted.
#define T3DS_COUNT_INSTRUCTIONS 0

// If 1, GSU tests will be run and printed once at boot-up, and profiler
// output will be disabled
#define RUN_GSU_TESTS 1

#endif // RELEASE

// Uncomment this to allow user to break into debug mode (for the 65816 CPU)
// 
//#define DEBUG_CPU

// Uncomment this to allow user to break into debug mode (for the SPC700 APU)
//
//#define DEBUG_APU



#define DEBUG_WAIT_L_KEY 	\
    { \
        uint32 prevkey = 0; \
        while (aptMainLoop()) \
        {  \
            hidScanInput(); \
            uint32 key = hidKeysHeld(); \
            if (key == KEY_L) break; \
            if (key == KEY_TOUCH) break; \
            if (key == KEY_SELECT) { GPU3DS.enableDebug ^= 1; break; } \
            if (prevkey != 0 && key == 0) \
                break;  \
            prevkey = key; \
        } \
    }
