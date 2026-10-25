#ifndef _3DSUTILS_H_
#define _3DSUTILS_H_

#include <3ds.h>
#include <cstdio>

typedef struct {
    bool                isNew3DS;
    bool                isReal3DS;     // false under Citra/Azahar
    CFG_SystemModel     model;
    CFG_Region          region;
} SDevice3DS;

extern SDevice3DS device3DS;

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
