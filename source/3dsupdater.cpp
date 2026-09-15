#include "3dsupdater.h"

#include "3dshttp.h"
#include "3dslog.h"
#include "3dssettings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <limits.h>

#define UPDATER_APP_RELEASE_API_URL "https://api.github.com/repos/matbo87/snes9x_3ds/releases/latest"
#define UPDATER_GITHUB_API_HEADERS "Accept: application/vnd.github+json"
#define UPDATER_COPY_CHUNK (64 * 1024)

namespace {

struct FileRequest {
    bool done;
    bool success;
    u32 downloaded;
};

static char updaterExecutable[PATH_MAX] = {};

struct AppUpdateCheck {
    UpdaterAppUpdateState state;
    UpdaterAppInfo info;
    u32 revision;
};

static AppUpdateCheck appUpdateCheck = { UPDATER_APP_UPDATE_IDLE, {}, 0 };

static void onFileResponse(const Http3dsResponse *response, void *data)
{
    FileRequest *request = static_cast<FileRequest *>(data);
    request->success = response->success;
    request->downloaded = response->downloaded;
    request->done = true;
}

static bool waitForFile(const char *url, const char *temporaryPath,
                        u32 expectedSize, const char *label,
                        UpdaterProgressCallback progress, void *userData)
{
    Http3dsProgress httpProgress = {};
    FileRequest request = {};
    if(!http3dsDownloadFile(url, temporaryPath, &httpProgress, onFileResponse, &request))
        return false;

    bool cancelled = false;
    while(!request.done && aptMainLoop()) {
        http3dsDrainCompletions();
        if(progress && !progress(label, httpProgress.downloaded, httpProgress.total, userData)) {
            cancelled = true;
            httpProgress.cancelRequested = true;
        }
        svcSleepThread(1000 * 1000);
    }
    if(!request.done && !cancelled)
        httpProgress.cancelRequested = true;
    while(!request.done) {
        http3dsDrainCompletions();
        if(!request.done)
            svcSleepThread(1000 * 1000);
    }

    return !cancelled && request.success
        && (!expectedSize || request.downloaded == expectedSize);
}

// Release tags are ASCII version strings, so reject escaped JSON values.
static bool jsonReadString(const char *text, const char *key, char *output, size_t outputSize)
{
    if(!text || !key || !output || outputSize == 0)
        return false;

    char quotedKey[64];
    int quotedKeyLength = snprintf(quotedKey, sizeof(quotedKey), "\"%s\"", key);
    if(quotedKeyLength < 0 || (size_t)quotedKeyLength >= sizeof(quotedKey))
        return false;

    const char *match = text;
    while((match = strstr(match, quotedKey)) != NULL) {
        size_t backslashes = 0;
        for(const char *p = match; p > text && p[-1] == '\\'; --p)
            ++backslashes;
        if(backslashes & 1) {
            ++match;
            continue;
        }

        const char *value = match + quotedKeyLength;
        while(isspace((unsigned char)*value))
            ++value;
        if(*value++ != ':') {
            ++match;
            continue;
        }
        while(isspace((unsigned char)*value))
            ++value;
        if(*value++ != '\"') {
            ++match;
            continue;
        }

        const char *end = value;
        while(*end && *end != '\"' && *end != '\\'
              && (unsigned char)*end >= 0x20)
            ++end;
        size_t length = (size_t)(end - value);
        if(*end == '\"' && length < outputSize) {
            memcpy(output, value, length);
            output[length] = '\0';
            return true;
        }
        ++match;
    }
    return false;
}

static bool parseVersion(const char *value, int *major, int *minor, int *micro)
{
    if(!value)
        return false;
    int parsed = sscanf(value, "v%d.%d.%d", major, minor, micro);
    if(parsed < 2)
        parsed = sscanf(value, "%d.%d.%d", major, minor, micro);
    if(parsed < 2)
        return false;
    if(parsed == 2)
        *micro = 0;
    return *major >= 0 && *minor >= 0 && *micro >= 0;
}

// *valid is false when either version string does not parse.
static bool versionIsNewer(const char *candidate, bool *valid)
{
    int remoteMajor = 0, remoteMinor = 0, remoteMicro = 0;
    int localMajor = 0, localMinor = 0, localMicro = 0;
    char local[32];
    snprintf(local, sizeof(local), "%s", settings3dsGetAppVersion(""));
    *valid = parseVersion(candidate, &remoteMajor, &remoteMinor, &remoteMicro)
        && parseVersion(local, &localMajor, &localMinor, &localMicro);
    if(!*valid)
        return false;
    if(remoteMajor != localMajor) return remoteMajor > localMajor;
    if(remoteMinor != localMinor) return remoteMinor > localMinor;
    return remoteMicro > localMicro;
}

static void onAppUpdateCheckResponse(const Http3dsResponse *response, void *data)
{
    (void)data;
    memset(&appUpdateCheck.info, 0, sizeof(appUpdateCheck.info));
    bool hasTag = response->success && response->body
        && jsonReadString(response->body, "tag_name", appUpdateCheck.info.tag,
                          sizeof(appUpdateCheck.info.tag));
    bool valid = false;
    if(hasTag)
        appUpdateCheck.info.available = versionIsNewer(appUpdateCheck.info.tag, &valid);
    if(valid) {
        appUpdateCheck.state = appUpdateCheck.info.available
            ? UPDATER_APP_UPDATE_AVAILABLE : UPDATER_APP_UPDATE_CURRENT;
        log3dsWrite("[UPDATER] GitHub release %s; app is %s", appUpdateCheck.info.tag,
                    appUpdateCheck.info.available ? "out of date" : "current");
    } else if(hasTag) {
        appUpdateCheck.state = UPDATER_APP_UPDATE_FAILED;
        log3dsWrite("[UPDATER] GitHub release tag %s is not a version this build can compare",
                    appUpdateCheck.info.tag);
    } else {
        appUpdateCheck.state = UPDATER_APP_UPDATE_FAILED;
        log3dsWrite("[UPDATER] GitHub release check failed: success=%d status=%d bytes=%lu detail=%.160s",
                    response->success, response->httpStatusCode,
                    (unsigned long)response->bodyLength,
                    response->body ? response->body : "<none>");
    }
    ++appUpdateCheck.revision;
}

static bool replaceFile(const char *temporaryPath, const char *targetPath)
{
    char backup[PATH_MAX];
    snprintf(backup, sizeof(backup), "%s.previous", targetPath);
    remove(backup);

    FILE *target = fopen(targetPath, "rb");
    bool hadTarget = target != NULL;
    if(target)
        fclose(target);
    if(hadTarget && rename(targetPath, backup) != 0) {
        log3dsWrite("[UPDATER] rename %s -> %s failed (errno %d)", targetPath, backup, errno);
        return false;
    }
    if(rename(temporaryPath, targetPath) == 0) {
        remove(backup);
        return true;
    }

    log3dsWrite("[UPDATER] rename %s -> %s failed (errno %d)", temporaryPath, targetPath, errno);
    if(hadTarget)
        rename(backup, targetPath);
    return false;
}

} // namespace

