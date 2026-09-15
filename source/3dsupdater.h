#ifndef _3DSUPDATER_H_
#define _3DSUPDATER_H_

#include <3ds.h>

#include <stddef.h>

// Called on the main thread while a request is pending; return false to cancel.
typedef bool (*UpdaterProgressCallback)(const char *label, u32 downloaded,
                                        u32 total, void *userData);

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

// App metadata comes from GitHub's latest-release API; package names are stable.
bool updaterBeginAppUpdateCheck(void);
UpdaterAppUpdateState updaterAppUpdateState(void);
const UpdaterAppInfo *updaterAppUpdateInfo(void);
u32 updaterAppUpdateCheckRevision(void);

// Downloads via targetPath.part and replaces targetPath only after a complete,
// successful download; expectedSize=0 skips the size check. Replacing the
// running .3dsx unmounts RomFS; it is remounted only if the swap fails.
bool updaterDownloadToFile(const char *url, const char *targetPath, u32 expectedSize,
                           const char *label, UpdaterProgressCallback progress, void *userData);

// Installs a downloaded CIA; *titleId receives its title ID, or 0 if unreadable.
bool updaterInstallCia(const char *ciaPath, u64 *titleId);
// Jumps to an installed SD title; false if the jump was refused.
bool updaterLaunchTitle(u64 titleId);

// Set from argv[0] at startup; the .3dsx update replaces that file.
void updaterSetExecutablePath(const char *path);
const char *updaterExecutablePath(void);

#endif
