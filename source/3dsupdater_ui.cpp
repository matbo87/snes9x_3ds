#include "3dsupdater_ui.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <limits.h>

#include <3ds.h>

#include "snes9x.h"
#include "memmap.h"
#include "3dsfiles.h"
#include "3dsgpu.h"
#include "3dshttp.h"
#include "3dsimpl.h"
#include "3dssettings.h"
#include "3dsthemes.h"
#include "3dsui_img.h"
#include "3dsupdater.h"
#include "3dsutils.h"

namespace {

static const char *updaterRomPath()
{
    if(settings3DS.isRomLoaded)
        return Memory.ROMFilename;
    return NULL;
}

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
    static std::vector<SMenuItem> items;
    if(items.empty()) {
        items.reserve(1);
        items.emplace_back(nullptr, MenuItemType::Action, "OK", "", 0);
    }
    return items;
}

static const std::vector<SMenuItem>& updaterMakeOptionsForYesNo()
{
    static std::vector<SMenuItem> items;
    if(items.empty()) {
        items.reserve(2);
        items.emplace_back(nullptr, MenuItemType::Action, "Yes", "", 0);
        items.emplace_back(nullptr, MenuItemType::Action, "No", "", 1);
    }
    return items;
}

static bool updaterConfirmDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                 std::vector<SMenuTab>& menuTabs, const char *title,
                                 const char *message, bool fadeIn = true)
{
    int result = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, message,
                                   Themes[(int)settings3DS.Theme].dialogColorWarn,
                                   updaterMakeOptionsForYesNo(), 1, fadeIn, 2, 3);
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
    bool showTransfer = true;
};

static bool updaterOnDownloadProgress(const char *label, u32 downloaded, u32 total, void *userData)
{
    DownloadProgressContext *context = static_cast<DownloadProgressContext *>(userData);
    hidScanInput();
    bool cancelled = (hidKeysDown() & KEY_B) != 0;

    if(!context->showTransfer) {
        menu3dsDrawSplash();
        menu3dsSwapBuffersAndWaitForVBlank();
        return !cancelled;
    }

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
                      updaterMakeOptionsForOk(), -1, false, 2, 3);
    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
}

static void updaterShowDownloadFailure(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                       std::vector<SMenuTab>& menuTabs, const char *title,
                                       UpdaterDownloadResult result)
{
    const char *message;
    switch(result) {
    case UPDATER_DOWNLOAD_CANCELLED:
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        return;
    case UPDATER_DOWNLOAD_NO_CONNECTION:
        message = "Could not reach the server. Check the network connection."; break;
    case UPDATER_DOWNLOAD_NOT_FOUND:
        message = "The update source does not have this file."; break;
    default:
        message = "Download failed."; break;
    }
    updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, message, false);
}

static void updaterShowWaitDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                  std::vector<SMenuTab>& menuTabs, const char *status,
                                  bool fadeIn = false)
{
    char message[160];
    snprintf(message, sizeof(message), "%s\nPress [B] to cancel.", status);
    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Online Updater", message,
                      Themes[(int)settings3DS.Theme].dialogColorInfo,
                      std::vector<SMenuItem>(), -1, fadeIn, 2, 3);
}