bool updaterBeginAppUpdateCheck(void)
{
    if(appUpdateCheck.state == UPDATER_APP_UPDATE_CHECKING)
        return true;

    memset(&appUpdateCheck.info, 0, sizeof(appUpdateCheck.info));
    appUpdateCheck.state = UPDATER_APP_UPDATE_CHECKING;
    log3dsWrite("[UPDATER] checking GitHub latest release");
    if(http3dsRequest(UPDATER_APP_RELEASE_API_URL, NULL, NULL, UPDATER_GITHUB_API_HEADERS,
                      onAppUpdateCheckResponse, NULL))
        return true;

    appUpdateCheck.state = UPDATER_APP_UPDATE_FAILED;
    log3dsWrite("[UPDATER] GitHub release check could not be queued");
    ++appUpdateCheck.revision;
    return false;
}

UpdaterAppUpdateState updaterAppUpdateState(void)
{
    return appUpdateCheck.state;
}

const UpdaterAppInfo *updaterAppUpdateInfo(void)
{
    return &appUpdateCheck.info;
}

u32 updaterAppUpdateCheckRevision(void)
{
    return appUpdateCheck.revision;
}

bool updaterDownloadToFile(const char *url, const char *targetPath, u32 expectedSize,
                           const char *label, UpdaterProgressCallback progress, void *userData)
{
    if(!url || !targetPath)
        return false;
    char temporaryPath[PATH_MAX];
    snprintf(temporaryPath, sizeof(temporaryPath), "%s.part", targetPath);
    remove(temporaryPath);
    if(!waitForFile(url, temporaryPath, expectedSize, label, progress, userData)) {
        remove(temporaryPath);
        return false;
    }

    // RomFS keeps the running .3dsx open, which blocks renaming it.
    bool isExecutable = updaterExecutable[0] && !strcmp(targetPath, updaterExecutable);
    if(isExecutable && settings3DS.isRomFsLoaded) {
        romfsExit();
        settings3DS.isRomFsLoaded = false;
    }
    if(replaceFile(temporaryPath, targetPath))
        return true;

    remove(temporaryPath);
    if(isExecutable && !settings3DS.isRomFsLoaded)
        settings3DS.isRomFsLoaded = R_SUCCEEDED(romfsInit());
    return false;
}

