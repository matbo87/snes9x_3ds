#include "3dsretroachievements.h"
#include "3dslog.h"
#include "3dssettings.h"
#include "3dsgpu.h"       // SGPU_TEXTURE_ID, needed by 3dsui_notif.h
#include "3dsui_notif.h"

#include "rc_client.h"
#include "rc_hash.h"

#include "memmap.h"

#include <3ds.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static rc_client_t *raClient = NULL;

static bool raLoginSucceeded = false;
static char raLastError[160] = {0};

#define RA_RESPONSE_INITIAL_CAP (128 * 1024)
// Timeout for status/body waits; httpcBeginRequest has no timeout.
#define RA_HTTP_TIMEOUT_NS (5ULL * 1000 * 1000 * 1000)
static char *raResponseBuf = NULL;
static u32   raResponseCap = 0;

// RC_CLIENT_ACHIEVEMENT_WARNING_ID in rc_client.c
#define RA_WARNING_ACHIEVEMENT_ID 101000001u

//---------------------------------------------------------
// Memory read callback.
//
// RA SNES address map (rcheevos consoleinfo.c):
//   0x000000 - 0x01FFFF  System RAM (128 KB)  -> Memory.RAM   ($7E0000 WRAM)
//   0x020000 - 0x09FFFF  Cartridge RAM (512K) -> Memory.SRAM  (SA-1 BW-RAM;
//                                                clamped to actual SRAM size)
//   0x0A0000 - 0x0A07FF  SA-1 I-RAM   (2 KB)  -> Memory.FillRAM + 0x3000
//
// SA1.BWRAM aliases Memory.SRAM.
// The SA-1 internal I-RAM lives at Memory.FillRAM[0x3000],
// the base used for I-RAM DMA and the SA-1 memory map in sa1.cpp.
//---------------------------------------------------------

#define RA_WRAM_START    0x000000u
#define RA_WRAM_END      0x020000u  // exclusive (128 KB)
#define RA_SRAM_START    0x020000u
#define RA_SRAM_END      0x0A0000u  // exclusive (512 KB region)
#define RA_SA1IRAM_START 0x0A0000u
#define RA_SA1IRAM_END   0x0A0800u  // exclusive (2 KB)
#define SA1_IRAM_OFFSET  0x3000u    // I-RAM base within Memory.FillRAM
#define SA1_IRAM_SIZE    0x0800u    // 2 KB

// Actual cartridge SRAM size in bytes (matches memmap.cpp's computation).
static u32 raSramSize()
{
    return Memory.SRAMSize ? (u32)((1 << (Memory.SRAMSize + 3)) * 128) : 0;
}

static u32 raReadMemory(u32 address, u8 *buffer, u32 numBytes, rc_client_t *client)
{
    (void)client;

    if(address < RA_WRAM_END) {
        if(!Memory.RAM)
            return 0;
        u32 avail = RA_WRAM_END - address;
        if(numBytes > avail) numBytes = avail;
        memcpy(buffer, Memory.RAM + address, numBytes);
        return numBytes;
    }

    if(address >= RA_SRAM_START && address < RA_SRAM_END) {
        u32 offset = address - RA_SRAM_START;
        u32 size = raSramSize();
        if(!Memory.SRAM || offset >= size)
            return 0;
        u32 avail = size - offset;
        if(numBytes > avail) numBytes = avail;
        memcpy(buffer, Memory.SRAM + offset, numBytes);
        return numBytes;
    }

    if(address >= RA_SA1IRAM_START && address < RA_SA1IRAM_END) {
        if(!Memory.FillRAM)
            return 0;
        u32 offset = address - RA_SA1IRAM_START;
        u32 avail = SA1_IRAM_SIZE - offset;
        if(numBytes > avail) numBytes = avail;
        memcpy(buffer, Memory.FillRAM + SA1_IRAM_OFFSET + offset, numBytes);
        return numBytes;
    }

    return 0; // unmapped
}

//---------------------------------------------------------
// Network transport. The worker only performs httpc; 
// rc_client callbacks are drained on the main thread.
// Synchronous menu/load paths use raSyncMode
//---------------------------------------------------------

