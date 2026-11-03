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
    if(session3DS.isRomLoaded)
        return Memory.ROMFilename;
    return NULL;
}

static bool updaterConfirmDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                 std::vector<SMenuTab>& menuTabs, const char *title,
                                 const char *message, bool fadeIn = true)
{
    int result = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, message,
                                   Themes[(int)settings3DS.Theme].dialogColorWarn,
                                   menu3dsMakePickerOptions({ "Yes", "No" }), 1, fadeIn,
                                   "", true);
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
    snprintf(text, sizeof(text), "%s%s: %lu%%%s", label, size,
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
                      menu3dsMakeOptionsForOk(), -1, false);
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
        message = "This file is not available online."; break;
    default:
        message = "Download failed."; break;
    }
    updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, message, false);
}

static void updaterShowWaitDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                  std::vector<SMenuTab>& menuTabs, const char *title, const char *status,
                                  bool fadeIn = false, bool sequence = false)
{
    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, status,
                      Themes[(int)settings3DS.Theme].dialogColorInfo,
                      std::vector<SMenuItem>(), -1, fadeIn, "Press [B] to cancel.", sequence);
}

static void updaterShowProgressDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                      std::vector<SMenuTab>& menuTabs, const char *title,
                                      const char *label, bool showTransfer = true, bool fadeIn = false)
{
    char status[96];
    snprintf(status, sizeof(status), "%s: %s", label,
             showTransfer ? "connecting..." : "downloading...");
    updaterShowWaitDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, status, fadeIn, true);
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
        snprintf(output, size, "Date unknown");
}

static void assetDates(const char *path, u32 published, char *output, size_t size)
{
    const FileInfo& local = file3dsGetFileInfo(path);
    if(!local.exists && !published) {
        snprintf(output, size, "N/A");
        return;
    }

    char localDate[32] = "N/A", onlineDate[32] = "N/A";
    if(local.exists)
        formatAssetDate((time_t)local.mtime, localDate, sizeof(localDate));
    if(published)
        formatAssetDate((time_t)published, onlineDate, sizeof(onlineDate));
    snprintf(output, size, "%s%c%s", localDate, UI_TEXT_SECTION_SEPARATOR, onlineDate);
}

static std::string assetDatesHeader(void)
{
    return std::string("On SD since") + UI_TEXT_SECTION_SEPARATOR + "Published";
}

static UpdaterGameAssets pageGameAssets = {};

struct AssetDownload {
    char source[PATH_MAX];
    char target[PATH_MAX];
    const char *title = "Online Updater";
    const char *label;
    bool inPlace = false;       // bypasses the temporary file; small files, no transfer status
    char replacePromptPrefix[64];
};

// Asks before replacing an existing file, unless OverwriteAssetsWithoutAsking is set.
// Returns false when declined; otherwise *result holds the download outcome.
static bool runAssetDownload(const AssetDownload& asset, UpdaterDownloadResult *result,
                             SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                             std::vector<SMenuTab>& menuTabs)
{
    bool askFirst = file3dsGetFileInfo(asset.target).exists && !settings3DS.OverwriteAssetsWithoutAsking;

    if(askFirst) {
        char prompt[256];
        snprintf(prompt, sizeof(prompt),
                 "%s already on your SD card.\nReplace with the online version?",
                 asset.replacePromptPrefix);
        if(!updaterConfirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs,
                                 asset.title, prompt)) {
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
            return false;
        }
    }

    updaterShowProgressDialog(dialogTab, isDialog, currentMenuTab, menuTabs, asset.title,
                              asset.label, !asset.inPlace, !askFirst);

    *result = UPDATER_DOWNLOAD_FAILED;
    char url[PATH_MAX * 2];
    if(updaterBuildAssetUrl(asset.source, url, sizeof(url))) {
        DownloadProgressContext progress = { &dialogTab, &isDialog, &currentMenuTab, &menuTabs };
        progress.showTransfer = !asset.inPlace;
        size_t length = strlen(asset.source);
        bool gzipped = length > 3 && !strcmp(asset.source + length - 3, ".gz");
        *result = asset.inPlace
            ? updaterDownloadInPlace(url, asset.target, asset.label,
                                     updaterOnDownloadProgress, &progress)
            : updaterDownloadToFile(url, asset.target, gzipped, asset.label,
                                    updaterOnDownloadProgress, &progress);
    }

    if(*result == UPDATER_DOWNLOAD_OK)
        menu3dsMarkTabDirty(currentMenuTab);   // refreshes the local dates
    return true;
}

