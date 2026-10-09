#ifndef _3DSUPDATER_H_
#define _3DSUPDATER_H_

#include <3ds.h>

#include <stddef.h>

#include "3dssettings.h"

// Called on the main thread while a request is pending; return false to cancel.
typedef bool (*UpdaterProgressCallback)(const char *label, u32 downloaded,
                                        u32 total, void *userData);

enum UpdaterDownloadResult {
    UPDATER_DOWNLOAD_OK,
    UPDATER_DOWNLOAD_CANCELLED,
    UPDATER_DOWNLOAD_NOT_FOUND,      // the source has no such file (404)
    UPDATER_DOWNLOAD_NO_CONNECTION,  // no usable response: DNS, TLS, timeout, stall
    UPDATER_DOWNLOAD_FAILED,         // any other status or a local write error
};

// The order must match the columns of game-assets.txt.
enum UpdaterAssetGroup {
    UPDATER_ASSET_GAME_SCREEN,
    UPDATER_ASSET_SECOND_SCREEN_COVER_ART,
    UPDATER_ASSET_SECOND_SCREEN_CONSOLE_EU_JP,
    UPDATER_ASSET_SECOND_SCREEN_CONSOLE_US,
    UPDATER_ASSET_CHEATS,
    UPDATER_ASSET_GROUP_COUNT,
};

#define UPDATER_MAX_CACHE_VARIANTS 12

struct UpdaterCacheVariant {
    Setting::ThumbnailMode mode;
    char source[96];
    u32 published;
};

struct UpdaterCacheManifest {
    UpdaterCacheVariant variants[UPDATER_MAX_CACHE_VARIANTS];
    int count;
};

// Publication timestamps; 0 means no asset.
struct UpdaterGameAssets {
    u32 published[UPDATER_ASSET_GROUP_COUNT];
};

struct UpdaterAppInfo {
    char tag[32];
    bool available;
};

enum UpdaterSyncPart {
    UPDATER_SYNC_APP,      // GitHub's latest-release API
    UPDATER_SYNC_CACHES,   // thumbnail-caches.txt
    UPDATER_SYNC_GAMES,    // game-assets.txt
    UPDATER_SYNC_PART_COUNT,
};

enum UpdaterSyncState {
    UPDATER_SYNC_NONE,
    UPDATER_SYNC_PENDING,
    UPDATER_SYNC_DONE,
    UPDATER_SYNC_FAILED,
};

// Successful parts stay cached; failed parts are retried by the next sync.
// Results arrive through http3dsDrainCompletions.
void updaterBeginSync(void);
// Cancelled failures return to NONE; successful completions stay cached.
void updaterCancelSync(void);
UpdaterSyncState updaterSyncState(UpdaterSyncPart part);
bool updaterSyncPending(void);

const UpdaterAppInfo *updaterAppUpdateInfo(void);
const UpdaterCacheManifest *updaterCacheManifest(void);
// Uses trimmed ROM names; missing games and an unsynced manifest yield zero timestamps.
void updaterFindGameAssets(const char *gameName, UpdaterGameAssets *assets);

const char *updaterAssetGroupPath(UpdaterAssetGroup group);

// Builds a raw-content URL, percent-encoding the relative path.
bool updaterBuildAssetUrl(const char *relativePath, char *output, size_t outputSize);

// Writes small assets in place to avoid slow directory operations;
// SD write failures may damage targetPath.
UpdaterDownloadResult updaterDownloadInPlace(const char *url, const char *targetPath,
                                             const char *label, UpdaterProgressCallback progress,
                                             void *userData);

// Downloads to targetPath.tmp before replacing targetPath.
// Unmounts RomFS for executable replacement; remounts only on failure.
UpdaterDownloadResult updaterDownloadToFile(const char *url, const char *targetPath,
                                            const char *label, UpdaterProgressCallback progress,
                                            void *userData);

// Installs a downloaded CIA; *titleId receives its title ID, or 0 if unreadable.
bool updaterInstallCia(const char *ciaPath, u64 *titleId);
// Jumps to an installed SD title; false if the jump was refused.
bool updaterLaunchTitle(u64 titleId);

// Set from argv[0] at startup; the .3dsx update replaces that file.
void updaterSetExecutablePath(const char *path);
const char *updaterExecutablePath(void);

#endif
