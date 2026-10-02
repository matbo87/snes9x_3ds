#include "3dsupdater_ui.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <limits.h>

#include <3ds.h>

#include "3dsfiles.h"
#include "3dsgpu.h"
#include "3dssettings.h"
#include "3dsthemes.h"
#include "3dsui_img.h"
#include "3dsupdater.h"
#include "3dsutils.h"

namespace {

static std::vector<SMenuItem> updaterMakePickerOptions(const std::vector<std::string>& options,
                                                      const std::vector<std::string>& descriptions = {})
{
    std::vector<SMenuItem> items;
    items.reserve(options.size());
    for(size_t i = 0; i < options.size(); ++i)
        items.emplace_back(nullptr, MenuItemType::Action, options[i],
                           i < descriptions.size() ? descriptions[i] : "", (int)i);
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

struct DownloadProgressContext {
    SMenuTab *dialogTab;
    bool *isDialog;
    int *currentMenuTab;
    std::vector<SMenuTab> *menuTabs;
    u64 firstByteTime = 0;
    u32 firstByteCount = 0;
    u64 speedShownTime = 0;
    u32 speedKBps = 0;
};

static bool updaterOnDownloadProgress(const char *label, u32 downloaded, u32 total, void *userData)
{
    DownloadProgressContext *context = static_cast<DownloadProgressContext *>(userData);
    hidScanInput();
    bool cancelled = (hidKeysDown() & KEY_B) != 0;
    bool started = total || downloaded;
    char text[160];
    u32 percent = total ? (u32)((u64)downloaded * 100 / total) : 0;
    char size[24] = "";
    if(total >= 1024 * 1024)
        snprintf(size, sizeof(size), "  \267  %lu.%lu MB", (unsigned long)(total / (1024 * 1024)),
            (unsigned long)(total % (1024 * 1024) * 10 / (1024 * 1024)));
    else if(total)
        snprintf(size, sizeof(size), "  \267  %lu KB", (unsigned long)((total + 1023) / 1024));
    // Average since the first byte, refreshed once a second so the digits stay readable.
    u64 now = osGetTime();
    if(downloaded && !context->firstByteTime) {
        context->firstByteTime = now;
        context->firstByteCount = downloaded;
        context->speedShownTime = now;
    } else if(context->firstByteTime && now - context->speedShownTime >= 1000) {
        context->speedShownTime = now;
        context->speedKBps = (u32)((u64)(downloaded - context->firstByteCount) * 1000
            / ((now - context->firstByteTime) * 1024));
    }
    char speed[24] = "";
    if(context->speedKBps)
        snprintf(speed, sizeof(speed), " (%lu KB/s)", (unsigned long)context->speedKBps);
    snprintf(text, sizeof(text), "%s%s: %lu%%%s\nPress [B] to cancel.", label, size,
        (unsigned long)(percent > 100 ? 100 : percent), speed);
    menu3dsDrawSplash();
    if(started && context->dialogTab->DialogText != text) {
        context->dialogTab->DialogText.assign(text);
        menu3dsDrawEverything(*context->dialogTab, *context->isDialog, *context->currentMenuTab, *context->menuTabs);
    }
    menu3dsSwapBuffersAndWaitForVBlank();

    return !cancelled;
}

static void updaterShowResultDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                    std::vector<SMenuTab>& menuTabs, const char *title,
                                    const char *message, bool success)
{
    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, message,
                      success ? Themes[(int)settings3DS.Theme].dialogColorSuccess
                              : Themes[(int)settings3DS.Theme].dialogColorWarn,
                      updaterMakeOptionsForOk(), -1, false);
    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
}