static void showAssetResult(UpdaterDownloadResult result, const char *title,
                            const char *successHint, SMenuTab& dialogTab, bool& isDialog,
                            int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    if(result != UPDATER_DOWNLOAD_OK) {
        updaterShowDownloadFailure(dialogTab, isDialog, currentMenuTab, menuTabs, title, result);
        return;
    }

    char message[256];
    snprintf(message, sizeof(message), "Downloaded.%s%s",
             successHint ? "\n" : "", successHint ? successHint : "");
    updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, message, true);
}

static void chooseThumbnailUpdate(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                  std::vector<SMenuTab>& menuTabs)
{
    const UpdaterCacheManifest& manifest = *updaterCacheManifest();
    if(manifest.count <= 0)
        return;
    std::vector<std::string> options, dates;
    for(int i = 0; i < manifest.count; ++i) {
        char path[PATH_MAX], description[64];
        file3dsGetThumbnailCachePath(manifest.variants[i].mode, path, sizeof(path));
        assetDates(path, manifest.variants[i].published, description, sizeof(description));
        options.emplace_back(file3dsThumbnailLabel(manifest.variants[i].mode));
        dates.emplace_back(description);
    }

    const char *message = "Pick a thumbnail type to download. Each is 10-15 MB\n(1000 games) and takes about 1-2 minutes.";

    std::vector<SMenuItem> items = menu3dsMakePickerOptions(options, dates);
    items.insert(items.begin(), SMenuItem(nullptr, MenuItemType::Header2, "Type", assetDatesHeader()));

    int selection = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Game Thumbnails", message,
                                      Themes[(int)settings3DS.Theme].dialogColorInfo,
                                      items);

    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);

    if(selection < 0 || selection >= manifest.count)
        return;

    const UpdaterCacheVariant& variant = manifest.variants[selection];
    const char *label = file3dsThumbnailLabel(variant.mode);
    AssetDownload asset = {};
    snprintf(asset.source, sizeof(asset.source), "%s", variant.source);
    file3dsGetThumbnailCachePath(variant.mode, asset.target, sizeof(asset.target));
    asset.title = "Game Thumbnails";
    asset.label = label;
    snprintf(asset.replacePromptPrefix, sizeof(asset.replacePromptPrefix), "%s is", label);

    // Only the selected type's cache is open in the reader, and an open file cannot be replaced.
    bool selected = variant.mode == settings3DS.GameThumbnailType;
    if(selected)
        img3dsCloseThumbnailCache();

    UpdaterDownloadResult result;
    bool declined = !runAssetDownload(asset, &result, dialogTab, isDialog, currentMenuTab, menuTabs);
    file3dsSetThumbnailTypeAvailable(variant.mode, file3dsGetFileInfo(asset.target).exists);
    if(selected)
        img3dsOpenThumbnailCache();

    if(!declined)
        showAssetResult(result, asset.title,
                        selected ? NULL : "You can select it under Emulator \273 Game Thumbnail.",
                        dialogTab, isDialog, currentMenuTab, menuTabs);
}

static void reloadBackground(SGPU_TEXTURE_ID textureId)
{
    img3dsInvalidateAsset(textureId);
    impl3dsUpdateUiAssets();
    menu3dsSetScreenDirty();
}

static void reloadGameScreenBackground(void)
{
    reloadBackground(UI_BG_GAME);
}

static void reloadSecondScreenBackground(void)
{
    reloadBackground(UI_BG_SECOND);
}

struct GameAsset {
    const char *title;
    const char *dir;
    const char *ext;
    const char *replacePromptPrefix;
    void (*reload)(void);   // after a successful download
    const Setting::AssetMode *mode;
    const char *successHint;
};

