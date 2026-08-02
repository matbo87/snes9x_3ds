#include "3dsra.h"
#include "3dslog.h"
#include "3dssettings.h"
#include "3dsgpu.h"       // SGPU_TEXTURE_ID, needed by 3dsui_notif.h
#include "3dspixel_utils.h"
#include "3dsui_notif.h"

#include "rc_client.h"
#include "rc_hash.h"

#include "png_utils.h"
#include "memmap.h"
#include "3dsimg_cache.h"
#include "3dsra_http.h"

#include <3ds.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#include <malloc.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

static rc_client_t *raClient = NULL;

static bool raLoginSucceeded = false;
static char raLastError[160] = {0};

static char raUserAgent[256] = {0};
static LightLock badgeLock;

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

static bool raUiDirty = false;
static u32 raLastUnlockedId = 0;

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

                raLastUnlockedId = event->achievement->id;
                raUiDirty = true;

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
            raLastUnlockedId = 0;
            raUiDirty = true;   // beaten/mastered state changed
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
void ra3dsLoadGame()
{
    if(!raClient)
        return;
    if(!Memory.ROM || Memory.CalculatedSize == 0)
        return;

    if(!rc_client_get_user_info(raClient))
        return;

    // Hash the original file bytes. LoadROM strips copier headers and can
    // deinterleave ROM data in Memory.ROM before emulation.
    char hash[33] = {0};
    rc_hash_iterator_t hashIterator;
    rc_hash_initialize_iterator(&hashIterator, Memory.ROMFilename, NULL, 0);
    int hashGenerated = rc_hash_generate(hash, RC_CONSOLE_SUPER_NINTENDO, &hashIterator);
    rc_hash_destroy_iterator(&hashIterator);
    if(!hashGenerated) {
        log3dsWrite("[RA] hash generation failed");
        return;
    }

    log3dsWrite("[RA] ROM hash: %s", hash);

    raHttpSetSyncMode(true);
    rc_client_begin_load_game(raClient, hash, raGameLoadedCallback, NULL);
    raHttpSetSyncMode(false);
}

void ra3dsUnloadGame()
{
    if(raClient) {
        raDrainCompletions();
        rc_client_unload_game(raClient);
    }
    // The badge display reader is owned by 3dsra_ui.cpp and closed by the ROM
    // loading path before the next game is loaded.
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
    raHttpSetSyncMode(true);
    rc_client_begin_login_with_password(raClient, raPendingUser, raPendingPassword, raLoginCallback, NULL);
    raHttpSetSyncMode(false);

    raClearPendingCredentials();

    return raLoginSucceeded ? RA_LOGIN_OK : RA_LOGIN_FAILED;
}