static void updaterShowProgressDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                      std::vector<SMenuTab>& menuTabs, const char *label)
{
    char text[160];
    snprintf(text, sizeof(text), "%s: connecting...\nPress [B] to cancel.", label);
    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Online Updater", text,
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

static void formatAssetDate(time_t timestamp, char *output, size_t size)
{
    if(!timestamp || !utils3dsGetFormattedDate(timestamp, output, size, "%Y-%m-%d"))
        snprintf(output, size, "unknown date");
}

static std::string publishedDate(u32 published)
{
    char date[32];
    formatAssetDate((time_t)published, date, sizeof(date));
    return std::string("online: ") + date;
}

static std::string localMarkedLabel(const char *label, const char *path)
{
    std::string text(label);
    if(IsFileExists(path))
        text += std::string(" ") + (char)UI_ICON_CHECKMARK;
    return text;
}

static u32 localAssetRevision = 0;

struct AssetDownload {
    std::string source;
    std::string target;
    const char *label;
    u32 published;
};

static void runAssetUpdate(const AssetDownload& asset, const char *operation,
                           SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                           std::vector<SMenuTab>& menuTabs, void (*afterRun)(bool updated) = NULL,
                           const char *successHint = NULL)
{
    if(IsFileExists(asset.target.c_str())) {
        u64 mtime = 0;
        char localDate[32], remoteDate[32];
        archive_getmtime(asset.target.c_str(), &mtime);
        formatAssetDate((time_t)mtime, localDate, sizeof(localDate));
        formatAssetDate((time_t)asset.published, remoteDate, sizeof(remoteDate));
        const char *name = strrchr(asset.target.c_str(), '/');
        char prompt[256];
        snprintf(prompt, sizeof(prompt),
                 "%s already exists.\nsd card: %s  \267  online: %s\n"
                 "Replace the local file with the online file?",
                 name ? name + 1 : asset.target.c_str(), localDate, remoteDate);
        if(!updaterConfirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                                 "Online Updater", prompt)) {
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
            if(afterRun)
                afterRun(false);
            return;
        }
    }

    updaterShowProgressDialog(dialogTab, isDialog, currentMenuTab, menuTabs, asset.label);

    UpdaterDownloadResult result = UPDATER_DOWNLOAD_FAILED;
    char url[PATH_MAX * 2];
    if(updaterBuildAssetUrl(asset.source.c_str(), url, sizeof(url))) {
        DownloadProgressContext progress = { &dialogTab, &isDialog, &currentMenuTab, &menuTabs };
        result = updaterDownloadToFile(url, asset.target.c_str(), asset.label,
                                       updaterOnDownloadProgress, &progress);
    }
    bool updated = result == UPDATER_DOWNLOAD_OK;
    if(updated)
        ++localAssetRevision;
    if(afterRun)
        afterRun(updated);

    if(!updated) {
        const char *message;
        switch(result) {
        case UPDATER_DOWNLOAD_CANCELLED:
            message = "Download cancelled."; break;
        case UPDATER_DOWNLOAD_NO_CONNECTION:
            message = "Could not reach the server. Check the network connection."; break;
        case UPDATER_DOWNLOAD_NOT_FOUND:
            message = "The update source does not have this file."; break;
        default:
            message = "Download failed."; break;
        }
        updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Online Updater",
                                message, false);
        return;
    }
    char message[256];
    snprintf(message, sizeof(message), "%s complete.", operation);
    if(successHint) {
        size_t used = strlen(message);
        snprintf(message + used, sizeof(message) - used, "\n%s", successHint);
    }
    updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Online Updater",
                            message, true);
}

static void reloadThumbnailCaches(bool updated)
{
    if(updated)
        file3dsRefreshThumbnailTypes();
    img3dsOpenThumbnailCache();
}

static void chooseThumbnailUpdate(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                  std::vector<SMenuTab>& menuTabs)
{
    const UpdaterCacheManifest& manifest = *updaterCacheManifest();
    if(manifest.count <= 0)
        return;
    std::vector<std::string> options, dates;
    for(int i = 0; i < manifest.count; ++i) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", settings3DS.RootDir, manifest.variants[i].source);
        options.emplace_back(localMarkedLabel(file3dsThumbnailLabel(manifest.variants[i].mode), path));
        dates.emplace_back(publishedDate(manifest.variants[i].published));
    }
    int selection = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                                      "Thumbnail Types",
                                      "Choose a thumbnail type to download. Each type is one file of 20-30 MB and can take a few minutes.",
                                      Themes[(int)settings3DS.Theme].dialogColorInfo,
                                      updaterMakePickerOptions(options, dates));

    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);

    if(selection < 0 || selection >= manifest.count)
        return;

    const UpdaterCacheVariant& variant = manifest.variants[selection];
    char target[PATH_MAX];
    snprintf(target, sizeof(target), "%s/%s", settings3DS.RootDir, variant.source);
    const char *label = file3dsThumbnailLabel(variant.mode);
    AssetDownload asset = { variant.source, target, label, variant.published };
    char operation[48];
    snprintf(operation, sizeof(operation), "%s update", label);

    // The active cache may be the target, and an open file cannot be renamed.
    img3dsCloseThumbnailCache();
    // Not selected automatically: an update is not a choice of what to display.
    const char *hint = variant.mode == settings3DS.GameThumbnailType
        ? NULL : "You can select it under Emulator \273 Game Thumbnail.";
    runAssetUpdate(asset, operation, dialogTab, isDialog, currentMenuTab, menuTabs,
                   reloadThumbnailCaches, hint);
}