typedef struct RaResponse {
    rc_client_server_callback_t callback;
    void *callbackData;
    char *body;
    u32 bodyLength;
    int httpStatusCode;
    struct RaResponse *next;
} RaResponse;

typedef struct RaRequest {
    char *url;
    char *postData;
    char *contentType;
    RaResponse *response;
    rc_client_server_callback_t callback;
    void *callbackData;
    struct RaRequest *next;
} RaRequest;

static Thread     raWorker = NULL;
static bool       raWorkerRunning = false;
static bool       raSyncMode = false;
static char       raUserAgent[256] = {0};
static LightLock  raHttpLock;
static LightLock  raRequestLock;
static CondVar    raRequestCond;
static RaRequest  *raRequestHead = NULL, *raRequestTail = NULL;
static LightLock  raCompletionLock;
static RaResponse *raCompletionHead = NULL, *raCompletionTail = NULL;

static char *raStrdup(const char *s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if(p)
        memcpy(p, s, n);
    return p;
}

static void raFreeRequest(RaRequest *req)
{
    if(!req)
        return;
    free(req->url);
    free(req->postData);
    free(req->contentType);
    free(req->response); // struct only: an unprocessed request has no body yet
    free(req);
}

static void raFreeResponse(RaResponse *resp)
{
    free(resp->body);
    free(resp);
}

static void raQueueCompletion(RaResponse *resp)
{
    resp->next = NULL;
    LightLock_Lock(&raCompletionLock);
    if(raCompletionTail)
        raCompletionTail->next = resp;
    else
        raCompletionHead = resp;
    raCompletionTail = resp;
    LightLock_Unlock(&raCompletionLock);
}

static RaResponse *raDetachCompletions()
{
    LightLock_Lock(&raCompletionLock);
    RaResponse *resp = raCompletionHead;
    raCompletionHead = raCompletionTail = NULL;
    LightLock_Unlock(&raCompletionLock);
    return resp;
}

static void raQueueRequest(RaRequest *req)
{
    LightLock_Lock(&raRequestLock);
    if(raRequestTail)
        raRequestTail->next = req;
    else
        raRequestHead = req;
    raRequestTail = req;
    CondVar_Signal(&raRequestCond);
    LightLock_Unlock(&raRequestLock);
}

static RaRequest *raPopRequestLocked()
{
    RaRequest *req = raRequestHead;
    if(req) {
        raRequestHead = req->next;
        if(!raRequestHead)
            raRequestTail = NULL;
    }
    return req;
}

// httpcDownloadData with a timeout per receive.
static Result raDownloadDataTimeout(httpcContext *context, u8 *buffer, u32 size, u32 *downloadedsize, u64 timeout)
{
    if(downloadedsize)
        *downloadedsize = 0;

    u32 dlStart = 0;
    Result ret = httpcGetDownloadSizeState(context, &dlStart, NULL);
    if(R_FAILED(ret))
        return ret;

    u32 pos = 0;
    Result dlret = (Result)HTTPC_RESULTCODE_DOWNLOADPENDING;
    while(pos < size && dlret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) {
        dlret = httpcReceiveDataTimeout(context, &buffer[pos], size - pos, timeout);

        u32 dlPos = 0;
        ret = httpcGetDownloadSizeState(context, &dlPos, NULL);
        if(R_FAILED(ret))
            return ret;

        pos = dlPos - dlStart;
    }

    if(downloadedsize)
        *downloadedsize = pos;
    return dlret;
}

