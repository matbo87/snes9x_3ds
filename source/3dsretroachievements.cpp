#include "3dsretroachievements.h"
#include "3dslog.h"
#include "3dssettings.h"

#include "rc_client.h"
#include "rc_hash.h"

#include "memmap.h"

#include <3ds.h>
#include <string.h>
#include <stdlib.h>

static rc_client_t *raClient = NULL;

static bool raLoginSucceeded = false;
static char raLastError[160] = {0};

#define RA_RESPONSE_INITIAL_CAP (128 * 1024)
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
// Synchronous http server call over httpc.
// https is dropped to http with verification disabled.
// RA tokens are low-value, so cleartext is an accepted trade-off.
//---------------------------------------------------------
static void raServerCall(const rc_api_request_t *request,
                         rc_client_server_callback_t callback,
                         void *callbackData, rc_client_t *client)
{
    (void)client;

    httpcContext context;
    bool contextOpened = false;
    u32 statusCode = 0;
    u32 totalRead = 0;

    char urlBuf[512];
    const char *url = request->url;
    if(strncmp(url, "https://", 8) == 0) {
        snprintf(urlBuf, sizeof(urlBuf), "http://%s", url + 8);
        url = urlBuf;
    }

    HTTPC_RequestMethod method = request->post_data ? HTTPC_METHOD_POST : HTTPC_METHOD_GET;
    if(R_FAILED(httpcOpenContext(&context, method, url, 1)))
        goto fail;
    contextOpened = true;

    httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
    httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_ENABLED);
    httpcAddRequestHeaderField(&context, "Connection", "Keep-Alive");

    {
        char userAgent[256];
        int len = snprintf(userAgent, sizeof(userAgent), "snes9x_3ds/%s ",
                           settings3dsGetAppVersion(""));
        if(len > 0 && (size_t)len < sizeof(userAgent))
            rc_client_get_user_agent_clause(raClient, userAgent + len, sizeof(userAgent) - len);
        httpcAddRequestHeaderField(&context, "User-Agent", userAgent);
    }

    if(request->post_data) {
        httpcAddRequestHeaderField(&context, "Content-Type",
            request->content_type ? request->content_type : "application/x-www-form-urlencoded");
        httpcAddPostDataRaw(&context, (const u32 *)request->post_data,
                            (u32)strlen(request->post_data));
    }

    if(R_FAILED(httpcBeginRequest(&context)))
        goto fail;

    httpcGetResponseStatusCode(&context, &statusCode);

    // read the response into the reusable buffer, growing on overflow
    {
        if(!raResponseBuf) {
            raResponseBuf = (char *)malloc(RA_RESPONSE_INITIAL_CAP);
            if(!raResponseBuf)
                goto fail;
            raResponseCap = RA_RESPONSE_INITIAL_CAP;
        }

        Result ret;
        do {
            if(totalRead + 4096 > raResponseCap) {
                char *grown = (char *)realloc(raResponseBuf, raResponseCap * 2);
                if(!grown)
                    break;
                raResponseBuf = grown;
                raResponseCap *= 2;
            }
            u32 readSize = 0;
            ret = httpcDownloadData(&context, (u8 *)(raResponseBuf + totalRead),
                                    raResponseCap - totalRead - 1, &readSize);
            totalRead += readSize;
        } while(ret == (Result)HTTPC_RESULTCODE_DOWNLOADPENDING);

        raResponseBuf[totalRead] = '\0';
    }

    httpcCloseContext(&context);

    {
        rc_api_server_response_t response;
        memset(&response, 0, sizeof(response));
        response.http_status_code = (int)statusCode;
        response.body = raResponseBuf;
        response.body_length = totalRead;
        callback(&response, callbackData);
    }

    return;

fail:
    if(contextOpened)
        httpcCloseContext(&context);

    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    response.http_status_code = 0;
    callback(&response, callbackData);
}

//---------------------------------------------------------
// rcheevos logging + event callbacks.
//---------------------------------------------------------
static void raLogCallback(const char *message, const rc_client_t *client)
{
    (void)client;
    log3dsWrite("[RA] %s", message);
}

static void raEventHandler(const rc_client_event_t *event, rc_client_t *client)
{
    (void)client;

    switch(event->type) {
        // TODO: replace with on-screen notification
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            if(event->achievement) {
                const char *kind = event->achievement->id >= RA_WARNING_ACHIEVEMENT_ID ? "warning" : "unlocked";
                printf("[RA] %s: %s\n", kind, event->achievement->title);
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
    (void)client; (void)userdata;

    if(result == RC_OK)
        log3dsWrite("[RA] game identified, achievements loaded");
    else if(result == RC_NO_GAME_LOADED)
        log3dsWrite("[RA] no achievements for this game");
    else
        log3dsWrite("[RA] game load error: %s", errorMessage ? errorMessage : "unknown");
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

    // auto-login with the stored token if credentials were saved previously
    if(settings3DS.RAUsername[0] && settings3DS.RAToken[0])
        rc_client_begin_login_with_token(raClient, settings3DS.RAUsername,
                                         settings3DS.RAToken, raLoginCallback, NULL);

    log3dsWrite("[RA] rc_client initialized (softcore)");
}

void ra3dsFinalize()
{
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

    rc_client_begin_load_game(raClient, hash, raGameLoadedCallback, NULL);
}

void ra3dsUnloadGame()
{
    if(raClient)
        rc_client_unload_game(raClient);
}

void ra3dsReset()
{
    if(raClient)
        rc_client_reset(raClient);
}

void ra3dsDoFrame()
{
    if(!raClient || !rc_client_is_game_loaded(raClient))
        return;

    rc_client_do_frame(raClient);
}

void ra3dsIdle()
{
    if(!raClient || !rc_client_is_game_loaded(raClient))
        return;

    rc_client_idle(raClient);
}

bool ra3dsIsLoggedIn()
{
    return raClient && rc_client_get_user_info(raClient) != NULL;
}

RaLoginResult ra3dsPromptLogin()
{
    if(!raClient)
        return RA_LOGIN_CANCELLED;

    char username[32] = {0};
    char password[64] = {0};

    if(!raPromptText("RetroAchievements username", username, sizeof(username), false))
        return RA_LOGIN_CANCELLED;
    if(!raPromptText("RetroAchievements password", password, sizeof(password), true))
        return RA_LOGIN_CANCELLED;

    // cheap pre-check for offline case (no AP association)
    if(osGetWifiStrength() == 0) {
        strncpy(raLastError, "No internet connection.", sizeof(raLastError) - 1);
        raLastError[sizeof(raLastError) - 1] = '\0';
        return RA_LOGIN_FAILED;
    }

    // synchronous transport: raLoginCallback runs before this returns
    raLoginSucceeded = false;
    raLastError[0] = '\0';
    rc_client_begin_login_with_password(raClient, username, password, raLoginCallback, NULL);

    return raLoginSucceeded ? RA_LOGIN_OK : RA_LOGIN_FAILED;
}

void ra3dsLogout()
{
    if(raClient)
        rc_client_logout(raClient);
    raClearCredentials();
}

const char *ra3dsGetUsername()
{
    if(!raClient)
        return NULL;
    const rc_client_user_t *user = rc_client_get_user_info(raClient);
    return user ? user->display_name : NULL;
}

const char *ra3dsGetLastError()
{
    return raLastError;
}
