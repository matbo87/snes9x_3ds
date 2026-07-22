#include "3dsretroachievements.h"
#include "3dslog.h"

#include "rc_client.h"
#include "rc_hash.h"

#include "memmap.h"

#include <3ds.h>
#include <string.h>

static rc_client_t *raClient = NULL;

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
// Server call.
// TODO: httpc transport
//---------------------------------------------------------
static void raServerCall(const rc_api_request_t *request,
                         rc_client_server_callback_t callback,
                         void *callbackData, rc_client_t *client)
{
    (void)request; (void)client;

    rc_api_server_response_t response;
    memset(&response, 0, sizeof(response));
    response.http_status_code = 0; // no transport yet
    callback(&response, callbackData);
}

//---------------------------------------------------------
// rcheevos logging + event callbacks.
//---------------------------------------------------------
static void raLogCallback(const char *message, const rc_client_t *client)
{
    (void)client;
    log3dsWrite("[RA] %s\n", message);
}

static void raEventHandler(const rc_client_event_t *event, rc_client_t *client)
{
    (void)client;

    // TODO: popups / unlock ui
    if(event->type == RC_CLIENT_EVENT_RESET)
        ra3dsReset();
}

//---------------------------------------------------------
// Public API.
//---------------------------------------------------------
void ra3dsInitialize()
{
    raClient = rc_client_create(raReadMemory, raServerCall);
    if(!raClient) {
        log3dsWrite("[RA] rc_client_create failed\n");
        return;
    }

    rc_client_enable_logging(raClient, RC_CLIENT_LOG_LEVEL_INFO, raLogCallback);
    rc_client_set_event_handler(raClient, raEventHandler);

    // hardcore mode is out of scope for now
    rc_client_set_hardcore_enabled(raClient, 0);

    log3dsWrite("[RA] rc_client initialized (softcore)\n");
}

void ra3dsFinalize()
{
    if(raClient) {
        rc_client_destroy(raClient);
        raClient = NULL;
    }
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
        log3dsWrite("[RA] hash generation failed\n");
        return;
    }

    log3dsWrite("[RA] ROM hash: %s (size %lu)\n", hash,
                (unsigned long)Memory.CalculatedSize);

    // TODO: game identification against the server:
    // rc_client_begin_load_game(raClient, hash, ...);
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
    if(raClient)
        rc_client_do_frame(raClient);
}

bool ra3dsIsLoggedIn()
{
    return raClient && rc_client_get_user_info(raClient) != NULL;
}
