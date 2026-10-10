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
#include <ctime>
#include <limits.h>

#define UPDATER_ASSET_BASE_URL "https://raw.githubusercontent.com/matbo87/snes9x_3ds-assets/master/dist/"
#define UPDATER_CACHE_MANIFEST_URL UPDATER_ASSET_BASE_URL "thumbnail-caches.txt"
#define UPDATER_GAME_MANIFEST_URL UPDATER_ASSET_BASE_URL "game-assets.txt"
#define UPDATER_APP_RELEASE_API_URL "https://api.github.com/repos/matbo87/snes9x_3ds/releases/latest"
#define UPDATER_GITHUB_API_HEADERS "Accept: application/vnd.github+json"
#define UPDATER_COPY_CHUNK (64 * 1024)

namespace {

struct FileRequest {
    bool done;
    bool success;
    bool transportFailed;
    int statusCode;
    // In-place downloads only.
    const char *targetPath;
    const Http3dsProgress *progress;
    bool written;
};

const char *const assetGroupPaths[UPDATER_ASSET_GROUP_COUNT] = {
    "backgrounds/game_screen",
    "backgrounds/second_screen_cover_art",
    "backgrounds/second_screen_console/eu_jp",
    "backgrounds/second_screen_console/us",
    "cheats",
};

static char updaterExecutable[PATH_MAX] = {};

struct SyncCache {
    UpdaterSyncState state[UPDATER_SYNC_PART_COUNT];
    UpdaterAppInfo app;
    UpdaterCacheManifest caches;
    char *games;   // cached game-assets.txt
    Http3dsProgress progress[UPDATER_SYNC_PART_COUNT];
};

static SyncCache syncCache = {};

static void finishSyncPart(UpdaterSyncPart part, bool ok)
{
    if(ok)
        syncCache.state[part] = UPDATER_SYNC_DONE;
    else
        syncCache.state[part] = syncCache.progress[part].cancelRequested
            ? UPDATER_SYNC_NONE : UPDATER_SYNC_FAILED;
}

static void onFileResponse(const Http3dsResponse *response, void *data)
{
    FileRequest *request = static_cast<FileRequest *>(data);
    request->success = response->success;
    request->transportFailed = response->transportFailed;
    request->statusCode = response->httpStatusCode;
    request->done = true;
}

static void onInPlaceResponse(const Http3dsResponse *response, void *data)
{
    onFileResponse(response, data);

    FileRequest *request = static_cast<FileRequest *>(data);
    if(request->progress->cancelRequested
       || !response->success || !response->body || !response->bodyLength)
        return;

    FILE *file = file3dsOpen(request->targetPath, "wb");
    if(!file) {
        log3dsWrite("[UPDATER] could not open %s (errno %d)", request->targetPath, errno);
        return;
    }

    request->written = fwrite(response->body, 1, response->bodyLength, file) == response->bodyLength;
    request->written = file3dsClose(file) == 0 && request->written;
    if(request->written)
        file3dsSetFileInfo(request->targetPath, { true, (u64)time(NULL) });
}

// Waits for completion after cancellation.
static bool waitForRequest(const FileRequest& request, Http3dsProgress *httpProgress,
                           const char *label, UpdaterProgressCallback progress, void *userData)
{
    bool cancelled = false;

    while(!request.done && aptMainLoop()) {
        http3dsDrainCompletions();
        // Completion takes precedence over cancellation.
        if(request.done)
            break;
        if(progress && !progress(label, httpProgress->downloaded, httpProgress->total, userData)) {
            cancelled = true;
            httpProgress->cancelRequested = true;
        }
        svcSleepThread(1000 * 1000);
    }

    if(!request.done && !cancelled)
        httpProgress->cancelRequested = true;

    while(!request.done) {
        http3dsDrainCompletions();
        if(!request.done)
            svcSleepThread(1000 * 1000);
    }
    return cancelled;
}

static UpdaterDownloadResult requestResult(const FileRequest& request, bool cancelled)
{
    if(cancelled)
        return UPDATER_DOWNLOAD_CANCELLED;
    if(!request.success) {
        // Check HTTP status first; CURLOPT_FAILONERROR also sets transportFailed.
        if(request.statusCode == 404)
            return UPDATER_DOWNLOAD_NOT_FOUND;
        return request.transportFailed ? UPDATER_DOWNLOAD_NO_CONNECTION : UPDATER_DOWNLOAD_FAILED;
    }
    return UPDATER_DOWNLOAD_OK;
}

static UpdaterDownloadResult waitForFile(const char *url, const char *temporaryPath, bool gzipped,
                                         const char *label, UpdaterProgressCallback progress,
                                         void *userData)
{
    Http3dsProgress httpProgress = {};
    FileRequest request = {};

    if(!http3dsDownloadFile(url, temporaryPath, gzipped, &httpProgress, onFileResponse, &request))
        return UPDATER_DOWNLOAD_FAILED;

    bool cancelled = waitForRequest(request, &httpProgress, label, progress, userData);
    return requestResult(request, cancelled);
}

// Generated manifests have no empty fields.
static int manifestSplitFields(char *line, char **fields, int maxFields)
{
    int count = 0;
    for(char *field = strtok(line, "\t"); field && count < maxFields;
        field = strtok(NULL, "\t"))
        fields[count++] = field;
    return count;
}

// Returns the next line, or NULL at EOF.
static const char *manifestNextLine(const char *text, char *line, size_t lineSize)
{
    const char *end = strchr(text, '\n');
    size_t length = end ? (size_t)(end - text) : strlen(text);
    while(length && (text[length - 1] == '\r' || text[length - 1] == ' '))
        --length;
    if(length >= lineSize)
        length = 0;   // skip oversized rows rather than truncate
    memcpy(line, text, length);
    line[length] = '\0';
    return end ? end + 1 : NULL;
}

static Setting::ThumbnailMode thumbnailModeForSource(const char *source)
{
    for(int i = 0; i < (int)Setting::ThumbnailMode::Count; ++i) {
        Setting::ThumbnailMode mode = (Setting::ThumbnailMode)i;
        const char *name = file3dsThumbnailCacheName(mode);
        char path[96];
        if(name && snprintf(path, sizeof(path), "thumbnails/%s.gz", name) < (int)sizeof(path)
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

// NULL gameName accepts any complete row.
static bool parseGameAssets(const char *text, const char *gameName, UpdaterGameAssets *assets)
{
    memset(assets, 0, sizeof(*assets));
    char line[512];

    for(const char *next = text; next && *next;) {
        next = manifestNextLine(next, line, sizeof(line));
        if(!line[0] || line[0] == '#')
            continue;

        char *fields[1 + UPDATER_ASSET_GROUP_COUNT];
        if(manifestSplitFields(line, fields, 1 + UPDATER_ASSET_GROUP_COUNT)
               != 1 + UPDATER_ASSET_GROUP_COUNT
           || (gameName && strcmp(fields[0], gameName)))
            continue;

        for(int i = 0; i < UPDATER_ASSET_GROUP_COUNT; ++i)
            assets->published[i] = (u32)strtoul(fields[i + 1], NULL, 10);
        return true;
    }
    return false;
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
        && parseCacheManifest(response->body, &syncCache.caches);
    if(ok)
        log3dsWrite("[UPDATER] cache manifest: %d variant(s)", syncCache.caches.count);
    else
        log3dsWrite("[UPDATER] cache manifest failed: success=%d status=%d",
                    response->success, response->httpStatusCode);

    finishSyncPart(UPDATER_SYNC_CACHES, ok);
}

static void onGameManifestResponse(const Http3dsResponse *response, void *data)
{
    (void)data;

    UpdaterGameAssets probe;
    bool ok = response->success && response->body && response->bodyLength
        && parseGameAssets(response->body, NULL, &probe);
    if(ok) {
        free(syncCache.games);
        syncCache.games = (char *)malloc(response->bodyLength + 1);
        ok = syncCache.games != NULL;
        if(ok) {
            memcpy(syncCache.games, response->body, response->bodyLength);
            syncCache.games[response->bodyLength] = '\0';
        }
    }

    if(ok)
        log3dsWrite("[UPDATER] game manifest: %lu bytes", (unsigned long)response->bodyLength);
    else
        log3dsWrite("[UPDATER] game manifest failed: success=%d status=%d",
                    response->success, response->httpStatusCode);

    finishSyncPart(UPDATER_SYNC_GAMES, ok);
}

static void onAppUpdateCheckResponse(const Http3dsResponse *response, void *data)
{
    (void)data;

    UpdaterAppInfo *info = &syncCache.app;
    memset(info, 0, sizeof(*info));

    bool hasTag = response->success && response->body
        && jsonReadString(response->body, "tag_name", info->tag, sizeof(info->tag));
    bool valid = false;
    if(hasTag)
        info->available = versionIsNewer(info->tag, &valid);

    if(valid)
        log3dsWrite("[UPDATER] GitHub release %s; app is %s", info->tag,
                    info->available ? "out of date" : "current");
    else if(hasTag)
        log3dsWrite("[UPDATER] GitHub release tag %s is not a version this build can compare",
                    info->tag);
    else
        log3dsWrite("[UPDATER] GitHub release check failed: success=%d status=%d bytes=%lu detail=%.160s",
                    response->success, response->httpStatusCode,
                    (unsigned long)response->bodyLength,
                    response->body ? response->body : "<none>");

    finishSyncPart(UPDATER_SYNC_APP, valid);
}

} // namespace

const char *updaterAssetGroupPath(UpdaterAssetGroup group)
{
    return group < UPDATER_ASSET_GROUP_COUNT ? assetGroupPaths[group] : "";
}

void updaterBeginSync(void)
{
    for(int i = 0; i < UPDATER_SYNC_PART_COUNT; ++i) {
        UpdaterSyncPart part = (UpdaterSyncPart)i;
        if(syncCache.state[part] == UPDATER_SYNC_DONE || syncCache.state[part] == UPDATER_SYNC_PENDING)
            continue;

        // Clear stale cancellation even if queueing fails.
        syncCache.progress[part].cancelRequested = false;
        bool queued = false;
        switch(part) {
        case UPDATER_SYNC_APP:
            log3dsWrite("[UPDATER] checking GitHub latest release");
            queued = http3dsRequest(UPDATER_APP_RELEASE_API_URL, NULL, NULL,
                                    UPDATER_GITHUB_API_HEADERS, &syncCache.progress[part],
                                    onAppUpdateCheckResponse, NULL);
            break;
        case UPDATER_SYNC_CACHES:
            log3dsWrite("[UPDATER] fetching cache manifest");
            queued = http3dsGet(UPDATER_CACHE_MANIFEST_URL, &syncCache.progress[part],
                                onCacheManifestResponse, NULL);
            break;
        case UPDATER_SYNC_GAMES:
            log3dsWrite("[UPDATER] fetching game manifest");
            queued = http3dsGet(UPDATER_GAME_MANIFEST_URL, &syncCache.progress[part],
                                onGameManifestResponse, NULL);
            break;
        default:
            break;
        }

        if(queued)
            syncCache.state[part] = UPDATER_SYNC_PENDING;
        else
            finishSyncPart(part, false);
    }
}

void updaterCancelSync(void)
{
    for(int i = 0; i < UPDATER_SYNC_PART_COUNT; ++i)
        if(syncCache.state[i] == UPDATER_SYNC_PENDING)
            syncCache.progress[i].cancelRequested = true;
}

UpdaterSyncState updaterSyncState(UpdaterSyncPart part)
{
    return part < UPDATER_SYNC_PART_COUNT ? syncCache.state[part] : UPDATER_SYNC_NONE;
}

bool updaterSyncPending(void)
{
    for(int i = 0; i < UPDATER_SYNC_PART_COUNT; ++i)
        if(syncCache.state[i] == UPDATER_SYNC_PENDING)
            return true;
    return false;
}

const UpdaterAppInfo *updaterAppUpdateInfo(void)
{
    return &syncCache.app;
}

const UpdaterCacheManifest *updaterCacheManifest(void)
{
    return &syncCache.caches;
}

void updaterFindGameAssets(const char *gameName, UpdaterGameAssets *assets)
{
    if(syncCache.state[UPDATER_SYNC_GAMES] == UPDATER_SYNC_DONE && gameName)
        parseGameAssets(syncCache.games, gameName, assets);
    else
        memset(assets, 0, sizeof(*assets));
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

UpdaterDownloadResult updaterDownloadInPlace(const char *url, const char *targetPath,
                                             const char *label, UpdaterProgressCallback progress,
                                             void *userData)
{
    if(!url || !targetPath)
        return UPDATER_DOWNLOAD_FAILED;

    Http3dsProgress httpProgress = {};
    FileRequest request = {};
    request.targetPath = targetPath;
    request.progress = &httpProgress;

    if(!http3dsGet(url, &httpProgress, onInPlaceResponse, &request))
        return UPDATER_DOWNLOAD_FAILED;

    bool cancelled = waitForRequest(request, &httpProgress, label, progress, userData);
    // A completed write takes precedence over cancellation.
    if(request.written)
        return UPDATER_DOWNLOAD_OK;

    UpdaterDownloadResult result = requestResult(request, cancelled);
    return result == UPDATER_DOWNLOAD_OK ? UPDATER_DOWNLOAD_FAILED : result;
}

UpdaterDownloadResult updaterDownloadToFile(const char *url, const char *targetPath, bool gzipped,
                                            const char *label, UpdaterProgressCallback progress,
                                            void *userData)
{
    if(!url || !targetPath)
        return UPDATER_DOWNLOAD_FAILED;
    char temporaryPath[PATH_MAX];
    snprintf(temporaryPath, sizeof(temporaryPath), "%s.tmp", targetPath);
    remove(temporaryPath);
    // The transport removes the temporary file on failure.
    UpdaterDownloadResult result = waitForFile(url, temporaryPath, gzipped, label, progress, userData);
    if(result != UPDATER_DOWNLOAD_OK)
        return result;

    // Unmount RomFS to release the running .3dsx.
    bool isExecutable = updaterExecutable[0] && !strcmp(targetPath, updaterExecutable);
    if(isExecutable && settings3DS.isRomFsLoaded) {
        romfsExit();
        settings3DS.isRomFsLoaded = false;
    }
    if(file3dsReplaceFile(temporaryPath, targetPath))
        return UPDATER_DOWNLOAD_OK;

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