void ra3dsLogout()
{
    if(raClient) {
        raHttpSetSyncMode(true);
        rc_client_logout(raClient);
        raHttpSetSyncMode(false);
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
        u8 c = (u8)raw[i];
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

// 0 when no game is loaded/identified.
u32 ra3dsGetLoadedGameId()
{
    const rc_client_game_t *game =
        (raClient && rc_client_is_game_loaded(raClient)) ? rc_client_get_game_info(raClient) : NULL;
    return game ? (u32)game->id : 0;
}

int ra3dsGetAchievementCount()
{
    RaGameSummary summary = {};
    return ra3dsGetGameSummary(&summary) ? summary.total : 0;
}

bool ra3dsCheckAndClearMenuDirty(void)
{
    bool dirty = raUiDirty;
    raUiDirty = false;
    return dirty;
}

uint32_t ra3dsGetLastUnlockedId(void)
{
    return raLastUnlockedId;
}

// Raw-socket plain-HTTP transport for badge downloads. http:C serializes
// requests heavily; SOC lets the badge pool overlap them.
#define RA_SOC_BUFFER_SIZE   (0x100000)

static u32            *raSocBuffer = NULL;
static bool            raSocReady  = false;
static struct sockaddr_in badgeAddr;
static char            badgeHost[128] = {0};

static bool raSocInit(void)
{
    if(raSocReady)
        return true;
    raSocBuffer = (u32 *)memalign(0x1000, RA_SOC_BUFFER_SIZE);
    if(!raSocBuffer)
        return false;
    if(R_FAILED(socInit(raSocBuffer, RA_SOC_BUFFER_SIZE))) {
        free(raSocBuffer);
        raSocBuffer = NULL;
        return false;
    }
    raSocReady = true;
    return true;
}

static void raSocExit(void)
{
    if(!raSocReady)
        return;
    socExit();
    free(raSocBuffer);
    raSocBuffer = NULL;
    raSocReady = false;
}

// Split "http[s]://host/path" into host and path. path includes the leading '/'.
static bool raSocParseUrl(const char *url, char *host, size_t hostSize, const char **pathOut)
{
    const char *urlHost = url;
    if(strncmp(urlHost, "https://", 8) == 0) urlHost += 8;
    else if(strncmp(urlHost, "http://", 7) == 0) urlHost += 7;

    const char *slash = strchr(urlHost, '/');
    if(!slash)
        return false;
    if(host) {
        size_t hostLen = (size_t)(slash - urlHost);
        if(hostLen == 0 || hostLen >= hostSize)
            return false;
        memcpy(host, urlHost, hostLen);
        host[hostLen] = '\0';
    }
    *pathOut = slash;
    return true;
}

// Blocking I/O is deliberate. On real hardware, poll() on the shared SOCU
// session serializes the 8-worker pool back to roughly 1-worker throughput;
// non-blocking connect also never reports completion via POLLOUT.
static bool raSocHttpGet(const struct sockaddr_in *addr, const char *host, const char *path,
                         u8 **bufOut, u32 *lenOut, int *statusOut)
{
    *bufOut = NULL;
    *lenOut = 0;
    *statusOut = 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0)
        return false;

    if(connect(fd, (const struct sockaddr *)addr, sizeof(*addr)) < 0) {
        close(fd);
        return false;
    }

    char req[512];
    int reqLen = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host, raUserAgent);
    if(reqLen <= 0 || reqLen >= (int)sizeof(req)) {
        close(fd);
        return false;
    }

    for(int sent = 0; sent < reqLen; ) {
        int n = send(fd, req + sent, reqLen - sent, 0);
        if(n <= 0) { close(fd); return false; }
        sent += n;
    }

    const u32 RA_SOC_MAX_RESPONSE = 128 * 1024;
    u32 cap = 8 * 1024, total = 0;
    u8 *buf = (u8 *)malloc(cap);
    if(!buf) { close(fd); return false; }
    for(;;) {
        if(total + 4096 > cap) {
            if(cap >= RA_SOC_MAX_RESPONSE) { free(buf); close(fd); return false; }
            u8 *grown = (u8 *)realloc(buf, cap * 2);
            if(!grown) { free(buf); close(fd); return false; }
            buf = grown;
            cap *= 2;
        }
        int n = recv(fd, buf + total, cap - total - 1, 0);
        if(n < 0) { free(buf); close(fd); return false; }
        if(n == 0) break;
        total += (u32)n;
    }
    close(fd);
    buf[total] = '\0';

    int status = 0;
    {
        u8 *sp = (u8 *)memchr(buf, ' ', total < 16 ? total : 16);
        if(sp) status = atoi((const char *)sp + 1);
    }

    u8 *body = NULL;
    u32 headerLen = 0;
    for(u32 i = 0; i + 3 < total; i++) {
        if(buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
            body = buf + i + 4;
            headerLen = i + 4;
            break;
        }
    }
    if(!body) { free(buf); return false; }
    u32 bodyLen = total - headerLen;

    bool chunked = false;
    static const char kTransferEncoding[] = "transfer-encoding:";
    static const char kChunked[] = "chunked";
    const u32 transferEncodingLen = sizeof(kTransferEncoding) - 1;
    for(u32 i = 0; i + transferEncodingLen <= headerLen; i++) {
        u32 j = 0;
        while(j < transferEncodingLen && (buf[i + j] | 0x20) == (u8)kTransferEncoding[j])
            j++;
        if(j != transferEncodingLen)
            continue;
        for(u32 k = i + transferEncodingLen; k < headerLen && buf[k] != '\n' && !chunked; k++) {
            u32 m = 0;
            while(m < 7 && k + m < headerLen && (buf[k + m] | 0x20) == (u8)kChunked[m])
                m++;
            if(m == 7) chunked = true;
        }
        break;
    }

    if(chunked) {
        u8 *out = (u8 *)malloc(bodyLen + 1);
        if(!out) { free(buf); return false; }
        u32 o = 0, p = 0;
        while(p < bodyLen) {
            u32 chunkLen = 0;
            while(p < bodyLen && body[p] != '\r' && body[p] != ';') {
                char c = (char)body[p++];
                int d;
                if(c >= '0' && c <= '9') d = c - '0';
                else if(c >= 'a' && c <= 'f') d = c - 'a' + 10;
                else if(c >= 'A' && c <= 'F') d = c - 'A' + 10;
                else break;
                chunkLen = chunkLen * 16 + (u32)d;
            }
            while(p < bodyLen && body[p] != '\n') p++;
            if(p < bodyLen) p++;
            if(chunkLen == 0) break;
            if(p + chunkLen > bodyLen) chunkLen = bodyLen - p;
            memcpy(out + o, body + p, chunkLen);
            o += chunkLen;
            p += chunkLen;
            if(p < bodyLen && body[p] == '\r') p++;
            if(p < bodyLen && body[p] == '\n') p++;
        }
        free(buf);
        out[o] = '\0';
        *bufOut = out;
        *lenOut = o;
        *statusOut = status;
        return true;
    }

    memmove(buf, body, bodyLen);
    buf[bodyLen] = '\0';
    *bufOut = buf;
    *lenOut = bodyLen;
    *statusOut = status;
    return true;
}