// Caller holds raHttpLock.
static bool raHttpPerform(const char *url, const char *postData, const char *contentType,
                          char **bufPtr, u32 *capPtr, u32 initialCap, u32 *lenOut, int *statusOut)
{
    *lenOut = 0;
    *statusOut = 0;

    char urlBuf[512];
    if(strncmp(url, "https://", 8) == 0) {
        snprintf(urlBuf, sizeof(urlBuf), "http://%s", url + 8);
        url = urlBuf;
    }

    httpcContext context;
    HTTPC_RequestMethod method = postData ? HTTPC_METHOD_POST : HTTPC_METHOD_GET;
    if(R_FAILED(httpcOpenContext(&context, method, url, 1)))
        return false;

    httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");
    httpcAddRequestHeaderField(&context, "User-Agent", raUserAgent);

    if(postData) {
        httpcAddRequestHeaderField(&context, "Content-Type",
            contentType ? contentType : "application/x-www-form-urlencoded");
        httpcAddPostDataRaw(&context, (const u32 *)postData, (u32)strlen(postData));
    }

    if(R_FAILED(httpcBeginRequest(&context))) {
        httpcCloseContext(&context);
        return false;
    }

    u32 statusCode = 0;
    if(R_FAILED(httpcGetResponseStatusCodeTimeout(&context, &statusCode, RA_HTTP_TIMEOUT_NS))) {
        httpcCancelConnection(&context);
        httpcCloseContext(&context);
        return false;
    }

    if(!*bufPtr) {
        *bufPtr = (char *)malloc(initialCap);
        if(!*bufPtr) {
            httpcCancelConnection(&context);
            httpcCloseContext(&context);
            return false;
        }
        *capPtr = initialCap;
    }

    u32 totalRead = 0;
    Result ret = 0;
    bool growFailed = false;
    do {
        if(totalRead + 4096 > *capPtr) {
            char *grown = (char *)realloc(*bufPtr, *capPtr * 2);
            if(!grown) {
                growFailed = true;
                break;
            }
            *bufPtr = grown;
            *capPtr *= 2;
        }
        u32 readSize = 0;
        ret = raDownloadDataTimeout(&context, (u8 *)(*bufPtr + totalRead), *capPtr - totalRead - 1, &readSize, RA_HTTP_TIMEOUT_NS);
        totalRead += readSize;
    } while(ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

    // Cancel first: libctru says CloseContext can hang before the full body is read.
    if(growFailed || R_FAILED(ret)) {
        httpcCancelConnection(&context);
        httpcCloseContext(&context);
        return false;
    }

    httpcCloseContext(&context);

    (*bufPtr)[totalRead] = '\0';
    *lenOut = totalRead;
    *statusOut = (int)statusCode;
    return true;
}

// Inline transport reuses the shared response buffer.
static void raServerCallInline(const rc_api_request_t *request,
                               rc_client_server_callback_t callback, void *callbackData)
{
    LightLock_Lock(&raHttpLock);
    u32 len = 0;
    int status = 0;
    bool ok = raHttpPerform(request->url, request->post_data, request->content_type,
                            &raResponseBuf, &raResponseCap, RA_RESPONSE_INITIAL_CAP, &len, &status);
    LightLock_Unlock(&raHttpLock);

    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    response.http_status_code = ok ? status : 0;
    response.body = ok ? raResponseBuf : NULL;
    response.body_length = ok ? len : 0;
    callback(&response, callbackData);
}

static void raWorkerMain(void *arg)
{
    (void)arg;

    LightLock_Lock(&raRequestLock);
    while(raWorkerRunning) {
        RaRequest *req = raPopRequestLocked();
        if(!req) {
            CondVar_Wait(&raRequestCond, &raRequestLock);
            continue;
        }
        LightLock_Unlock(&raRequestLock);

        char *body = NULL;
        u32 cap = 0, len = 0;
        int status = 0;
        LightLock_Lock(&raHttpLock);
        bool ok = raHttpPerform(req->url, req->postData, req->contentType, &body, &cap, 4096, &len, &status);
        LightLock_Unlock(&raHttpLock);

        if(!ok)
            free(body);

        RaResponse *resp = req->response;
        req->response = NULL;
        resp->callback = req->callback;
        resp->callbackData = req->callbackData;
        resp->body = ok ? body : NULL;
        resp->bodyLength = ok ? len : 0;
        resp->httpStatusCode = ok ? status : 0;
        raQueueCompletion(resp);

        raFreeRequest(req);
        LightLock_Lock(&raRequestLock);
    }
    LightLock_Unlock(&raRequestLock);
}

static void raServerCall(const rc_api_request_t *request,
                         rc_client_server_callback_t callback,
                         void *callbackData, rc_client_t *client)
{
    (void)client;

    if(raSyncMode || !raWorkerRunning) {
        raServerCallInline(request, callback, callbackData);
        return;
    }

    char *url = raStrdup(request->url);
    RaRequest *req = url ? (RaRequest *)malloc(sizeof(RaRequest)) : NULL;
    RaResponse *resp = req ? (RaResponse *)malloc(sizeof(RaResponse)) : NULL;
    if(!req || !resp) {
        free(url);
        free(req);
        raServerCallInline(request, callback, callbackData);
        return;
    }

    req->url = url;
    req->postData = raStrdup(request->post_data);
    req->contentType = raStrdup(request->content_type);
    req->response = resp;
    req->callback = callback;
    req->callbackData = callbackData;
    req->next = NULL;

    if((request->post_data && !req->postData) || (request->content_type && !req->contentType)) {
        raFreeRequest(req);
        raServerCallInline(request, callback, callbackData);
        return;
    }

    raQueueRequest(req);
}

static void raDrainCompletions()
{
    RaResponse *resp = raDetachCompletions();
    while(resp) {
        RaResponse *next = resp->next;

        rc_api_server_response_t response;
        memset(&response, 0, sizeof(response));
        response.http_status_code = resp->httpStatusCode;
        response.body = resp->body;
        response.body_length = resp->bodyLength;
        resp->callback(&response, resp->callbackData);

        raFreeResponse(resp);
        resp = next;
    }
}

static void raFreeQueues()
{
    RaRequest *req = raRequestHead;
    while(req) {
        RaRequest *next = req->next;
        raFreeRequest(req);
        req = next;
    }
    raRequestHead = raRequestTail = NULL;

    RaResponse *resp = raCompletionHead;
    while(resp) {
        RaResponse *next = resp->next;
        raFreeResponse(resp);
        resp = next;
    }
    raCompletionHead = raCompletionTail = NULL;
}

//---------------------------------------------------------
// rcheevos logging + event callbacks.
//---------------------------------------------------------
static void raLogCallback(const char *message, const rc_client_t *client)
{
    (void)client;
    log3dsWrite("[RA] %s", message);
}

// Merge unlocks into one toast while it is visible, keeping the highest-point title.
#define RA_TOAST_MS 2500.0
static char raUnlockHeadline[128] = {0};
static unsigned raUnlockBestPoints = 0;
static int raUnlockExtra = 0;
static u64 raUnlockToastUntil = 0;

static void raFormatUnlockToast(char *out, size_t outSize)
{
    char suffix[24] = {0};
    if(raUnlockExtra > 0)
        snprintf(suffix, sizeof(suffix), " (+%d more)", raUnlockExtra);

    const char *prefix = "Unlocked: ";
    size_t fixed = strlen(prefix) + strlen(suffix);
    size_t titleMax = outSize > fixed + 1 ? outSize - fixed - 1 : 0;
    size_t titleLen = strlen(raUnlockHeadline);

    if(titleLen > titleMax && titleMax > 3)
        snprintf(out, outSize, "%s%.*s...%s", prefix, (int)(titleMax - 3), raUnlockHeadline, suffix);
    else
        snprintf(out, outSize, "%s%.*s%s", prefix, (int)titleMax, raUnlockHeadline, suffix);
}

static void raEventHandler(const rc_client_event_t *event, rc_client_t *client)
{
    (void)client;

    switch(event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            if(event->achievement) {
                // Warning achievements are server/client messages, not unlocks.
                if(event->achievement->id >= RA_WARNING_ACHIEVEMENT_ID) {
                    log3dsWrite("[RA] warning: %s", event->achievement->title);
                    break;
                }

                const char *title = event->achievement->title ? event->achievement->title : "Achievement";
                unsigned points = (unsigned)event->achievement->points;
                log3dsWrite("[RA] unlocked: %s", title);

                if(svcGetSystemTick() < raUnlockToastUntil) {
                    raUnlockExtra++;
                    if(points > raUnlockBestPoints) {
                        raUnlockBestPoints = points;
                        strncpy(raUnlockHeadline, title, sizeof(raUnlockHeadline) - 1);
                        raUnlockHeadline[sizeof(raUnlockHeadline) - 1] = '\0';
                    }
                } else {
                    raUnlockExtra = 0;
                    raUnlockBestPoints = points;
                    strncpy(raUnlockHeadline, title, sizeof(raUnlockHeadline) - 1);
                    raUnlockHeadline[sizeof(raUnlockHeadline) - 1] = '\0';
                }

                char msg[64];
                raFormatUnlockToast(msg, sizeof(msg));

                notif3dsTrigger(Notif::RetroAchievement, Notif::Type::Success,
                                settings3DS.GameScreen, RA_TOAST_MS, msg);
                raUnlockToastUntil = svcGetSystemTick() + (u64)(RA_TOAST_MS * CPU_TICKS_PER_MSEC);
            }
            break;

        case RC_CLIENT_EVENT_GAME_COMPLETED:
            log3dsWrite("[RA] game completed");
            break;

        case RC_CLIENT_EVENT_RESET:
            ra3dsReset();
            break;

        default:
            break;
    }
}

// credentials live in the global config
static void raStoreCredentials(const char *username, const char *token)
{
    strncpy(settings3DS.RAUsername, username ? username : "", sizeof(settings3DS.RAUsername) - 1);
    settings3DS.RAUsername[sizeof(settings3DS.RAUsername) - 1] = '\0';
    strncpy(settings3DS.RAToken, token ? token : "", sizeof(settings3DS.RAToken) - 1);
    settings3DS.RAToken[sizeof(settings3DS.RAToken) - 1] = '\0';
    settings3DS.isDirty = true;
    settingsSave(false);
}

static void raClearCredentials()
{
    settings3DS.RAUsername[0] = '\0';
    settings3DS.RAToken[0] = '\0';
    settings3DS.isDirty = true;
    settingsSave(false);
}

//---------------------------------------------------------
// Login + game-load callbacks.
//---------------------------------------------------------
static void raLoginCallback(int result, const char *errorMessage, rc_client_t *client, void *userdata)
{
    (void)userdata;

    if(result == RC_OK) {
        raLoginSucceeded = true;
        const rc_client_user_t *user = rc_client_get_user_info(client);
        if(user && user->token)
            raStoreCredentials(user->username, user->token);
        log3dsWrite("[RA] login ok: %s", user ? user->display_name : "");
    } else {
        raLoginSucceeded = false;
        strncpy(raLastError, errorMessage ? errorMessage : "unknown", sizeof(raLastError) - 1);
        raLastError[sizeof(raLastError) - 1] = '\0';
        log3dsWrite("[RA] login failed: %s", raLastError);
    }
}

static void raGameLoadedCallback(int result, const char *errorMessage, rc_client_t *client, void *userdata)
{
    (void)userdata;

    char msg[160];
    Notif::Type type;

    if(result == RC_OK) {
        rc_client_user_game_summary_t summary;
        rc_client_get_user_game_summary(client, &summary);
        if(summary.num_core_achievements == 0) {
            snprintf(msg, sizeof(msg), "RetroAchievements: no achievements for this game");
            type = Notif::Type::Info;
        } else {
            snprintf(msg, sizeof(msg), "RetroAchievements: %u/%u unlocked",
                     (unsigned)summary.num_unlocked_achievements,
                     (unsigned)summary.num_core_achievements);
            type = Notif::Type::Success;
        }
        log3dsWrite("[RA] game identified, achievements loaded");
    } else if(result == RC_NO_GAME_LOADED) {
        snprintf(msg, sizeof(msg), "RetroAchievements: no achievements for this game");
        type = Notif::Type::Info;
        log3dsWrite("[RA] no achievements for this game");
    } else {
        snprintf(msg, sizeof(msg), "RetroAchievements unavailable");
        type = Notif::Type::Warning;
        log3dsWrite("[RA] game load error: %s", errorMessage ? errorMessage : "unknown");
    }

    notif3dsTrigger(Notif::RetroAchievement, type, settings3DS.GameScreen, RA_TOAST_MS, msg);
}

// swkbd helper: prompt for one line of text, returns false if cancelled
static bool raPromptText(const char *hint, char *out, size_t outSize, bool password)
{
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int)outSize - 1);
    swkbdSetHintText(&swkbd, hint);
    if(password)
        swkbdSetPasswordMode(&swkbd, SWKBD_PASSWORD_HIDE_DELAY);
    return swkbdInputText(&swkbd, out, outSize) == SWKBD_BUTTON_RIGHT;
}