static void updaterShowProgressDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                      std::vector<SMenuTab>& menuTabs, const char *label,
                                      bool showTransfer = true, bool fadeIn = false)
{
    char status[96];
    snprintf(status, sizeof(status), "%s: %s", label,
             showTransfer ? "connecting..." : "downloading...");
    updaterShowWaitDialog(dialogTab, isDialog, currentMenuTab, menuTabs, status, fadeIn);
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

static void publishedDate(u32 published, char *output, size_t size)
{
    char date[32];
    formatAssetDate((time_t)published, date, sizeof(date));
    snprintf(output, size, "%c %s", UI_ICON_CLOUD, date);
}

static void localMarkedLabel(const char *label, const char *path, char *output, size_t size)
{
    if(file3dsGetFileInfo(path).exists)
        snprintf(output, size, "%s %c", label, UI_ICON_CHECKMARK);
    else
        snprintf(output, size, "%s", label);
}

static u32 localAssetRevision = 0;

static UpdaterGameAssets pageGameAssets = {};

struct AssetDownload {
    char source[PATH_MAX];
    char target[PATH_MAX];
    const char *label;
    u32 published;
    bool showTransfer = true;
    bool inPlace = false;       // bypasses the temporary file
    char replacePromptPrefix[64];
};

static void runAssetUpdate(const AssetDownload& asset, const char *operation,
                           SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                           std::vector<SMenuTab>& menuTabs, void (*afterRun)(bool updated) = NULL,
                           const char *successHint = NULL)
{
    FileInfo local = file3dsGetFileInfo(asset.target);

    if(local.exists) {
        // Local and remote dates are informational.
        char localDate[40] = "", remoteDate[32];
        if(local.mtime) {
            char date[32];
            formatAssetDate((time_t)local.mtime, date, sizeof(date));
            snprintf(localDate, sizeof(localDate), " (%s)", date);
        }

        formatAssetDate((time_t)asset.published, remoteDate, sizeof(remoteDate));
        char prompt[256];
        snprintf(prompt, sizeof(prompt),
                 "%s already on your SD card%s. Replace with version from %s?",
                 asset.replacePromptPrefix, localDate, remoteDate);
        if(!updaterConfirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                                 "Online Updater", prompt)) {
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
            if(afterRun)
                afterRun(false);
            return;
        }
    }

    updaterShowProgressDialog(dialogTab, isDialog, currentMenuTab, menuTabs, asset.label,
                              asset.showTransfer, !local.exists);

    UpdaterDownloadResult result = UPDATER_DOWNLOAD_FAILED;
    char url[PATH_MAX * 2];
    if(updaterBuildAssetUrl(asset.source, url, sizeof(url))) {
        DownloadProgressContext progress = { &dialogTab, &isDialog, &currentMenuTab, &menuTabs };
        progress.showTransfer = asset.showTransfer;
        result = asset.inPlace
            ? updaterDownloadInPlace(url, asset.target, asset.label,
                                     updaterOnDownloadProgress, &progress)
            : updaterDownloadToFile(url, asset.target, asset.label,
                                    updaterOnDownloadProgress, &progress);
    }

    bool updated = result == UPDATER_DOWNLOAD_OK;
    if(updated) {
        file3dsSetFileInfo(asset.target, { true, (u64)time(NULL) });
        ++localAssetRevision;
    }

    if(afterRun)
        afterRun(updated);

    if(!updated) {
        updaterShowDownloadFailure(dialogTab, isDialog, currentMenuTab, menuTabs, "Online Updater",
                                   result);
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
        char path[PATH_MAX], label[64], date[40];
        snprintf(path, sizeof(path), "%s/%s", settings3DS.RootDir, manifest.variants[i].source);
        localMarkedLabel(file3dsThumbnailLabel(manifest.variants[i].mode), path, label, sizeof(label));
        publishedDate(manifest.variants[i].published, date, sizeof(date));
        options.emplace_back(label);
        dates.emplace_back(date);
    }

    const char *message = "Pick a thumbnail type to download. Each is 20-30 MB\n(1000 games) and takes about 2 minutes.";

    int selection = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Game Thumbnails", message,
                                      Themes[(int)settings3DS.Theme].dialogColorInfo,
                                      updaterMakePickerOptions(options, dates), -1, true, 2, 5);

    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);

    if(selection < 0 || selection >= manifest.count)
        return;

    const UpdaterCacheVariant& variant = manifest.variants[selection];
    const char *label = file3dsThumbnailLabel(variant.mode);
    AssetDownload asset = {};
    snprintf(asset.source, sizeof(asset.source), "%s", variant.source);
    snprintf(asset.target, sizeof(asset.target), "%s/%s", settings3DS.RootDir, variant.source);
    asset.label = label;
    asset.published = variant.published;
    snprintf(asset.replacePromptPrefix, sizeof(asset.replacePromptPrefix), "%s is", label);
    char operation[48];
    snprintf(operation, sizeof(operation), "%s update", label);

    // Close the active cache before replacing its file.
    img3dsCloseThumbnailCache();

    const char *hint = variant.mode == settings3DS.GameThumbnailType
        ? NULL : "You can select it under Emulator \273 Game Thumbnail.";
    runAssetUpdate(asset, operation, dialogTab, isDialog, currentMenuTab, menuTabs,
                   reloadThumbnailCaches, hint);
}