static const GameAsset gameScreenAsset = {
    "Game Screen BG", GAME_SCREEN_BG_DIR, ".png", "Game Screen BG is", reloadGameScreenBackground,
    &settings3DS.GameScreenBg, "You can select it under Settings \273 Game Screen BG." };
static const GameAsset secondScreenAsset = {
    "Second Screen BG", SECOND_SCREEN_BG_DIR, ".png", "Second Screen BG is", reloadSecondScreenBackground,
    &settings3DS.SecondScreenBg, "You can select it under Settings \273 Second Screen BG." };

static const char *gameAssetHint(const GameAsset& kind)
{
    if(kind.mode && (*kind.mode == Setting::AssetMode::Adaptive || *kind.mode == Setting::AssetMode::CustomOnly))
        return NULL;
    return kind.successHint;
}

static void gameAssetTarget(const GameAsset& kind, char *output, size_t size)
{
    file3dsGetRelatedPath(updaterRomPath(), output, size, kind.ext, kind.dir, true);
}

static void runGameAssetUpdate(const GameAsset& kind, UpdaterAssetGroup group, const char *label,
                               SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                               std::vector<SMenuTab>& menuTabs)
{
    u32 published = pageGameAssets.published[group];
    if(!published)
        return;

    char name[NAME_MAX + 1];
    file3dsGetGameAssetName(updaterRomPath(), name, sizeof(name));

    AssetDownload asset = {};
    gameAssetTarget(kind, asset.target, sizeof(asset.target));
    snprintf(asset.source, sizeof(asset.source), "%s/%s%s", updaterAssetGroupPath(group), name, kind.ext);
    asset.title = kind.title;
    asset.label = label;
    asset.inPlace = true;
    snprintf(asset.replacePromptPrefix, sizeof(asset.replacePromptPrefix), "%s", kind.replacePromptPrefix);

    UpdaterDownloadResult result;
    if(!runAssetDownload(asset, &result, dialogTab, isDialog, currentMenuTab, menuTabs))
        return;
    if(result == UPDATER_DOWNLOAD_OK)
        kind.reload();
    showAssetResult(result, kind.title, gameAssetHint(kind), dialogTab, isDialog, currentMenuTab, menuTabs);
}

static void runGameScreenBackgroundUpdate(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                          std::vector<SMenuTab>& menuTabs)
{
    runGameAssetUpdate(gameScreenAsset, UPDATER_ASSET_GAME_SCREEN, "Game Screen",
                       dialogTab, isDialog, currentMenuTab, menuTabs);
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
            char date[32];
            formatAssetDate((time_t)game->published[secondScreenVariants[i].group], date, sizeof(date));
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
                                      "Choose a variant. Only one is kept on the SD card. Downloading another replaces it.",
                                      Themes[(int)settings3DS.Theme].dialogColorInfo,
                                      menu3dsMakePickerOptions(labels, dates));
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        if(selection < 0 || selection >= (int)available.size())
            return;
    }

    const auto& variant = secondScreenVariants[available[selection]];
    runGameAssetUpdate(secondScreenAsset, variant.group, variant.label,
                       dialogTab, isDialog, currentMenuTab, menuTabs);
}

static void reloadCheats(void)
{
    impl3dsReloadCheats();
    menu3dsMarkTabDirty(TAB_CHEATS);
}

static const GameAsset cheatAsset = {
    "Cheats", CHEATS_DIR, ".chx", "Cheats for this game are", reloadCheats,
    NULL, "You'll find them in the Cheats tab." };

static void runCheatUpdate(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                           std::vector<SMenuTab>& menuTabs)
{
    runGameAssetUpdate(cheatAsset, UPDATER_ASSET_CHEATS, "Cheats",
                       dialogTab, isDialog, currentMenuTab, menuTabs);
}

