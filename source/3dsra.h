#ifndef _3DSRA_H
#define _3DSRA_H

#include <cstddef>
#include <cstdint>

//---------------------------------------------------------
// Types
//---------------------------------------------------------

typedef enum {
    RA_LOGIN_CANCELLED = 0,
    RA_LOGIN_OK,
    RA_LOGIN_FAILED,  // see ra3dsGetLastError()
    RA_LOGIN_PENDING, // call ra3dsBeginLogin()
} RaLoginResult;

// What the UI is currently waiting on
typedef enum {
    RA_PENDING_NONE = 0,
    RA_PENDING_LOGIN,
    RA_PENDING_GAME_LOAD,
} RaPending;

typedef struct RaUser {
    char name[64];
    int  softcorePoints;
    bool hardcore;
} RaUser;

typedef struct RaGameSummary {
    int unlocked;
    int total;
    int pointsUnlocked;
    int pointsTotal;
    bool beaten;
    bool mastered;
    int unsupported;
    char beatenDate[12];   // "MM/DD/YY" or ""
    char masteredDate[12]; // "MM/DD/YY" or ""
} RaGameSummary;

// Mirrors RC_CLIENT_ACHIEVEMENT_TYPE_* (rc_client.h).
typedef enum {
    RA_ACH_TYPE_STANDARD = 0,
    RA_ACH_TYPE_MISSABLE = 1,
    RA_ACH_TYPE_PROGRESSION = 2,
    RA_ACH_TYPE_WIN = 3,
} RaAchievementType;

typedef struct RaAchievementInfo {
    uint32_t id;
    char title[128];
    char description[256];
    int  points;
    float rarity;     // unlock rate, 0-100
    bool unlocked;
    bool unsupported;
    int  type;        // RaAchievementType
    char unlockDate[12]; // "MM/DD/YY" or ""
} RaAchievementInfo;

//---------------------------------------------------------
// Lifecycle
//---------------------------------------------------------

void ra3dsInitialize(void);
void ra3dsFinalize(void);

// Identifies the loaded ROM with RetroAchievements.
void ra3dsLoadGame(void);

void ra3dsUnloadGame(void);

void ra3dsReset(void);

// Drops an in-flight unlock toast and reopens the merge window. Use this rather than
// notif3dsHideRich: hiding the slot alone leaves the next unlock merging into it.
void ra3dsDropUnlockToast(void);

void ra3dsDoFrame(void);

// Keeps rc_client housekeeping alive while paused.
void ra3dsIdle(void);

//---------------------------------------------------------
// Account / login
//---------------------------------------------------------

bool ra3dsIsAvailable(void);
bool ra3dsIsLoggedIn(void);
RaPending ra3dsPending(void);
bool ra3dsLoginInFlight(void);
void ra3dsCancelPending(void);
RaLoginResult ra3dsPromptLogin(void);
void ra3dsBeginLogin(void);
void ra3dsLogout(void);
const char *ra3dsGetLastError(void);

bool ra3dsGetUser(RaUser *out);

//---------------------------------------------------------
// Game / achievements
//---------------------------------------------------------

const char *ra3dsGetGameTitle(void);

// 0 when no game is loaded/identified.
uint32_t ra3dsGetLoadedGameId(void);

// Returns false when no game is loaded.
bool ra3dsGetGameSummary(RaGameSummary *out);

// Copies "" when rich presence is unavailable.
void ra3dsGetRichPresence(char *out, size_t outSize);

int ra3dsGetAchievementCount(void);

// Menu-refresh signalling
bool ra3dsCheckAndClearMenuDirty(void);
uint32_t ra3dsGetLastUnlockedId(void);

// Copies up to maxItems core achievements. Returns the number written.
int ra3dsGetAchievements(RaAchievementInfo *out, int maxItems);

// Badge download / cache build (menu-driven). The display side lives in
// 3dsra_ui.h (badge reader).
int  ra3dsBeginBadgeCache(void);
bool ra3dsBadgeCachePoll(int *doneOut, int *totalOut);
void ra3dsEndBadgeCache(void);

//---------------------------------------------------------
// Badge cache format — shared by the writer (3dsra.cpp) and the display reader
// (3dsra_ui.cpp), which read and write the same
// <RootDir>/ra_badges/<gameId>.cache files.
//---------------------------------------------------------

// Uses the shared ImageCacheHeader/ImageCacheEntry format (3dsimg_cache.h).
// "RA Badge" + format version in the 4th char; bump the char on a format change.
#define RA_BADGE_MAGIC "RAB1"

// Capacity of the staging/display buffers and the index.
// Everything is stored at 64x64: achievement badges are native 64x64, 
// while 96x96 game thumbnail is downscaled to 64x64 at cache-write time. 
// Off-size entries < 64x64 are accepted, larger ones are skipped.
constexpr uint16_t badgeMaxWidth  = 64;
constexpr uint16_t badgeMaxHeight = 64;
constexpr size_t   badgeMaxCount  = 512;   // 256 achievements x locked/unlocked; bounds download time

// Reserved key for the game thumbnail (RetroAchievements ids start at 1)
#define RA_GAME_BADGE_KEY 0u

// <RootDir>/ra_badges/<gameId>.cache
void getBadgePath(uint32_t gameId, char *out, size_t outSize);

#endif // _3DSRA_H
