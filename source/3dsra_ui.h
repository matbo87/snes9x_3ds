#ifndef _3DSRA_UI_H_
#define _3DSRA_UI_H_

#include <vector>

#include "3dsmenu.h"   // SMenuTab, SMenuItem

// Presentation hooks for the generic menu.

// Opens the achievement detail page in this tab.
void ra3dsOpenAchievementsPage(SMenuTab& tab);

void ra3dsRefreshAchievementsPage(SMenuTab& tab);

// Adds the RetroAchievements entry when the current game is identified by RA.
bool ra3dsAppendMenuEntry(std::vector<SMenuItem>& items);

//---------------------------------------------------------
// Badge display reader
//---------------------------------------------------------

// Allocate / free the badge display buffers. Called by the app once at startup
// and shutdown (3dsmain.cpp), alongside ra3dsInitialize / ra3dsFinalize.
void ra3dsUiInitialize(void);
void ra3dsUiFinalize(void);

// Refreshes the reader onto the loaded game's badge cache (closing any prior one
// first), so the RA page draws without a first-view fopen. Idempotent; a no-op
// when no game or cache is present.
void ra3dsOpenBadgeCache(void);

const char* ra3dsGetBadgeCacheDate(void);

// Closes the open cache, keeping the buffers. For game teardown (impl3dsLoadROM);
// a reopen just calls ra3dsOpenBadgeCache.
void ra3dsCloseBadgeCache(void);

// Resolves a cached badge into the internal display buffer. Returns false when
// no badge is cached (missing/failed download).
bool ra3dsLoadBadge(u32 key, bool unlocked = false);

// Draws the loaded badge with its bottom-right corner at (rightX, bottomY).
void ra3dsDrawBadge(int rightX, int bottomY);

const u16* ra3dsGetBadgePixels(int* w, int* h);

struct RaTag { char glyph; const char* label; };

// The first four entries match RaAchievementType.
enum RaTagId {
    RA_TAG_STANDARD = 0,
    RA_TAG_MISSABLE,
    RA_TAG_PROGRESSION,
    RA_TAG_WIN,
    RA_TAG_UNLOCKED,
    RA_TAG_UNSUPPORTED,
    RA_TAG_ACHIEVEMENTS,
    RA_TAG_POINTS,
    RA_TAG_UNLOCK_RATE,
    RA_TAG_BEATEN_PROGRESS,
    RA_TAG_BEATEN,
    RA_TAG_MASTERED,
    RA_TAG_RICH_PRESENCE,
    RA_TAG_COUNT,
};

RaTag ra3dsTag(RaTagId id);

RaTag ra3dsTagByType(int achievementType);

#endif