static void installAppUpdate(const UpdaterAppInfo& info, SMenuTab& dialogTab,
                             bool& isDialog, int& currentMenuTab,
                             std::vector<SMenuTab>& menuTabs)
{
    DownloadProgressContext progress = { &dialogTab, &isDialog, &currentMenuTab, &menuTabs };
    AppUpdateMode mode = appUpdateMode();

    char prompt[160];
    if(mode == APP_UPDATE_REPLACE_3DSX)
        snprintf(prompt, sizeof(prompt), "Download version %s? The emulator will close afterwards. Restart it from the Homebrew Launcher.", info.tag);
    else
        snprintf(prompt, sizeof(prompt), "Download and install version %s?\nThe emulator will restart afterwards.", info.tag);
    if(!updaterConfirmDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Emulator Update",prompt)) {
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
        return;
    }

    char target[PATH_MAX], url[512];
    const char *packageName = mode == APP_UPDATE_INSTALL_CIA ? "snes9x_3ds.cia" : "snes9x_3ds.3dsx";
    if(mode == APP_UPDATE_REPLACE_3DSX) {
        snprintf(target, sizeof(target), "%s", updaterExecutablePath());
    } else {
        snprintf(target, sizeof(target), "%s/updates", settings3DS.RootDir);
        mkdir(target, 0777);
        snprintf(target, sizeof(target), "%s/updates/%s", settings3DS.RootDir, packageName);
    }

    snprintf(url, sizeof(url), "https://github.com/matbo87/snes9x_3ds/releases/download/%s/%s",
             info.tag, packageName);
    char label[48];
    snprintf(label, sizeof(label), "Downloading %s", info.tag);
    updaterShowProgressDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Emulator Update", label);
    UpdaterDownloadResult result = updaterDownloadToFile(url, target, false, label,
                                                         updaterOnDownloadProgress, &progress);
    if(result != UPDATER_DOWNLOAD_OK) {
        updaterShowDownloadFailure(dialogTab, isDialog, currentMenuTab, menuTabs, "Emulator Update",result);
        return;
    }

    if(mode == APP_UPDATE_INSTALL_CIA) {
        dialogTab.DialogText.assign("Installing...");
        dialogTab.DialogHint.clear();
        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs);
        menu3dsSwapBuffersAndWaitForVBlank();

        DownloadProgressContext installProgress = { &dialogTab, &isDialog, &currentMenuTab, &menuTabs };
        installProgress.showTransfer = false;
        u64 titleId = 0;
        if(!updaterInstallCia(target, &titleId, updaterOnDownloadProgress, &installProgress)) {
            char message[PATH_MAX + 96];
            snprintf(message, sizeof(message),
                     "Installation failed. The CIA was saved to\n%s\nInstall it with a CIA installer such as FBI.",
                     target);
            updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Emulator Update",message, false);
            return;
        }
        remove(target);

        if(!titleId || !updaterLaunchTitle(titleId)) {
            char message[96];
            snprintf(message, sizeof(message), "Installed %s. The emulator will close.",
                     info.tag);
            updaterShowResultDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Emulator Update",message, true);
        }
    }
    session3DS.emulatorState = EMUSTATE_END;
}

static bool syncPartFailed(void)
{
    for(int i = 0; i < UPDATER_SYNC_PART_COUNT; ++i)
        if(updaterSyncState((UpdaterSyncPart)i) == UPDATER_SYNC_FAILED)
            return true;
    return false;
}

static bool runSync(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                    std::vector<SMenuTab>& menuTabs)
{
    updaterBeginSync();
    if(!updaterSyncPending())
        return true;

    updaterShowWaitDialog(dialogTab, isDialog, currentMenuTab, menuTabs, "Online Updater", "Checking for online content...", true);
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
                            "Could not check for online content. Check the network connection and try again.",
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
        menu3dsMarkTabDirty(currentMenuTab);
    }, MenuItemType::Action, text, "retry");
}

typedef void (*GameAssetAction)(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab,
                                std::vector<SMenuTab>& menuTabs);