bool updaterInstallCia(const char *ciaPath, u64 *titleId)
{
    if(titleId)
        *titleId = 0;
    if(!ciaPath)
        return false;
    // FS paths are relative to the SD archive.
    if(!strncmp(ciaPath, "sdmc:", 5))
        ciaPath += 5;

    Handle file = 0;
    if(R_FAILED(FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                                        fsMakePath(PATH_ASCII, ciaPath), FS_OPEN_READ, 0)))
        return false;

    u64 length = 0;
    u8 *buffer = static_cast<u8 *>(malloc(UPDATER_COPY_CHUNK));
    // amExit must pair with a successful amInit, or the AM refcount underflows.
    bool amReady = buffer && R_SUCCEEDED(FSFILE_GetSize(file, &length)) && length > 0
        && R_SUCCEEDED(amInit());

    bool ok = false;
    Handle ciaHandle = 0;
    if(amReady) {
        // An unreadable title ID stays 0; the install itself can still succeed.
        AM_TitleEntry entry = {};
        if(titleId && R_SUCCEEDED(AM_GetCiaFileInfo(MEDIATYPE_SD, &entry, file)))
            *titleId = entry.titleID;
        ok = R_SUCCEEDED(AM_StartCiaInstall(MEDIATYPE_SD, &ciaHandle));
    }
    for(u64 offset = 0; ok && offset < length;) {
        u32 wanted = (u32)(length - offset < UPDATER_COPY_CHUNK ? length - offset : UPDATER_COPY_CHUNK);
        u32 read = 0, written = 0;
        ok = R_SUCCEEDED(FSFILE_Read(file, &read, offset, buffer, wanted)) && read == wanted
            && R_SUCCEEDED(FSFILE_Write(ciaHandle, &written, offset, buffer, read, FS_WRITE_FLUSH))
            && written == read;
        offset += read;
    }
    if(ciaHandle) {
        if(ok)
            ok = R_SUCCEEDED(AM_FinishCiaInstall(ciaHandle));
        else
            AM_CancelCIAInstall(ciaHandle);
    }
    if(amReady)
        amExit();
    free(buffer);
    FSFILE_Close(file);
    return ok;
}

bool updaterLaunchTitle(u64 titleId)
{
    u8 param[0x300] = {};
    u8 hmac[0x20] = {};
    return R_SUCCEEDED(APT_PrepareToDoApplicationJump(0, titleId, MEDIATYPE_SD))
        && R_SUCCEEDED(APT_DoApplicationJump(param, sizeof(param), hmac));
}

void updaterSetExecutablePath(const char *path)
{
    snprintf(updaterExecutable, sizeof(updaterExecutable), "%s", path ? path : "");
}

const char *updaterExecutablePath(void)
{
    return updaterExecutable;
}