//---------------------------------------------------------
// Public API.
//---------------------------------------------------------
void ra3dsInitialize()
{
    httpcInit(0x1000);

    raClient = rc_client_create(raReadMemory, raServerCall);
    if(!raClient) {
        log3dsWrite("[RA] rc_client_create failed");
        return;
    }

    rc_client_enable_logging(raClient, RC_CLIENT_LOG_LEVEL_INFO, raLogCallback);
    rc_client_set_event_handler(raClient, raEventHandler);

    // hardcore mode is out of scope for now
    rc_client_set_hardcore_enabled(raClient, 0);

    // build the User-Agent once (read-only, shared with the worker thread)
    {
        int len = snprintf(raUserAgent, sizeof(raUserAgent), "snes9x_3ds/%s ", settings3dsGetAppVersion(""));
        if(len > 0 && (size_t)len < sizeof(raUserAgent))
            rc_client_get_user_agent_clause(raClient, raUserAgent + len, sizeof(raUserAgent) - len);
    }

    // start the network worker (do_frame-originated calls run off the emu thread)
    LightLock_Init(&raHttpLock);
    LightLock_Init(&raRequestLock);
    LightLock_Init(&raCompletionLock);
    CondVar_Init(&raRequestCond);
    raWorkerRunning = true;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    raWorker = threadCreate(raWorkerMain, NULL, 0x4000, prio + 1, 0, false);
    if(!raWorker)
        raWorkerRunning = false; // fall back to synchronous transport

    // auto-login with the stored token (synchronous)
    if(settings3DS.RAUsername[0] && settings3DS.RAToken[0]) {
        raSyncMode = true;
        rc_client_begin_login_with_token(raClient, settings3DS.RAUsername,
                                         settings3DS.RAToken, raLoginCallback, NULL);
        raSyncMode = false;
    }

    log3dsWrite("[RA] rc_client initialized (softcore)");
}