static void addGameAssetItem(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs,
                             const GameAsset& kind, const char *label, u32 published,
                             GameAssetAction action)
{
    char path[PATH_MAX], description[64];
    gameAssetTarget(kind, path, sizeof(path));
    assetDates(path, published, description, sizeof(description));
    if(!published) {
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled, label, description);
        return;
    }

    tab.MenuItems.emplace_back([&menuTabs, &currentMenuTab, action](int) {
        SMenuTab dialogTab; bool isDialog = false;
        action(dialogTab, isDialog, currentMenuTab, menuTabs);
    }, MenuItemType::Action, label, description);
}

static void addGameAssetItems(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    if(updaterSyncState(UPDATER_SYNC_GAMES) != UPDATER_SYNC_DONE) {
        addRetryItem(tab, currentMenuTab, menuTabs, "  Could not check for game assets");
        return;
    }

    // Manifest names match local asset basenames.
    char gameName[NAME_MAX + 1];
    file3dsGetGameAssetName(updaterRomPath(), gameName, sizeof(gameName));
    updaterFindGameAssets(gameName, &pageGameAssets);

    const UpdaterGameAssets *game = &pageGameAssets;
    addGameAssetItem(tab, currentMenuTab, menuTabs, gameScreenAsset, "  Game Screen BG",
                     game->published[UPDATER_ASSET_GAME_SCREEN], runGameScreenBackgroundUpdate);
    addGameAssetItem(tab, currentMenuTab, menuTabs, secondScreenAsset, "  Second Screen BG",
                     secondScreenPublished(game), chooseSecondScreenBackgroundUpdate);
    addGameAssetItem(tab, currentMenuTab, menuTabs, cheatAsset, "  Cheats",
                     game->published[UPDATER_ASSET_CHEATS], runCheatUpdate);
}

static void addThumbnailItems(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    if(updaterSyncState(UPDATER_SYNC_CACHES) != UPDATER_SYNC_DONE) {
        addRetryItem(tab, currentMenuTab, menuTabs, "  Could not check for game thumbnails");
        return;
    }

    tab.MenuItems.emplace_back([&menuTabs, &currentMenuTab](int) {
        SMenuTab dialogTab; bool isDialog = false;
        chooseThumbnailUpdate(dialogTab, isDialog, currentMenuTab, menuTabs);
    }, MenuItemType::Action, "  Game Thumbnails", "");
}

static void addAppUpdateItems(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    if(updaterSyncState(UPDATER_SYNC_APP) != UPDATER_SYNC_DONE) {
        addRetryItem(tab, currentMenuTab, menuTabs, "  Could not check for emulator update");
        return;
    }

    const UpdaterAppInfo *info = updaterAppUpdateInfo();
    if(!info->available) {
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Note,
                                   "  Emulator is up to date", "");
        return;
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
}

static void populateOnlineUpdaterPage(SMenuTab& tab, int& currentMenuTab,
                                      std::vector<SMenuTab>& menuTabs, bool resetPage)
{
    tab.SubTitle.assign("Online Updater");
    tab.SubTitleRight.clear();
    tab.MenuItems.clear();

    tab.MenuItems.emplace_back(nullptr, MenuItemType::Action, menu3dsSubPageBackLabel(), "");
    tab.MenuItems.emplace_back([](int val) {
        settings3DS.OverwriteAssetsWithoutAsking = val != 0;
        settings3DS.isDirty = true;
    }, MenuItemType::Checkbox, "  Overwrite files without asking", "", settings3DS.OverwriteAssetsWithoutAsking);
    tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled, "", "");

    if(updaterRomPath()) {
        std::string columns;
        if(updaterSyncState(UPDATER_SYNC_GAMES) == UPDATER_SYNC_DONE)
            columns = assetDatesHeader();
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2, "Current Game", columns);
        addGameAssetItems(tab, currentMenuTab, menuTabs);
        tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled, "", "");
    }

    tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2, "Appearance", "");
    addThumbnailItems(tab, currentMenuTab, menuTabs);
    tab.MenuItems.emplace_back(nullptr, MenuItemType::Disabled, "", "");

    tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2, "Emulator Update", "");
    addAppUpdateItems(tab, currentMenuTab, menuTabs);

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
}

void updater3dsRefreshPage(SMenuTab& tab, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    populateOnlineUpdaterPage(tab, currentMenuTab, menuTabs, false);
}