static void reloadBackground(SGPU_TEXTURE_ID textureId)
{
    img3dsInvalidateAsset(textureId);
    impl3dsUpdateUiAssets();
    menu3dsSetScreenDirty();
}

static void reloadGameScreenBackground(bool updated)
{
    if(updated)
        reloadBackground(UI_BG_GAME);
}

static void reloadSecondScreenBackground(bool updated)
{
    if(updated)
        reloadBackground(UI_BG_SECOND);
}

static void runGameScreenBackgroundUpdate(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                          std::vector<SMenuTab>& menuTabs)
{
    u32 published = pageGameAssets.published[UPDATER_ASSET_GAME_SCREEN];
    if(!published)
        return;

    AssetDownload asset = {};
    file3dsGetRelatedPath(updaterRomPath(), asset.target, sizeof(asset.target), ".png", GAME_SCREEN_BG_DIR, true);
    const char *name = strrchr(asset.target, '/');
    if(!name)
        return;

    snprintf(asset.source, sizeof(asset.source), "%s%s",
             updaterAssetGroupPath(UPDATER_ASSET_GAME_SCREEN), name);
    asset.label = "Game Screen";
    asset.published = published;
    asset.showTransfer = false;
    asset.inPlace = true;
    snprintf(asset.replacePromptPrefix, sizeof(asset.replacePromptPrefix), "Game Screen BG is");
    runAssetUpdate(asset, "Game screen BG update", dialogTab, isDialog,
                   currentMenuTab, menuTabs, reloadGameScreenBackground);
}

// All variants share one target file.
static const struct { UpdaterAssetGroup group; const char *label; } secondScreenVariants[] = {
    { UPDATER_ASSET_SECOND_SCREEN_COVER_ART,      "Cover"            },
    { UPDATER_ASSET_SECOND_SCREEN_CONSOLE_EU_JP,  "Console (EU/JP)"  },
    { UPDATER_ASSET_SECOND_SCREEN_CONSOLE_US,     "Console (USA)"    },
};

static u32 secondScreenPublished(const UpdaterGameAssets *game)
{
    u32 newest = 0;
    for(const auto& variant : secondScreenVariants)
        if(game->published[variant.group] > newest)
            newest = game->published[variant.group];
    return newest;
}

static void chooseSecondScreenBackgroundUpdate(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                               std::vector<SMenuTab>& menuTabs)
{
    const UpdaterGameAssets *game = &pageGameAssets;
    std::vector<int> available;
    std::vector<std::string> labels, dates;
    for(size_t i = 0; i < sizeof(secondScreenVariants) / sizeof(secondScreenVariants[0]); ++i) {
        if(game->published[secondScreenVariants[i].group]) {
            available.push_back((int)i);
            char date[40];
            publishedDate(game->published[secondScreenVariants[i].group], date, sizeof(date));
            labels.emplace_back(secondScreenVariants[i].label);
            dates.emplace_back(date);
        }
    }
    if(available.empty())
        return;

    int selection = 0;
    if(available.size() > 1) {
        selection = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                                      "Second Screen BG",
                                      "Choose a variant first. Only one is kept on the SD card, so downloading another variant replaces the current one.",
                                      Themes[(int)settings3DS.Theme].dialogColorInfo,
                                      updaterMakePickerOptions(labels, dates), -1, true, 2, 4);
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        if(selection < 0 || selection >= (int)available.size())
            return;
    }

    const auto& variant = secondScreenVariants[available[selection]];
    AssetDownload asset = {};
    file3dsGetRelatedPath(updaterRomPath(), asset.target, sizeof(asset.target), ".png", SECOND_SCREEN_BG_DIR, true);
    const char *name = strrchr(asset.target, '/');
    if(!name)
        return;

    snprintf(asset.source, sizeof(asset.source), "%s%s", updaterAssetGroupPath(variant.group), name);
    asset.label = variant.label;
    asset.published = game->published[variant.group];
    asset.showTransfer = false;
    asset.inPlace = true;
    snprintf(asset.replacePromptPrefix, sizeof(asset.replacePromptPrefix), "Second Screen BG is");
    runAssetUpdate(asset, "Second Screen BG update", dialogTab, isDialog,
                   currentMenuTab, menuTabs, reloadSecondScreenBackground);
}