static void installAppUpdate(const UpdaterAppInfo& info, SMenuTab& dialogTab,
                             bool& isDialog, int& currentMenuTab,
                             std::vector<SMenuTab>& menuTabs)
{
    DownloadProgressContext progress = { &dialogTab, &isDialog, &currentMenuTab, &menuTabs };
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
            updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update",
                                    "The update path is too long.", false);
            return;
        }
        memcpy(target, updateDir, dirLength);
        target[dirLength] = '/';
        memcpy(target + dirLength + 1, packageName, packageLength + 1);
    }
    snprintf(url, sizeof(url), "https://github.com/matbo87/snes9x_3ds/releases/download/%s/%s",
             info.tag, packageName);
    char label[48];
    snprintf(label, sizeof(label), "Downloading %s", info.tag);
    updaterShowProgressDialog(dialogTab, isDialog, currentMenuTab, menuTabs, label);
    if(updaterDownloadToFile(url, target, label, updaterOnDownloadProgress, &progress) != UPDATER_DOWNLOAD_OK) {
        updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update", "App download cancelled or failed.", false);
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
            updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update", message, false);
            return;
        }
        remove(target);

        if(!titleId || !updaterLaunchTitle(titleId)) {
            char message[96];
            snprintf(message, sizeof(message), "Installed %s. Snes9x will close.",
                     info.tag);
            updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update", message, true);
        }
    }
    GPU3DS.emulatorState = EMUSTATE_END;
}

static u32 appUpdatePageRevision = 0;
static u32 assetPageRevision = 0;
static u32 localAssetPageRevision = 0;

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

    switch(updaterCacheCheckState()) {
    case UPDATER_ASSET_CHECK_READY: {
        const UpdaterCacheManifest *manifest = updaterCacheManifest();
        u32 newest = 0;
        for(int i = 0; i < manifest->count; ++i)
            if(manifest->variants[i].published > newest)
                newest = manifest->variants[i].published;
        tab.MenuItems.emplace_back([&menuTabs, &currentMenuTab](int) {
            SMenuTab dialogTab; bool isDialog = false;
            chooseThumbnailUpdate(dialogTab, isDialog, currentMenuTab, menuTabs);
        }, MenuItemType::Action, "  Game Thumbnails", publishedDate(newest));
        break;
    }
    case UPDATER_ASSET_CHECK_FAILED:
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "  Could not check for game thumbnails", "");
        break;
    default:
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "  Checking for game thumbnails...", "");
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
    updaterBeginAssetCheck();
    populateOnlineUpdaterPage(tab, currentMenuTab, menuTabs, true);
    appUpdatePageRevision = updaterAppUpdateCheckRevision();
    assetPageRevision = updaterAssetCheckRevision();
    localAssetPageRevision = localAssetRevision;
}

void updater3dsRefreshPage(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    populateOnlineUpdaterPage(tab, currentMenuTab, menuTabs, false);
    tab.MakeSureSelectionIsOnScreen(menu3dsGetListVisibleItems(tab), 2);
}

bool updater3dsCheckAndClearPageDirty(void)
{
    u32 appRevision = updaterAppUpdateCheckRevision();
    u32 assetRevision = updaterAssetCheckRevision();
    if(appRevision == appUpdatePageRevision && assetRevision == assetPageRevision
       && localAssetRevision == localAssetPageRevision)
        return false;
    appUpdatePageRevision = appRevision;
    assetPageRevision = assetRevision;
    localAssetPageRevision = localAssetRevision;
    return true;
}
