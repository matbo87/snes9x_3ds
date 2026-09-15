#include "3dsupdater_ui.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <limits.h>

#include <3ds.h>

#include "3dsgpu.h"
#include "3dssettings.h"
#include "3dsthemes.h"
#include "3dsupdater.h"

namespace {

static std::vector<SMenuItem> updaterMakePickerOptions(const std::vector<std::string>& options)
{
    std::vector<SMenuItem> items;
    items.reserve(options.size());
    for(size_t i = 0; i < options.size(); ++i)
        items.emplace_back(nullptr, MenuItemType::Action, options[i], "", (int)i);
    return items;
}

static const std::vector<SMenuItem>& updaterMakeOptionsForOk()
{
    static const std::vector<SMenuItem> items = {
        SMenuItem(nullptr, MenuItemType::Action, "OK", "", 0)
    };
    return items;
}

static bool updaterConfirmDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                 std::vector<SMenuTab>& menuTabs, const char *title,
                                 const char *message, bool fadeIn = true)
{
    int result = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, message,
                                   Themes[(int)settings3DS.Theme].dialogColorWarn,
                                   updaterMakePickerOptions({ "Yes", "No" }), 1, fadeIn);
    return result == 0;
}

struct OnlineProgressContext {
    SMenuTab *dialogTab;
    bool *isDialog;
    int *currentMenuTab;
    std::vector<SMenuTab> *menuTabs;
};

static bool onlineProgress(const char *label, u32 downloaded, u32 total, void *userData)
{
    OnlineProgressContext *context = static_cast<OnlineProgressContext *>(userData);
    hidScanInput();
    bool cancelled = (hidKeysDown() & KEY_B) != 0;
    char text[160];
    if(total > 0)
        snprintf(text, sizeof(text), "%s %lu / %lu KB\nPress [B] to cancel.", label,
            (unsigned long)(downloaded / 1024), (unsigned long)(total / 1024));
    else
        snprintf(text, sizeof(text), "%s\nPress [B] to cancel.", label);
    context->dialogTab->DialogText.assign(text);
    menu3dsDrawSplash();
    menu3dsDrawEverything(*context->dialogTab, *context->isDialog, *context->currentMenuTab, *context->menuTabs);
    menu3dsSwapBuffersAndWaitForVBlank();

    return !cancelled;
}

static void showOnlineResult(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                             std::vector<SMenuTab>& menuTabs, const char *title,
                             const char *message, bool success)
{
    if(isDialog)
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs, false);
    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, message,
                      success ? Themes[(int)settings3DS.Theme].dialogColorSuccess
                              : Themes[(int)settings3DS.Theme].dialogColorWarn,
                      updaterMakeOptionsForOk(), -1, false);
    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
}

static void beginOnlineStatus(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                              std::vector<SMenuTab>& menuTabs)
{
    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Online Updater",
                      "Connecting...\nPress [B] to cancel.",
                      Themes[(int)settings3DS.Theme].dialogColorInfo,
                      std::vector<SMenuItem>(), -1, false);
}

enum AppUpdateMode {
    APP_UPDATE_REPLACE_3DSX,
    APP_UPDATE_INSTALL_CIA,
};

static AppUpdateMode appUpdateMode()
{
    return envIsHomebrew() && updaterExecutablePath()[0]
        ? APP_UPDATE_REPLACE_3DSX : APP_UPDATE_INSTALL_CIA;
}