static void reloadCheats(bool updated)
{
    if(!updated)
        return;

    impl3dsReloadCheats();
    menu3dsMarkTabDirty(TAB_CHEATS);
}

static void runCheatUpdate(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                           std::vector<SMenuTab>& menuTabs)
{
    u32 published = pageGameAssets.published[UPDATER_ASSET_CHEATS];
    if(!published)
        return;

    AssetDownload asset = {};
    file3dsGetRelatedPath(updaterRomPath(), asset.target, sizeof(asset.target), ".chx", CHEATS_DIR, true);
    const char *name = strrchr(asset.target, '/');
    if(!name)
        return;

    snprintf(asset.source, sizeof(asset.source), "%s%s", updaterAssetGroupPath(UPDATER_ASSET_CHEATS), name);
    asset.label = "Cheats";
    asset.published = published;
    asset.showTransfer = false;
    asset.inPlace = true;
    snprintf(asset.replacePromptPrefix, sizeof(asset.replacePromptPrefix), "Cheats for this game are");
    runAssetUpdate(asset, "Cheat update", dialogTab, isDialog,
                   currentMenuTab, menuTabs, reloadCheats);
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
    UpdaterDownloadResult result = updaterDownloadToFile(url, target, label,
                                                         updaterOnDownloadProgress, &progress);
    if(result != UPDATER_DOWNLOAD_OK) {
        updaterShowDownloadFailure(dialogTab, isDialog, currentMenuTab, menuTabs, "App Update", result);
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

static u32 syncPageRevision = 0;
static u32 localAssetPageRevision = 0;

static bool syncPartFailed(void)
{
    for(int i = 0; i < UPDATER_SYNC_PART_COUNT; ++i)
        if(updaterSyncState((UpdaterSyncPart)i) == UPDATER_SYNC_FAILED)
            return true;
    return false;
}

// Probe thumbnail files during the network wait.
static void prefillLocalFiles(void)
{
    for(int i = 0; i < (int)Setting::ThumbnailMode::Count; ++i) {
        const char *name = file3dsThumbnailCacheName((Setting::ThumbnailMode)i);
        if(!name)
            continue;
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/thumbnails/%s", settings3DS.RootDir, name);
        file3dsGetFileInfo(path);
    }
}

static bool runSync(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                    std::vector<SMenuTab>& menuTabs)
{
    updaterBeginSync();
    if(!updaterSyncPending())
        return true;

    updaterShowWaitDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Syncing update data...", true);
    prefillLocalFiles();
    DownloadProgressContext context = { &dialogTab, &isDialog, &currentMenuTab, &menuTabs };
    context.showTransfer = false;
    bool cancelled = false;
    while(updaterSyncPending() && aptMainLoop()) {
        http3dsDrainCompletions();
        if(!updaterOnDownloadProgress("", 0, 0, &context) && !cancelled) {
            cancelled = true;
            updaterCancelSync();
        }
    }

    return !cancelled;
}

static void showSyncFailed(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                           std::vector<SMenuTab>& menuTabs)
{
    updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Online Updater",
                            "Could not sync update data. Check the network connection and try again.",
                            false);
}

static void addRetryItem(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs,
                         const char *text)
{
    tab.MenuItems.emplace_back([&menuTabs, &currentMenuTab](int) {
        SMenuTab dialogTab; bool isDialog = false;
        
        if(runSync(dialogTab, isDialog, currentMenuTab, menuTabs) && syncPartFailed())
            showSyncFailed(dialogTab, isDialog, currentMenuTab, menuTabs);
        else
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
    }, MenuItemType::Action, text, "retry");
}

// Match manifest names to local asset basenames.
static void currentGameName(char *output, size_t size)
{
    output[0] = '\0';
    if(!updaterRomPath())
        return;

    char path[PATH_MAX];
    file3dsGetRelatedPath(updaterRomPath(), path, sizeof(path), "", CHEATS_DIR, true);
    const char *name = strrchr(path, '/');
    if(name)
        snprintf(output, size, "%s", name + 1);
}

static void addGameAssetItems(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    switch(updaterSyncState(UPDATER_SYNC_GAMES)) {
    case UPDATER_SYNC_DONE: {
        char gameName[NAME_MAX + 1];
        currentGameName(gameName, sizeof(gameName));
        updaterFindGameAssets(gameName, &pageGameAssets);

        const UpdaterGameAssets *game = &pageGameAssets;
        u32 secondScreen = secondScreenPublished(game);

        char gamePath[PATH_MAX], secondPath[PATH_MAX], cheatPath[PATH_MAX];
        file3dsGetRelatedPath(updaterRomPath(), gamePath, sizeof(gamePath), ".png", GAME_SCREEN_BG_DIR, true);
        file3dsGetRelatedPath(updaterRomPath(), secondPath, sizeof(secondPath), ".png", SECOND_SCREEN_BG_DIR, true);
        file3dsGetRelatedPath(updaterRomPath(), cheatPath, sizeof(cheatPath), ".chx", CHEATS_DIR, true);

        char gameLabel[64], secondLabel[64], cheatLabel[64];
        localMarkedLabel("  Game Screen BG", gamePath, gameLabel, sizeof(gameLabel));
        localMarkedLabel("  Second Screen BG", secondPath, secondLabel, sizeof(secondLabel));
        localMarkedLabel("  Cheats", cheatPath, cheatLabel, sizeof(cheatLabel));

        char gameDate[40], secondDate[40], cheatDate[40];
        publishedDate(game->published[UPDATER_ASSET_GAME_SCREEN], gameDate, sizeof(gameDate));
        publishedDate(secondScreen, secondDate, sizeof(secondDate));
        publishedDate(game->published[UPDATER_ASSET_CHEATS], cheatDate, sizeof(cheatDate));

        if(game->published[UPDATER_ASSET_GAME_SCREEN])
            tab.MenuItems.emplace_back([&menuTabs, &currentMenuTab](int) {
                SMenuTab dialogTab; bool isDialog = false;
                runGameScreenBackgroundUpdate(dialogTab, isDialog, currentMenuTab, menuTabs);
            }, MenuItemType::Action, gameLabel, gameDate);
        else
            tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                       gameLabel, "missing");

        if(secondScreen)
            tab.MenuItems.emplace_back([&menuTabs, &currentMenuTab](int) {
                SMenuTab dialogTab; bool isDialog = false;
                chooseSecondScreenBackgroundUpdate(dialogTab, isDialog, currentMenuTab, menuTabs);
            }, MenuItemType::Action, secondLabel, secondDate);
        else
            tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                       secondLabel, "missing");

        if(game->published[UPDATER_ASSET_CHEATS])
            tab.MenuItems.emplace_back([&menuTabs, &currentMenuTab](int) {
                SMenuTab dialogTab; bool isDialog = false;
                runCheatUpdate(dialogTab, isDialog, currentMenuTab, menuTabs);
            }, MenuItemType::Action, cheatLabel, cheatDate);
        else
            tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                       cheatLabel, "missing");
        break;
    }
    case UPDATER_SYNC_PENDING:
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "  Checking for game assets...", "");
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "", "");
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "", "");
        break;
    default:
        addRetryItem(tab, currentMenuTab, menuTabs, "  Could not check for game assets");
        break;
    }
}

