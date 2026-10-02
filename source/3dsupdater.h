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

enum UpdaterAssetCheckState {
    UPDATER_ASSET_CHECK_IDLE,
    UPDATER_ASSET_CHECK_CHECKING,
    UPDATER_ASSET_CHECK_READY,
    UPDATER_ASSET_CHECK_FAILED,
};

struct UpdaterAppInfo {
    char tag[32];
    bool available;
};

enum UpdaterAppUpdateState {
    UPDATER_APP_UPDATE_IDLE,
    UPDATER_APP_UPDATE_CHECKING,
    UPDATER_APP_UPDATE_CURRENT,
    UPDATER_APP_UPDATE_AVAILABLE,
    UPDATER_APP_UPDATE_FAILED,
};

// The cache manifest is fetched asynchronously when the page opens. App
// metadata comes from GitHub's latest-release API and is not part of it.
bool updaterBeginAssetCheck(void);
UpdaterAssetCheckState updaterCacheCheckState(void);
u32 updaterAssetCheckRevision(void);
const UpdaterCacheManifest *updaterCacheManifest(void);

bool updaterBeginAppUpdateCheck(void);
UpdaterAppUpdateState updaterAppUpdateState(void);
const UpdaterAppInfo *updaterAppUpdateInfo(void);
u32 updaterAppUpdateCheckRevision(void);

// Builds an HTTPS raw-content URL for a dist-relative asset path; the path is
// percent encoded.
bool updaterBuildAssetUrl(const char *relativePath, char *output, size_t outputSize);

// Downloads via targetPath.part and replaces targetPath only after a complete,
// successful download. Replacing the running .3dsx unmounts RomFS; it is
// remounted only if the swap fails.
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