void ra3dsFinalize()
{
    // stop the worker, then discard any queued work (no callbacks at shutdown)
    if(raWorker) {
        LightLock_Lock(&raRequestLock);
        raWorkerRunning = false;
        CondVar_Signal(&raRequestCond);
        LightLock_Unlock(&raRequestLock);
        threadJoin(raWorker, UINT64_MAX);
        threadFree(raWorker);
        raWorker = NULL;
    }
    raFreeQueues();

    if(raClient) {
        rc_client_destroy(raClient);
        raClient = NULL;
    }
    httpcExit();

    free(raResponseBuf);
    raResponseBuf = NULL;
    raResponseCap = 0;
}

void ra3dsLoadGame()
{
    if(!raClient)
        return;
    if(!Memory.ROM || Memory.CalculatedSize == 0)
        return;

    if(!rc_client_get_user_info(raClient))
        return;

    // RA hashes the ROM without its 512-byte SMC header. The loader already removed
    // that header and CalculatedSize excludes it, so Memory.ROM can be hashed as-is.
    char hash[33] = {0};
    if(!rc_hash_generate_from_buffer(hash, RC_CONSOLE_SUPER_NINTENDO,
                                     Memory.ROM, Memory.CalculatedSize)) {
        log3dsWrite("[RA] hash generation failed");
        return;
    }

    log3dsWrite("[RA] ROM hash: %s (size %lu)", hash,
                (unsigned long)Memory.CalculatedSize);

    raSyncMode = true;
    rc_client_begin_load_game(raClient, hash, raGameLoadedCallback, NULL);
    raSyncMode = false;
}