// Badge cache. Downloaded PNGs are decoded at finalization and stored as
// pre-swizzled RGB565 (column-major, top row first in memory), the same layout
// the software thumbnail blit expects. Entries use key = achievementId*2 +
// lockedFlag (0 = unlocked badge, 1 = locked badge).
#define RA_BADGE_POOL_THREADS 8
#define RA_BADGE_FAIL_ABORT   12
// game thumbnail source is 96x96, achievement badges are 64x64
#define RA_BADGE_DIM_MAX   96

typedef struct BadgeJob {
    u32         key;
    const char  *url;
    u8          *data;  // downloaded PNG, then replaced by swizzled RGB565
    u32         length;
    u16         width;  // set once decoded/swizzled
    u16         height;
} BadgeJob;

static BadgeJob *badgeJobs = NULL;
static int   badgeJobCount = 0;
static int   badgeNextJob = 0;
static int   badgeCompleted = 0;
static int   badgeSucceeded = 0;
static int   badgeFailed = 0;
static bool  badgeCancel = false;

static rc_client_achievement_list_t *badgeList = NULL;
static Thread badgePool[RA_BADGE_POOL_THREADS] = {0};
static u64    badgeTStart = 0;
static u32    badgeGameId = 0;

// Declared in 3dsra.h; shared with the display reader in 3dsra_ui.cpp.
void getBadgePath(u32 gameId, char *out, size_t outSize)
{
    snprintf(out, outSize, "%s/ra_badges/%u.cache", settings3DS.RootDir, (unsigned)gameId);
}

