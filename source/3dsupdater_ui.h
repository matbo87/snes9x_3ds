#ifndef _3DSUPDATER_UI_H_
#define _3DSUPDATER_UI_H_

#include <vector>

#include "3dsmenu.h"

// Builds the Emulator tab's temporary Online Updater sub-page.
void updater3dsOpenPage(SMenuTab& tab, int& currentMenuTab,
                        std::vector<SMenuTab>& menuTabs);
void updater3dsRefreshPage(SMenuTab& tab, int& currentMenuTab,
                           std::vector<SMenuTab>& menuTabs);
bool updater3dsCheckAndClearPageDirty(void);

#endif