void ra3dsUnloadGame()
{
    if(raClient) {
        raDrainCompletions();
        rc_client_unload_game(raClient);
    }
}

void ra3dsReset()
{
    if(raClient)
        rc_client_reset(raClient);
}

void ra3dsDoFrame()
{
    if(!raClient)
        return;

    raDrainCompletions();

    if(!rc_client_is_game_loaded(raClient))
        return;

    rc_client_do_frame(raClient);
}

void ra3dsIdle()
{
    if(!raClient)
        return;

    raDrainCompletions();

    if(!rc_client_is_game_loaded(raClient))
        return;

    rc_client_idle(raClient);
}

bool ra3dsIsLoggedIn()
{
    return raClient && rc_client_get_user_info(raClient) != NULL;
}

static char raPendingUser[32];
static char raPendingPassword[64];

// Clear the whole buffers, not just null-terminate, so the plaintext password
// does not stay resident in memory.
static void raClearPendingCredentials()
{
    memset(raPendingUser, 0, sizeof(raPendingUser));
    memset(raPendingPassword, 0, sizeof(raPendingPassword));
}

static void raFormatDate(time_t value, char *out, size_t outSize)
{
    if(!out || outSize == 0)
        return;

    out[0] = '\0';
    if(!value)
        return;

    struct tm *localTime = localtime(&value);
    if(localTime)
        strftime(out, outSize, "%m/%d/%y", localTime);
}

