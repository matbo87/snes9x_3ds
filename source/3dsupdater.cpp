#include "3dsupdater.h"

#include "3dshttp.h"
#include "3dsfiles.h"
#include "3dslog.h"
#include "3dssettings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <limits.h>

#define UPDATER_ASSET_BASE_URL "https://raw.githubusercontent.com/matbo87/snes9x_3ds-assets/master/dist/"
#define UPDATER_CACHE_MANIFEST_URL UPDATER_ASSET_BASE_URL "thumbnail-caches.txt"
#define UPDATER_APP_RELEASE_API_URL "https://api.github.com/repos/matbo87/snes9x_3ds/releases/latest"
#define UPDATER_GITHUB_API_HEADERS "Accept: application/vnd.github+json"
#define UPDATER_COPY_CHUNK (64 * 1024)

namespace {

struct FileRequest {
    bool done;
    bool success;
    bool transportFailed;
    int statusCode;
};

static char updaterExecutable[PATH_MAX] = {};

struct AppUpdateCheck {
    UpdaterAppUpdateState state;
    UpdaterAppInfo info;
    u32 revision;
};

static AppUpdateCheck appUpdateCheck = { UPDATER_APP_UPDATE_IDLE, {}, 0 };

struct AssetCheck {
    UpdaterAssetCheckState cacheState;
    UpdaterCacheManifest cache;
    u32 revision;
};

static AssetCheck assetCheck = {};

static void onFileResponse(const Http3dsResponse *response, void *data)
{
    FileRequest *request = static_cast<FileRequest *>(data);
    request->success = response->success;
    request->transportFailed = response->transportFailed;
    request->statusCode = response->httpStatusCode;
    request->done = true;
}

static UpdaterDownloadResult waitForFile(const char *url, const char *temporaryPath,
                                         const char *label, UpdaterProgressCallback progress,
                                         void *userData)
{
    Http3dsProgress httpProgress = {};
    FileRequest request = {};
    if(!http3dsDownloadFile(url, temporaryPath, &httpProgress, onFileResponse, &request))
        return UPDATER_DOWNLOAD_FAILED;

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

    if(cancelled)
        return UPDATER_DOWNLOAD_CANCELLED;
    if(!request.success) {
        // A 404 also sets transportFailed, because CURLOPT_FAILONERROR aborts
        // the transfer; the status is what tells the two apart.
        if(request.statusCode == 404)
            return UPDATER_DOWNLOAD_NOT_FOUND;
        return request.transportFailed ? UPDATER_DOWNLOAD_NO_CONNECTION : UPDATER_DOWNLOAD_FAILED;
    }
    return UPDATER_DOWNLOAD_OK;
}

// Manifest rows are tab separated; a field is never empty, so strtok is enough.
static int manifestSplitFields(char *line, char **fields, int maxFields)
{
    int count = 0;
    for(char *field = strtok(line, "\t"); field && count < maxFields;
        field = strtok(NULL, "\t"))
        fields[count++] = field;
    return count;
}

// Copies one line out of text, stopping at the newline; returns the next line.
static const char *manifestNextLine(const char *text, char *line, size_t lineSize)
{
    const char *end = strchr(text, '\n');
    size_t length = end ? (size_t)(end - text) : strlen(text);
    while(length && (text[length - 1] == '\r' || text[length - 1] == ' '))
        --length;
    if(length >= lineSize)
        length = 0;   // overlong rows are skipped, not truncated into a match
    memcpy(line, text, length);
    line[length] = '\0';
    return end ? end + 1 : NULL;
}

// Matches the path the app opens, so a newer cache this build has no type for
// maps to None and is never offered.
static Setting::ThumbnailMode thumbnailModeForSource(const char *source)
{
    for(int i = 0; i < (int)Setting::ThumbnailMode::Count; ++i) {
        Setting::ThumbnailMode mode = (Setting::ThumbnailMode)i;
        const char *name = file3dsThumbnailCacheName(mode);
        char path[96];
        if(name && snprintf(path, sizeof(path), "thumbnails/%s", name) < (int)sizeof(path)
           && !strcmp(source, path))
            return mode;
    }
    return Setting::ThumbnailMode::None;
}

static bool parseCacheManifest(const char *text, UpdaterCacheManifest *manifest)
{
    memset(manifest, 0, sizeof(*manifest));
    char line[256];
    for(const char *next = text; next && *next;) {
        next = manifestNextLine(next, line, sizeof(line));
        if(!line[0] || line[0] == '#')
            continue;
        char *fields[2];
        if(manifestSplitFields(line, fields, 2) != 2
           || manifest->count >= UPDATER_MAX_CACHE_VARIANTS)
            continue;
        UpdaterCacheVariant *variant = &manifest->variants[manifest->count];
        variant->mode = thumbnailModeForSource(fields[0]);
        snprintf(variant->source, sizeof(variant->source), "%s", fields[0]);
        variant->published = (u32)strtoul(fields[1], NULL, 10);
        if(variant->mode != Setting::ThumbnailMode::None && variant->published)
            ++manifest->count;
    }
    return manifest->count > 0;
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

static void onCacheManifestResponse(const Http3dsResponse *response, void *data)
{
    (void)data;
    bool ok = response->success && response->body
        && parseCacheManifest(response->body, &assetCheck.cache);
    assetCheck.cacheState = ok ? UPDATER_ASSET_CHECK_READY : UPDATER_ASSET_CHECK_FAILED;
    if(ok)
        log3dsWrite("[UPDATER] cache manifest: %d variant(s)", assetCheck.cache.count);
    else
        log3dsWrite("[UPDATER] cache manifest failed: success=%d status=%d",
                    response->success, response->httpStatusCode);
    ++assetCheck.revision;
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

bool updaterBeginAssetCheck(void)
{
    if(assetCheck.cacheState == UPDATER_ASSET_CHECK_CHECKING)
        return true;

    memset(&assetCheck.cache, 0, sizeof(assetCheck.cache));
    log3dsWrite("[UPDATER] checking cache manifest");

    assetCheck.cacheState = http3dsGet(UPDATER_CACHE_MANIFEST_URL, onCacheManifestResponse, NULL)
        ? UPDATER_ASSET_CHECK_CHECKING : UPDATER_ASSET_CHECK_FAILED;

    ++assetCheck.revision;
    return assetCheck.cacheState == UPDATER_ASSET_CHECK_CHECKING;
}

UpdaterAssetCheckState updaterCacheCheckState(void)
{
    return assetCheck.cacheState;
}

u32 updaterAssetCheckRevision(void)
{
    return assetCheck.revision;
}

const UpdaterCacheManifest *updaterCacheManifest(void)
{
    return &assetCheck.cache;
}

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

bool updaterBuildAssetUrl(const char *relativePath, char *output, size_t outputSize)
{
    static const char base[] = UPDATER_ASSET_BASE_URL;
    if(!relativePath || !output || outputSize <= sizeof(base))
        return false;
    size_t used = snprintf(output, outputSize, "%s", base);
    for(const unsigned char *p = reinterpret_cast<const unsigned char *>(relativePath); *p; ++p) {
        bool safe = isalnum(*p) || *p == '/' || *p == '.' || *p == '-' || *p == '_';
        int written = safe
            ? snprintf(output + used, outputSize - used, "%c", *p)
            : snprintf(output + used, outputSize - used, "%%%02X", *p);
        if(written < 0 || (size_t)written >= outputSize - used) {
            output[0] = '\0';
            return false;
        }
        used += written;
    }
    return true;
}

UpdaterDownloadResult updaterDownloadToFile(const char *url, const char *targetPath,
                                            const char *label, UpdaterProgressCallback progress,
                                            void *userData)
{
    if(!url || !targetPath)
        return UPDATER_DOWNLOAD_FAILED;
    char temporaryPath[PATH_MAX];
    snprintf(temporaryPath, sizeof(temporaryPath), "%s.part", targetPath);
    remove(temporaryPath);
    UpdaterDownloadResult result = waitForFile(url, temporaryPath, label, progress, userData);
    if(result != UPDATER_DOWNLOAD_OK) {
        remove(temporaryPath);
        return result;
    }

    // RomFS keeps the running .3dsx open, which blocks renaming it (hb app update only).
    bool isExecutable = updaterExecutable[0] && !strcmp(targetPath, updaterExecutable);
    if(isExecutable && settings3DS.isRomFsLoaded) {
        romfsExit();
        settings3DS.isRomFsLoaded = false;
    }
    if(replaceFile(temporaryPath, targetPath))
        return UPDATER_DOWNLOAD_OK;

    remove(temporaryPath);

    //  hb app update only
    if(isExecutable && !settings3DS.isRomFsLoaded)
        settings3DS.isRomFsLoaded = R_SUCCEEDED(romfsInit());
    return UPDATER_DOWNLOAD_FAILED;
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
