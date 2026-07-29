#ifndef _3DSRETROACHIEVEMENTS_UI_H_
#define _3DSRETROACHIEVEMENTS_UI_H_

#include <vector>

#include "3dsmenu.h"   // SMenuTab, SMenuItem

// Presentation hooks for the generic menu.

// Opens the achievement detail page in this tab.
void ra3dsOpenAchievementsPage(SMenuTab& tab);

// Adds the RetroAchievements entry when the current game has achievements.
bool ra3dsAppendMenuEntry(std::vector<SMenuItem>& items);

#endif