static void addThumbnailItems(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    switch(updaterSyncState(UPDATER_SYNC_CACHES)) {
    case UPDATER_SYNC_DONE: {
        const UpdaterCacheManifest *manifest = updaterCacheManifest();
        u32 newest = 0;
        for(int i = 0; i < manifest->count; ++i)
            if(manifest->variants[i].published > newest)
                newest = manifest->variants[i].published;

        char date[40];
        publishedDate(newest, date, sizeof(date));
        tab.MenuItems.emplace_back([&menuTabs, &currentMenuTab](int) {
            SMenuTab dialogTab; bool isDialog = false;
            chooseThumbnailUpdate(dialogTab, isDialog, currentMenuTab, menuTabs);
        }, MenuItemType::Action, "  Game Thumbnails", date);
        break;
    }
    case UPDATER_SYNC_PENDING:
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "  Checking for game thumbnails...", "");
        break;
    default:
        addRetryItem(tab, currentMenuTab, menuTabs, "  Could not check for game thumbnails");
        break;
    }
}

static void addAppUpdateItems(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    const UpdaterAppInfo *info = updaterAppUpdateInfo();

    switch(updaterSyncState(UPDATER_SYNC_APP)) {
    case UPDATER_SYNC_DONE: {
        if(!info->available) {
            tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                       "  App is up to date", "");
            break;
        }
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
    case UPDATER_SYNC_PENDING:
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled,
                                   "  Checking for app update...", "");
        break;
    default:
        addRetryItem(tab, currentMenuTab, menuTabs, "  Could not check for app update");
        break;
    }
}

