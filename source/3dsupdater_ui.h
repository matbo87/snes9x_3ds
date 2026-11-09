#ifndef _3DSUPDATER_UI_H_
#define _3DSUPDATER_UI_H_

#include <vector>

#include "3dsmenu.h"

// Builds the Emulator tab's temporary Online Updater sub-page.
void updater3dsOpenPage(SMenuTab& tab, int& currentMenuTab,
                        std::vector<SMenuTab>& menuTabs);
void updater3dsRefreshPage(SMenuTab& tab, int& currentMenuTab,
                           std::vector<SMenuTab>& menuTabs);

// Start-up update check. The dialog shows only if the result arrives in the
// start menu with no dialog open; otherwise the result is dropped.
void updater3dsStartAutoCheck(void);
bool updater3dsPollAutoCheck(bool canShow);
void updater3dsShowAutoCheck(int& currentMenuTab, std::vector<SMenuTab>& menuTabs);

#endif
