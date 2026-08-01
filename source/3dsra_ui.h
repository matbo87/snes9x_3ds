#ifndef _3DSRA_UI_H_
#define _3DSRA_UI_H_

#include <vector>

#include "3dsmenu.h"   // SMenuTab, SMenuItem

// Presentation hooks for the generic menu.

// Opens the achievement detail page in this tab.
void ra3dsOpenAchievementsPage(SMenuTab& tab);

// Adds the RetroAchievements entry when the current game has achievements.
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

// Closes the open cache, keeping the buffers. For game teardown (impl3dsLoadROM);
// a reopen just calls ra3dsOpenBadgeCache.
void ra3dsCloseBadgeCache(void);

// Resolves a cached badge into the internal display buffer. Returns false when
// no badge is cached (missing/failed download).
bool ra3dsLoadBadge(unsigned achievementId, bool unlocked);

// Draws the loaded badge with its bottom-right corner at (rightX, bottomY).
void ra3dsDrawBadge(int rightX, int bottomY);

#endif