RaLoginResult ra3dsPromptLogin()
{
    raClearPendingCredentials();

    if(!raClient)
        return RA_LOGIN_CANCELLED;

    if(osGetWifiStrength() == 0) {
        strncpy(raLastError, "No internet connection.", sizeof(raLastError) - 1);
        raLastError[sizeof(raLastError) - 1] = '\0';
        return RA_LOGIN_FAILED;
    }

    if(!raPromptText("RetroAchievements username", raPendingUser, sizeof(raPendingUser), false)) {
        raClearPendingCredentials();
        return RA_LOGIN_CANCELLED;
    }
    if(!raPromptText("RetroAchievements password", raPendingPassword, sizeof(raPendingPassword), true)) {
        raClearPendingCredentials();
        return RA_LOGIN_CANCELLED;
    }

    return RA_LOGIN_PENDING;
}

RaLoginResult ra3dsCompleteLogin()
{
    if(!raClient) {
        raClearPendingCredentials();
        return RA_LOGIN_CANCELLED;
    }

    raLoginSucceeded = false;
    raLastError[0] = '\0';
    raSyncMode = true;
    rc_client_begin_login_with_password(raClient, raPendingUser, raPendingPassword, raLoginCallback, NULL);
    raSyncMode = false;

    raClearPendingCredentials();

    return raLoginSucceeded ? RA_LOGIN_OK : RA_LOGIN_FAILED;
}

void ra3dsLogout()
{
    if(raClient) {
        raSyncMode = true;
        rc_client_logout(raClient);
        raSyncMode = false;
    }
    raClearCredentials();
}

const char *ra3dsGetLastError()
{
    return raLastError;
}