static void populateOnlineUpdaterPage(SMenuTab& tab, int& currentMenuTab,
                                      std::vector<SMenuTab>& menuTabs, bool resetPage)
{
    tab.SubTitle.assign("Online Updater");
    tab.SubTitleRight.clear();
    tab.MenuItems.clear();

    const char *backLabel = settings3DS.Theme == Setting::Theme::RetroArch ? "  Back" : "  \213 Back";
    tab.MenuItems.emplace_back(nullptr, MenuItemType::Action, backLabel, "");
    tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2, "", "");

    if(updaterRomPath()) {
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2, "Current Game", "");
        addGameAssetItems(tab, currentMenuTab, menuTabs);
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2, "", "");
    }

    tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2, "Appearance", "");
    addThumbnailItems(tab, currentMenuTab, menuTabs);

    tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2, "App Update", "");
    addAppUpdateItems(tab, currentMenuTab, menuTabs);

    tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2, "", "");
    char legend[64];
    snprintf(legend, sizeof(legend), "  %c on SD card  \267  %c latest online version",
             UI_ICON_CHECKMARK, UI_ICON_CLOUD);
    tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled, legend, "");

    if(resetPage) {
        tab.subPage = { SUBPAGE_ONLINE_UPDATER, 0, {}, tab.SelectedItemIndex, tab.FirstItemIndex };
        tab.SelectedItemIndex = 0;
        tab.FirstItemIndex = 0;
    }
}

} // namespace

void updater3dsOpenPage(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    SMenuTab dialogTab; bool isDialog = false;

    if(!runSync(dialogTab, isDialog, currentMenuTab, menuTabs)) {
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        menu3dsMarkTabDirty(currentMenuTab);
        return;
    }

    bool anyDone = false;
    for(int i = 0; i < UPDATER_SYNC_PART_COUNT; ++i)
        anyDone |= updaterSyncState((UpdaterSyncPart)i) == UPDATER_SYNC_DONE;

    if(!anyDone) {
        showSyncFailed(dialogTab, isDialog, currentMenuTab, menuTabs);
        menu3dsMarkTabDirty(currentMenuTab);
        return;
    }

    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
    populateOnlineUpdaterPage(tab, currentMenuTab, menuTabs, true);
    syncPageRevision = updaterSyncRevision();
    localAssetPageRevision = localAssetRevision;
}

void updater3dsRefreshPage(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    populateOnlineUpdaterPage(tab, currentMenuTab, menuTabs, false);
    tab.MakeSureSelectionIsOnScreen(menu3dsGetListVisibleItems(tab), 2);
}

bool updater3dsCheckAndClearPageDirty(void)
{
    u32 syncRevision = updaterSyncRevision();
    if(syncRevision == syncPageRevision && localAssetRevision == localAssetPageRevision)
        return false;
    syncPageRevision = syncRevision;
    localAssetPageRevision = localAssetRevision;
    return true;
}
