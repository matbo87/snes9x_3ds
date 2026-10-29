#ifndef _3DSUTILS_H_
#define _3DSUTILS_H_

#include <3ds.h>
#include <cstdio>

typedef struct {
    bool                isNew3DS;
    bool                isReal3DS;     // false under Citra/Azahar
    int                 secondaryCore; // core for helper threads; set by utils3dsInitSecondaryCore
    CFG_SystemModel     model;
    CFG_Region          region;
} SDevice3DS;

extern SDevice3DS device3DS;

// Helper threads on device3DS.secondaryCore (lower value = higher priority).
#define THREAD_PRIORITY_AUDIO       0x18
#define THREAD_PRIORITY_SD_WORKER   0x19

typedef enum {
    EMUSTATE_EMULATE = 1,
    EMUSTATE_PAUSEMENU = 2,
    EMUSTATE_END = 3
} EMUSTATE;

typedef struct {
    EMUSTATE            emulatorState;
    bool                isRomFsLoaded;
    bool                isRomLoaded;
    bool                turboMode;             // fast-forward state
    bool                cheatsDirty;
    bool                menuTabDirty[4];
    bool                sdWriteFailPending;    // Write-mode open/close failure not shown yet.
    int                 sdWriteFailNotifCount;
} SSession3DS;

extern SSession3DS session3DS;

void utils3dsInitialize();

// New3DS: core 2. Old3DS: core 1 (syscore) under an APT CPU limit, core 0 if that fails.
void utils3dsInitSecondaryCore();
// Old3DS CPU limit around APT suspend/sleep and exit; no-ops otherwise.
void utils3dsApplyCpuLimit();
void utils3dsRestoreCpuLimit();
int utils3dsGetRandomInt(int min, int max, int excluded = -1);

// time_t timestamp to a strftime format, "YYYY-MM-DD HH:MM" by default.
// returns true if successful, false if buffer too small.
bool utils3dsGetFormattedDate(time_t timestamp, char* output, size_t bufferSize, const char* format = "%Y-%m-%d %H:%M");

// DJB2
u32 utils3dsHashString(const char* str);

void utils3dsGetSanitizedPath(const char* path, char* output, size_t bufferSize);
void utils3dsGetBasename(const char* path, char* output, size_t bufferSize, bool keepExtension);

// Removes " (USA)", "[!]", etc.
void utils3dsGetTrimmedBasename(const char* path, char* output, size_t bufferSize, bool keepExtension);

void utils3dsDebugPause();

#endif