static void installAppUpdate(const UpdaterAppInfo& info, SMenuTab& dialogTab,
                             bool& isDialog, int& currentMenuTab,
                             std::vector<SMenuTab>& menuTabs)
{
    OnlineProgressContext progress = { &dialogTab, &isDialog, &currentMenuTab, &menuTabs };
    AppUpdateMode mode = appUpdateMode();
    
    char prompt[160];
    snprintf(prompt, sizeof(prompt), "Download and install version %s? %s", info.tag,
             mode == APP_UPDATE_REPLACE_3DSX
                 ? "Snes9x will close. Start it again from the homebrew launcher."
                 : "Snes9x will restart.");
    if(!updaterConfirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update", prompt)) {
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        return;
    }

    char updateDir[PATH_MAX], target[PATH_MAX], url[512];
    const char *packageName = mode == APP_UPDATE_INSTALL_CIA ? "snes9x_3ds.cia" : "snes9x_3ds.3dsx";
    if(mode == APP_UPDATE_REPLACE_3DSX) {
        snprintf(target, sizeof(target), "%s", updaterExecutablePath());
    } else {
        snprintf(updateDir, sizeof(updateDir), "%s/updates", settings3DS.RootDir);
        mkdir(updateDir, 0777);
        size_t dirLength = strlen(updateDir);
        size_t packageLength = strlen(packageName);
        if(dirLength + 1 + packageLength >= sizeof(target)) {
            showOnlineResult(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update",
                             "The update path is too long.", false);
            return;
        }
        memcpy(target, updateDir, dirLength);
        target[dirLength] = '/';
        memcpy(target + dirLength + 1, packageName, packageLength + 1);
    }
    snprintf(url, sizeof(url), "https://github.com/matbo87/snes9x_3ds/releases/download/%s/%s",
             info.tag, packageName);
    beginOnlineStatus(dialogTab, isDialog, currentMenuTab, menuTabs);
    char label[48];
    snprintf(label, sizeof(label), "Downloading %s", info.tag);
    if(!updaterDownloadToFile(url, target, 0, label, onlineProgress, &progress)) {
        showOnlineResult(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update", "App download cancelled or failed.", false);
        return;
    }
    if(mode == APP_UPDATE_INSTALL_CIA) {
        dialogTab.DialogText.assign("Installing app update...");
        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs);
        menu3dsSwapBuffersAndWaitForVBlank();

        u64 titleId = 0;
        if(!updaterInstallCia(target, &titleId)) {
            char message[PATH_MAX + 96];
            snprintf(message, sizeof(message),
                     "Installation failed. The CIA was saved to\n%s\nInstall it with a CIA installer such as FBI.",
                     target);
            showOnlineResult(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update", message, false);
            return;
        }
        remove(target);

        if(!titleId || !updaterLaunchTitle(titleId)) {
            char message[96];
            snprintf(message, sizeof(message), "Installed %s. Snes9x will close.",
                     info.tag);
            showOnlineResult(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update", message, true);
        }
    }
    GPU3DS.emulatorState = EMUSTATE_END;
}

static u32 appUpdatePageRevision = 0;

static void populateOnlineUpdaterPage(SMenuTab& tab, int& currentMenuTab,
                                      std::vector<SMenuTab>& menuTabs, bool resetPage)
{
    tab.SetTitle("Online Updater");
    tab.SubTitle.clear();
    tab.SubTitleRight.clear();
    tab.MenuItems.clear();
    tab.MenuItems.emplace_back(nullptr, MenuItemType::Action, "  Back", "");

    const UpdaterAppInfo *info = updaterAppUpdateInfo();
    switch(updaterAppUpdateState()) {
    case UPDATER_APP_UPDATE_AVAILABLE: {
        char label[64];
        snprintf(label, sizeof(label), "  Update to %s", info->tag);
        tab.MenuItems.emplace_back([&menuTabs, &currentMenuTab](int) {
            const UpdaterAppInfo *latest = updaterAppUpdateInfo();
            if(!latest->available)
                return;
            SMenuTab dialogTab; bool isDialog = false;
            installAppUpdate(*latest, dialogTab, isDialog, currentMenuTab, menuTabs);
        }, MenuItemType::Action, label, "");
        break;
    }
    case UPDATER_APP_UPDATE_CURRENT:
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "  App is up to date", "");
        break;
    case UPDATER_APP_UPDATE_FAILED:
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "  Could not check for app update", "");
        break;
    case UPDATER_APP_UPDATE_IDLE:
    case UPDATER_APP_UPDATE_CHECKING:
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "  Checking for app update...", "");
        break;
    }

    if(resetPage) {
        tab.subPage = { SUBPAGE_ONLINE_UPDATER, 0, {}, tab.SelectedItemIndex, tab.FirstItemIndex };
        tab.SelectedItemIndex = 0;
        tab.FirstItemIndex = 0;
    }
}

} // namespace

void updater3dsOpenPage(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    updaterBeginAppUpdateCheck();
    populateOnlineUpdaterPage(tab, currentMenuTab, menuTabs, true);
    appUpdatePageRevision = updaterAppUpdateCheckRevision();
}

void updater3dsRefreshPage(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    populateOnlineUpdaterPage(tab, currentMenuTab, menuTabs, false);
    tab.MakeSureSelectionIsOnScreen(menu3dsGetListVisibleItems(tab), 2);
}

bool updater3dsCheckAndClearPageDirty(void)
{
    u32 revision = updaterAppUpdateCheckRevision();
    if(revision == appUpdatePageRevision)
        return false;
    appUpdatePageRevision = revision;
    return true;
}
