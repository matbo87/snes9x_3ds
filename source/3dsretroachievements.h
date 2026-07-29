#ifndef _3DSRETROACHIEVEMENTS_H
#define _3DSRETROACHIEVEMENTS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------
// Types
//---------------------------------------------------------

typedef enum {
    RA_LOGIN_CANCELLED = 0,
    RA_LOGIN_OK,
    RA_LOGIN_FAILED,  // see ra3dsGetLastError()
    RA_LOGIN_PENDING, // call ra3dsCompleteLogin()
} RaLoginResult;

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
    unsigned id;
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

void ra3dsDoFrame(void);

// Keeps rc_client housekeeping alive while paused.
void ra3dsIdle(void);

//---------------------------------------------------------
// Account / login
//---------------------------------------------------------

bool ra3dsIsLoggedIn(void);
RaLoginResult ra3dsPromptLogin(void);
RaLoginResult ra3dsCompleteLogin(void);
void ra3dsLogout(void);
const char *ra3dsGetLastError(void);

bool ra3dsGetUser(RaUser *out);

//---------------------------------------------------------
// Game / achievements
//---------------------------------------------------------

const char *ra3dsGetGameTitle(void);

// Returns false when no game is loaded.
bool ra3dsGetGameSummary(RaGameSummary *out);

// Copies "" when rich presence is unavailable.
void ra3dsGetRichPresence(char *out, size_t outSize);

int ra3dsGetAchievementCount(void);

// Copies up to maxItems core achievements. Returns the number written.
int ra3dsGetAchievements(RaAchievementInfo *out, int maxItems);

#ifdef __cplusplus
}
#endif

#endif // _3DSRETROACHIEVEMENTS_H