bool ra3dsGetUser(RaUser *out)
{
    if(!out || !raClient)
        return false;
    const rc_client_user_t *user = rc_client_get_user_info(raClient);
    if(!user)
        return false;

    strncpy(out->name, user->display_name ? user->display_name : "", sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    out->softcorePoints = (int)user->score_softcore;
    out->hardcore = rc_client_get_hardcore_enabled(raClient) != 0;
    return true;
}

bool ra3dsGetGameSummary(RaGameSummary *out)
{
    if(!out || !raClient || !rc_client_is_game_loaded(raClient))
        return false;

    rc_client_user_game_summary_t clientSummary;
    rc_client_get_user_game_summary(raClient, &clientSummary);
    out->unlocked       = (int)clientSummary.num_unlocked_achievements;
    out->total          = (int)clientSummary.num_core_achievements;
    out->pointsUnlocked = (int)clientSummary.points_unlocked;
    out->pointsTotal    = (int)clientSummary.points_core;
    out->beaten         = clientSummary.beaten_time != 0;
    out->mastered       = clientSummary.completed_time != 0;
    out->unsupported    = (int)clientSummary.num_unsupported_achievements;
    
    raFormatDate(clientSummary.beaten_time, out->beatenDate, sizeof(out->beatenDate));
    raFormatDate(clientSummary.completed_time, out->masteredDate, sizeof(out->masteredDate));

    return true;
}

void ra3dsGetRichPresence(char *out, size_t outSize)
{
    if(!out || outSize == 0)
        return;
    out[0] = '\0';
    if(!raClient || !rc_client_is_game_loaded(raClient))
        return;

    char raw[256];
    if(rc_client_get_rich_presence_message(raClient, raw, sizeof(raw)) == 0)
        return;

    // Keep only text the 3DS font can draw, and drop emoji-style "[...]" tags.
    size_t writePos = 0;
    size_t groupStart = 0;
    bool inGroup = false;
    bool groupDirty = false;
    bool lastSpace = false;

    for(size_t i = 0; raw[i] && writePos + 1 < outSize; i++) {
        unsigned char c = (unsigned char)raw[i];
        bool renderable = c >= 0x20 && c < 0x7f;

        if(c == '[') {
            inGroup = true;
            groupDirty = false;
            groupStart = writePos;
        }

        if(!renderable) {
            if(inGroup) groupDirty = true;
            continue;
        }

        if(c == ' ') {
            if(lastSpace) continue;
            lastSpace = true;
        } else {
            lastSpace = false;
        }

        out[writePos++] = (char)c;

        if(c == ']') {
            inGroup = false;
            if(groupDirty) {
                writePos = groupStart;
                lastSpace = writePos > 0 && out[writePos - 1] == ' ';
            }
        }
    }

    while(writePos > 0 && out[writePos - 1] == ' ')
        writePos--;
    out[writePos] = '\0';
}

const char *ra3dsGetGameTitle()
{
    if(!raClient)
        return "";
    const rc_client_game_t *game = rc_client_get_game_info(raClient);
    return game && game->title ? game->title : "";
}

int ra3dsGetAchievementCount()
{
    RaGameSummary summary = {};
    return ra3dsGetGameSummary(&summary) ? summary.total : 0;
}


int ra3dsGetAchievements(RaAchievementInfo *out, int maxItems)
{
    if(!out || maxItems <= 0 || !raClient || !rc_client_is_game_loaded(raClient))
        return 0;

    // Ask rcheevos for its bucket order, then regroup by display state.
    rc_client_achievement_list_t *list = rc_client_create_achievement_list(
        raClient, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
        RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if(!list)
        return 0;

    int written = 0;

    for(int displayGroup = 0; displayGroup < 3 && written < maxItems; displayGroup++) {
        for(u32 bucketIndex = 0; bucketIndex < list->num_buckets && written < maxItems; bucketIndex++) {
            const rc_client_achievement_bucket_t *bucket = &list->buckets[bucketIndex];
            for(u32 achievementIndex = 0; achievementIndex < bucket->num_achievements && written < maxItems; achievementIndex++) {
                const rc_client_achievement_t *achievement = bucket->achievements[achievementIndex];
                // Server notices are not real achievements.
                if(achievement->id >= RA_WARNING_ACHIEVEMENT_ID)
                    continue;
                
                bool unlocked = achievement->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED;
                bool unsupported = achievement->state == RC_CLIENT_ACHIEVEMENT_STATE_DISABLED;
                int achievementGroup = unlocked ? 0 : unsupported ? 2 : 1;
                if(achievementGroup != displayGroup)
                    continue;
                
                RaAchievementInfo *outAchievement = &out[written++];
                outAchievement->id = achievement->id;
                strncpy(outAchievement->title, achievement->title ? achievement->title : "", sizeof(outAchievement->title) - 1);
                outAchievement->title[sizeof(outAchievement->title) - 1] = '\0';
                strncpy(outAchievement->description, achievement->description ? achievement->description : "", sizeof(outAchievement->description) - 1);
                outAchievement->description[sizeof(outAchievement->description) - 1] = '\0';
                outAchievement->points = (int)achievement->points;
                outAchievement->rarity = achievement->rarity;
                outAchievement->unlocked = unlocked;
                outAchievement->unsupported = unsupported;
                outAchievement->type = (int)achievement->type;
                
                raFormatDate(unlocked ? achievement->unlock_time : 0, outAchievement->unlockDate, sizeof(outAchievement->unlockDate));
            }
        }
    }

    rc_client_destroy_achievement_list(list);

    return written;
}