static bool badgeCacheComplete(u32 gameId, u32 expectedCount)
{
    char path[512];
    getBadgePath(gameId, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if(!f)
        return false;
    ImageCacheHeader h;
    bool ok = fread(&h, sizeof(h), 1, f) == 1
              && memcmp(h.magic, RA_BADGE_MAGIC, 4) == 0   // magic encodes the version
              && !(h.flags & IMG_CACHE_FLAG_INCOMPLETE)
              && h.expectedCount == expectedCount;
    fclose(f);
    return ok;
}

static void badgeWorker(void *arg)
{
    (void)arg;
    for(;;) {
        LightLock_Lock(&badgeLock);
        int i = (badgeCancel || badgeNextJob >= badgeJobCount)
                    ? -1 : badgeNextJob++;
        LightLock_Unlock(&badgeLock);
        if(i < 0)
            break;

        BadgeJob *job = &badgeJobs[i];
        u8   *buf = NULL;
        u32   len = 0;
        int   status = 0;

        const char *badgePath = NULL;
        bool ok = raSocParseUrl(job->url, NULL, 0, &badgePath)
                  && raSocHttpGet(&badgeAddr, badgeHost, badgePath, &buf, &len, &status);

        if(ok && status == 200 && len > 0) {
            u8 *trimmed = (u8 *)realloc(buf, len);
            job->data = trimmed ? trimmed : buf;
            job->length = len;
        } else {
            free(buf);
        }

        LightLock_Lock(&badgeLock);
        if(job->data) {
            badgeSucceeded++;
        } else {
            badgeFailed++;
            if(badgeFailed >= RA_BADGE_FAIL_ABORT && badgeSucceeded == 0)
                badgeCancel = true;
        }
        badgeCompleted++;
        LightLock_Unlock(&badgeLock);
    }
}

int ra3dsBeginBadgeCache(void)
{
    if(badgeList || !badgeJobs)
        return 0;

    if(!raClient || !rc_client_is_game_loaded(raClient))
        return 0;

    const rc_client_game_t *game = rc_client_get_game_info(raClient);
    if(!game)
        return 0;

    // CATEGORY_CORE includes bonus subsets; keep the primary set shown by UI.
    u32 coreSubset = 0;
    rc_client_subset_list_t *subs = rc_client_create_subset_list(raClient);
    if(subs) {
        if(subs->num_subsets > 0)
            coreSubset = subs->subsets[0]->id;
        rc_client_destroy_subset_list(subs);
    }

    badgeList = rc_client_create_achievement_list(
        raClient, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE,
        RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
    if(!badgeList)
        return 0;

    // clear the reused pool: stale data/width from a prior sync must not leak in
    memset(badgeJobs, 0, badgeMaxCount * sizeof(BadgeJob));

    // Cap at badgeMaxCount: badges past it show the placeholder, the
    // achievements themselves are unaffected (they come from rc_client).
    badgeJobCount = 0;

    // Game thumbnail (96x96) as the first job. Its URL is resolved into a
    // persistent buffer since BadgeJob.url does not own its string.
    static char badgeGameUrl[256];
    if(rc_client_game_get_image_url(game, badgeGameUrl, sizeof(badgeGameUrl)) == RC_OK
       && badgeGameUrl[0]) {
        badgeJobs[badgeJobCount].key = RA_GAME_BADGE_KEY * 2 + 1;
        badgeJobs[badgeJobCount].url = badgeGameUrl;
        badgeJobCount++;
    }

    for(u32 b = 0; b < badgeList->num_buckets && (size_t)badgeJobCount < badgeMaxCount; b++) {
        const rc_client_achievement_bucket_t *bucket = &badgeList->buckets[b];
        if(coreSubset && bucket->subset_id != 0 && bucket->subset_id != coreSubset)
            continue;
        for(u32 i = 0; i < bucket->num_achievements && (size_t)badgeJobCount < badgeMaxCount; i++) {
            const rc_client_achievement_t *ach = bucket->achievements[i];
            if(ach->id >= RA_WARNING_ACHIEVEMENT_ID)
                continue;
            if(ach->badge_url && ach->badge_url[0]) {
                badgeJobs[badgeJobCount].key = ach->id * 2 + 0;
                badgeJobs[badgeJobCount].url = ach->badge_url;
                badgeJobCount++;
            }
            if(ach->badge_locked_url && ach->badge_locked_url[0] &&
               (size_t)badgeJobCount < badgeMaxCount) {
                badgeJobs[badgeJobCount].key = ach->id * 2 + 1;
                badgeJobs[badgeJobCount].url = ach->badge_locked_url;
                badgeJobCount++;
            }
        }
    }

    if(badgeJobCount == 0) {
        rc_client_destroy_achievement_list(badgeList);
        badgeList = NULL;
        return 0;
    }

    if(badgeCacheComplete((u32)game->id, (u32)badgeJobCount)) {
        rc_client_destroy_achievement_list(badgeList);
        badgeList = NULL;
        return 0;
    }
    const char *firstBadgePath = NULL;
    if(!raSocInit() ||
       !raSocParseUrl(badgeJobs[0].url, badgeHost, sizeof(badgeHost), &firstBadgePath)) {
        raSocExit();
        rc_client_destroy_achievement_list(badgeList);
        badgeList = NULL;
        return 0;
    }

    struct hostent *he = gethostbyname(badgeHost);
    if(!he || !he->h_addr_list || !he->h_addr_list[0]) {
        raSocExit();
        rc_client_destroy_achievement_list(badgeList);
        badgeList = NULL;
        return 0;
    }
    memset(&badgeAddr, 0, sizeof(badgeAddr));
    badgeAddr.sin_family = AF_INET;
    badgeAddr.sin_port   = htons(80);
    memcpy(&badgeAddr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    badgeGameId = (u32)game->id;
    badgeNextJob = 0;
    badgeCompleted = 0;
    badgeSucceeded = 0;
    badgeFailed = 0;
    badgeCancel = false;

    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);

    int spawned = 0;
    badgeTStart = osGetTime();
    for(int t = 0; t < RA_BADGE_POOL_THREADS; t++) {
        badgePool[t] = threadCreate(badgeWorker, NULL, 0x8000, prio + 1, -1, false);
        if(badgePool[t])
            spawned++;
    }
    if(spawned == 0)
        badgeWorker(NULL);

    return badgeJobCount;
}

bool ra3dsBadgeCachePoll(int *doneOut, int *totalOut)
{
    LightLock_Lock(&badgeLock);
    int  done      = badgeCompleted;
    int  claimed   = badgeNextJob;
    bool cancelled = badgeCancel;
    LightLock_Unlock(&badgeLock);
    if(doneOut)  *doneOut = done;
    if(totalOut) *totalOut = badgeJobCount;
    return cancelled ? (done < claimed) : (done < badgeJobCount);
}

void ra3dsEndBadgeCache(void)
{
    if(!badgeList)     // no sync in progress
        return;

    LightLock_Lock(&badgeLock);
    badgeCancel = true;
    LightLock_Unlock(&badgeLock);
    for(int t = 0; t < RA_BADGE_POOL_THREADS; t++) {
        if(badgePool[t]) {
            threadJoin(badgePool[t], UINT64_MAX);
            threadFree(badgePool[t]);
            badgePool[t] = NULL;
        }
    }
    raSocExit();
    u64 tEnd = osGetTime();

    char dir[400];
    snprintf(dir, sizeof(dir), "%s/ra_badges", settings3DS.RootDir);
    mkdir(dir, 0777);

    char path[512], tmp[520];
    getBadgePath(badgeGameId, path, sizeof(path));
    snprintf(tmp,  sizeof(tmp),  "%s.tmp", path);

    // Decode + swizzle each downloaded PNG straight into the cache as pre-swizzled
    // RGB565, staging through g_fileBuffer so nothing is allocated per badge:
    // libpng decodes to its front, the swizzle writes a non-overlapping slice
    // past the largest possible RGBA image, and that slice is written out. Runs
    // on the caller thread after workers joined, so g_fileBuffer is free to use.
    // Header + a full-size index are reserved up front; payloads stream in after
    // them and the index is back-filled once dimensions are known.
    u32 okCount = 0, totalBytes = 0;
    bool wrote = false;

    FILE *f = fopen(tmp, "wb");
    if(f) {
        static char ioBuf[64 * 1024];
        setvbuf(f, ioBuf, _IOFBF, sizeof(ioBuf));

        u32 payloadBase = (u32)sizeof(ImageCacheHeader)
                          + (u32)badgeJobCount * (u32)sizeof(ImageCacheEntry);

        bool writeOk = fseek(f, (long)payloadBase, SEEK_SET) == 0;
        for(int i = 0; writeOk && i < badgeJobCount; i++) {
            BadgeJob *j = &badgeJobs[i];
            if(!j->data)
                continue;

            int w = 0, h = 0;
            if(!decodePngFromMemory(j->data, j->length, w, h) || w <= 0 || h <= 0)
                continue;   // undecodable -> skip -> partial cache

            // decode -> downscale -> swizzle
            const u32 *rgba = (const u32 *)g_fileBuffer;
            u32 *dscratch = (u32 *)(g_fileBuffer + RA_BADGE_DIM_MAX * RA_BADGE_DIM_MAX * 4);
            u16 *swz = (u16 *)(dscratch + badgeMaxWidth * badgeMaxHeight);

            // downscale 96x96 game thumbnail to 64x64
            if(w == 96 && h == 96) {
                boxDownscale3to2Rgba(rgba, w, h, dscratch, badgeMaxWidth);
                rgba = dscratch;
                w = badgeMaxWidth;
                h = badgeMaxHeight;
            } else if(w > badgeMaxWidth || h > badgeMaxHeight) {
                continue;   // unexpected size larger than RA_BADGE_DIM_MAX -> skip
            }
            for(int py = 0; py < h; py++) {
                int row = h - 1 - py;
                for(int px = 0; px < w; px++)
                    swz[px * h + row] = rgba8ToRgb565(rgba[py * w + px]);
            }

            u32 bytes = (u32)((size_t)w * h * sizeof(u16));
            if(fwrite(swz, 1, bytes, f) != bytes) {
                writeOk = false;
                break;
            }
            j->width  = (u16)w;   // width>0 marks a stored entry
            j->height = (u16)h;
            okCount++;
            totalBytes += bytes;
        }

        if(writeOk && fseek(f, 0, SEEK_SET) == 0) {
            ImageCacheHeader h;
            memcpy(h.magic, RA_BADGE_MAGIC, 4);
            h._padding      = 0;
            h.flags         = (okCount < (u32)badgeJobCount) ? IMG_CACHE_FLAG_INCOMPLETE : 0;
            h.count         = okCount;
            h.expectedCount = (u32)badgeJobCount;
            h.width         = badgeMaxWidth;   // header carries the max; entries store their own dims
            h.height        = badgeMaxHeight;
            writeOk = fwrite(&h, sizeof(h), 1, f) == 1;

            u32 offset = payloadBase;
            for(int i = 0; writeOk && i < badgeJobCount; i++) {
                BadgeJob *j = &badgeJobs[i];
                if(j->width == 0) continue;   // not stored
                ImageCacheEntry entry = { j->key, offset, j->width, j->height };
                writeOk = fwrite(&entry, sizeof(entry), 1, f) == 1;
                offset += (u32)j->width * j->height * (u32)sizeof(u16);
            }
        } else {
            writeOk = false;
        }

        wrote = (fclose(f) == 0) && writeOk;
    }

    u32 failCount = (u32)badgeJobCount - okCount;

    // SD rename does not overwrite; remove the old cache only after tmp is good.
    bool saved = false;
    if(wrote) {
        if(rename(tmp, path) == 0) {
            saved = true;
        } else {
            remove(path);
            saved = rename(tmp, path) == 0;
        }
    }
    if(!saved)
        remove(tmp);

    log3dsWrite("[RA] badge cache: %d jobs, %u ok, %u fail, %u bytes, %llu ms, %s -> %s",
                badgeJobCount, okCount, failCount, totalBytes,
                (unsigned long long)(tEnd - badgeTStart),
                saved ? "saved" : "write failed", path);

    for(int i = 0; i < badgeJobCount; i++)
        free(badgeJobs[i].data);
    badgeJobCount = 0;

    rc_client_destroy_achievement_list(badgeList);
    badgeList = NULL;

    // A fresh cache exists now; the menu refreshes the display reader after this returns.
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

void ra3dsInitialize()
{
    raClient = rc_client_create(raReadMemory, raServerCall);
    if(!raClient) {
        log3dsWrite("[RA] rc_client_create failed");
        return;
    }

    rc_client_enable_logging(raClient, RC_CLIENT_LOG_LEVEL_INFO, raLogCallback);
    rc_client_set_event_handler(raClient, raEventHandler);

    if(!badgeJobs)
        badgeJobs = (BadgeJob *)malloc(badgeMaxCount * sizeof(BadgeJob));
    LightLock_Init(&badgeLock);

    // hardcore mode is out of scope for now
    rc_client_set_hardcore_enabled(raClient, 0);

    // build the User-Agent once, then hand a copy to the httpc transport
    {
        int len = snprintf(raUserAgent, sizeof(raUserAgent), "snes9x_3ds/%s ", settings3dsGetAppVersion(""));
        if(len > 0 && (size_t)len < sizeof(raUserAgent))
            rc_client_get_user_agent_clause(raClient, raUserAgent + len, sizeof(raUserAgent) - len);
    }
    raHttpSetUserAgent(raUserAgent);

    // start the network worker (do_frame-originated calls run off the emu thread)
    raHttpInitialize();

    // auto-login with the stored token (synchronous)
    if(settings3DS.RAUsername[0] && settings3DS.RAToken[0]) {
        raHttpSetSyncMode(true);
        rc_client_begin_login_with_token(raClient, settings3DS.RAUsername,
                                         settings3DS.RAToken, raLoginCallback, NULL);
        raHttpSetSyncMode(false);
    }

    log3dsWrite("[RA] rc_client initialized (softcore)");
}

void ra3dsFinalize()
{
    raHttpFinalize();

    if(raClient) {
        rc_client_destroy(raClient);
        raClient = NULL;
    }

    free(badgeJobs);
    badgeJobs = NULL;
}
