// Savestate slots and the worker shared by quick saves and SRAM writes.

#include <atomic>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "snes9x.h"
#include "memmap.h"
#include "snapshot.h"

#include "3dssave.h"
#include "3dsimpl.h"
#include "3dsimpl_gpu.h"
#include "3dsfiles.h"
#include "3dsutils.h"
#include "3dslog.h"
#include "3dssettings.h"
#include "3dssound.h"
#include "3dsmenu.h"
#include "3dsui_notif.h"
#include "3dsui_img.h"

// Fixed-size THM block at EOF: big-endian u16 width/height, then RGB565 pixels
// in column-major order, bottom row first. Fixed size allows a single seek.
#define SAVE_THUMBNAIL_WIDTH         (SNES_WIDTH / 2)
#define SAVE_THUMBNAIL_MAX_HEIGHT    (SNES_HEIGHT_EXTENDED / 2)
#define SAVE_THUMBNAIL_PIXELS_SIZE   (SAVE_THUMBNAIL_WIDTH * SAVE_THUMBNAIL_MAX_HEIGHT * 2)
#define SAVE_THUMBNAIL_DATA_SIZE     (4 + SAVE_THUMBNAIL_PIXELS_SIZE)
#define SAVE_THUMBNAIL_HEADER_SIZE   11  // "THM:<6 digits>:"
#define SAVE_THUMBNAIL_BLOCK_SIZE    (SAVE_THUMBNAIL_HEADER_SIZE + SAVE_THUMBNAIL_DATA_SIZE)

// Largest state without RA: ~497 KB (SPC7110 + RTC); the rest is for the RA block
// (Super Bomberman + RA + thumbnail: 431 + 159 + 30 ≈ 620 KB)
#define SAVE_WORKER_STATE_MAX        (640 * 1024)
#define SAVE_WORKER_BUFFER_SIZE      (SAVE_WORKER_STATE_MAX + SAVE_THUMBNAIL_PIXELS_SIZE)

typedef enum {
    SAVE_JOB_QUICK_SAVE,
    SAVE_JOB_SRAM,
} SaveJobType;

typedef struct {
    SaveJobType type;
    char path[PATH_MAX];
    size_t size;
    int slot;       // quick save only
    bool ok;
} SaveJob;

static bool slotHasSavestate[SAVESLOTS_MAX];

static u8* saveWorkerBuffer = NULL;
static Thread saveWorkerThread = NULL;
static LightEvent saveWorkerJobEvent;    // main -> worker: job ready or terminate
static LightEvent saveWorkerDoneEvent;   // worker -> main: job finished
static std::atomic<bool> saveWorkerTerminate{false};

// One job at a time; jobs share the reserve. Set by main before the job event.
static SaveJob saveJob;

// Main thread only.
static bool saveInProgress = false;
static bool sramSavePending = false;     // SRAM timer fired while a job ran
static bool homeAllowedBeforeSave, sleepAllowedBeforeSave;

void sav3dsGetSlotPath(int slotNumber, char* out, size_t bufferSize)
{
    char ext[16];
    snprintf(ext, sizeof(ext), ".%d.frz", slotNumber);
    file3dsGetRelatedPath(Memory.ROMFilename, out, bufferSize, ext, "savestates");
}

static void sav3dsGetAutoPath(char* out, size_t bufferSize)
{
    file3dsGetRelatedPath(Memory.ROMFilename, out, bufferSize, ".auto.frz", "savestates");
}

void sav3dsGetSRAMPath(char* out, size_t bufferSize)
{
    file3dsGetRelatedPath(Memory.ROMFilename, out, bufferSize, ".srm", "saves");
}

static bool sav3dsSlotFileExists(int slotNumber)
{
    char path[PATH_MAX];
    sav3dsGetSlotPath(slotNumber, path, sizeof(path));
    return IsFileExists(path);
}

// Worker thread, or main thread with the worker idle.
static bool sav3dsWriteReserve(const char* path, size_t size)
{
    FILE* fp = fopen(path, "wb");
    if (!fp) return false;

    bool ok = fwrite(saveWorkerBuffer, 1, size, fp) == size;
    return fclose(fp) == 0 && ok;
}

static void sav3dsFormatThumbnailHeader(char* out, size_t bufferSize)
{
    snprintf(out, bufferSize, "THM:%06d:", SAVE_THUMBNAIL_DATA_SIZE);
}

// Main thread only; returns stateSize unchanged if the thumbnail is omitted.
static size_t sav3dsAppendThumbnail(size_t stateSize)
{
    if (stateSize + SAVE_THUMBNAIL_BLOCK_SIZE > SAVE_WORKER_STATE_MAX) {
        log3dsWrite("save state: no room for the thumbnail");
        return stateSize;
    }

    // Use aligned scratch space; the block offset may be odd.
    u16* pixels = (u16*)(saveWorkerBuffer + SAVE_WORKER_STATE_MAX);
    memset(pixels, 0, SAVE_THUMBNAIL_PIXELS_SIZE);

    int width, height;
    if (!impl3dsCaptureThumbnail(pixels, SAVE_THUMBNAIL_PIXELS_SIZE, &width, &height))
        return stateSize;

    char header[16];
    sav3dsFormatThumbnailHeader(header, sizeof(header));

    u8* dst = saveWorkerBuffer + stateSize;
    memcpy(dst, header, SAVE_THUMBNAIL_HEADER_SIZE);
    dst += SAVE_THUMBNAIL_HEADER_SIZE;
    dst[0] = width >> 8;
    dst[1] = width;
    dst[2] = height >> 8;
    dst[3] = height;
    memcpy(dst + 4, pixels, SAVE_THUMBNAIL_PIXELS_SIZE);

    return stateSize + SAVE_THUMBNAIL_BLOCK_SIZE;
}

bool sav3dsReadThumbnail(const char* path, u16* pixels, size_t capacity, int* width, int* height)
{
    FILE* fp = fopen(path, "rb");
    if (!fp) return false;

    char expected[16];
    sav3dsFormatThumbnailHeader(expected, sizeof(expected));

    u8 header[SAVE_THUMBNAIL_HEADER_SIZE + 4];
    bool ok = fseek(fp, -SAVE_THUMBNAIL_BLOCK_SIZE, SEEK_END) == 0 &&
        fread(header, 1, sizeof(header), fp) == sizeof(header) &&
        memcmp(header, expected, SAVE_THUMBNAIL_HEADER_SIZE) == 0;

    if (ok) {
        const u8* size = header + SAVE_THUMBNAIL_HEADER_SIZE;
        *width = (size[0] << 8) | size[1];
        *height = (size[2] << 8) | size[3];
        size_t count = *width * *height;

        ok = *width > 0 && *height > 0 &&
            *width <= SAVE_THUMBNAIL_WIDTH && *height <= SAVE_THUMBNAIL_MAX_HEIGHT &&
            count * sizeof(u16) <= capacity &&
            fread(pixels, sizeof(u16), count, fp) == count;
    }

    fclose(fp);
    return ok;
}

static bool sav3dsSaveState(const char* filename, bool withThumbnail)
{
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

    // Wait before reusing the worker's buffer.
    sav3dsProcessSaveCompletion(true);

    size_t size = S9xFreezeToMemory(saveWorkerBuffer, SAVE_WORKER_STATE_MAX);
    if (!size) {
        log3dsWrite("save state: state exceeds %d bytes", SAVE_WORKER_STATE_MAX);
        session3DS.sdWriteFailPending = true;
        return false;
    }

    if (withThumbnail)
        size = sav3dsAppendThumbnail(size);

    if (!sav3dsWriteReserve(filename, size)) {
        session3DS.sdWriteFailPending = true;
        return false;
    }
    return true;
}

static bool sav3dsLoadState(const char* filename)
{
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

    bool success = S9xLoadSnapshot(filename);
    if (success)
    {
        gpu3dsInitializeMode7Vertexes();
    }
    return success;
}

bool sav3dsSaveSlot(int slotNumber)
{
    char path[PATH_MAX];
    sav3dsGetSlotPath(slotNumber, path, sizeof(path));

    const bool sdWriteFailWasPending = session3DS.sdWriteFailPending;
    if (sav3dsSaveState(path, settings3DS.SaveStateScreenshots)) {
        sav3dsUpdateSlotState(slotNumber);
        img3dsInvalidateStateThumbnail();
        return true;
    }

    session3DS.sdWriteFailPending = sdWriteFailWasPending;
    return false;
}

void sav3dsSaveAuto()
{
    if (!session3DS.isRomLoaded || !settings3DS.AutoSavestate)
        return;

    char path[PATH_MAX];
    sav3dsGetAutoPath(path, sizeof(path));
    sav3dsSaveState(path, false);
}

bool sav3dsLoadSlot(int slotNumber)
{
    char path[PATH_MAX];
    sav3dsGetSlotPath(slotNumber, path, sizeof(path));

    return sav3dsLoadState(path);
}

void sav3dsLoadAuto()
{
    if (!session3DS.isRomLoaded || !settings3DS.AutoSavestate)
        return;

    char path[PATH_MAX];
    sav3dsGetAutoPath(path, sizeof(path));

    snd3dsDrainMixing();
    sav3dsLoadState(path);
    snd3dsResumeMixing();
}

// Main prepares saveWorkerBuffer; the worker only writes files.
// The worker must not use session3DS, g_fileBuffer or g_streamBuffer.

static void saveWorkerMain(void* arg)
{
    while (true)
    {
        LightEvent_Wait(&saveWorkerJobEvent);
        if (saveWorkerTerminate) break;

        saveJob.ok = sav3dsWriteReserve(saveJob.path, saveJob.size);
        LightEvent_Signal(&saveWorkerDoneEvent);
    }
}

bool sav3dsInitialize()
{
    log3dsWrite("allocate save worker buffer (%.2fkb)", float(SAVE_WORKER_BUFFER_SIZE) / 1024);
    saveWorkerBuffer = (u8*)malloc(SAVE_WORKER_BUFFER_SIZE);
    if (!saveWorkerBuffer) {
        log3dsWrite("Unable to allocate save worker buffer");
        return false;
    }

    LightEvent_Init(&saveWorkerJobEvent, RESET_ONESHOT);
    LightEvent_Init(&saveWorkerDoneEvent, RESET_ONESHOT);
    saveWorkerTerminate = false;

    saveWorkerThread = threadCreate(saveWorkerMain, NULL, 0x8000, THREAD_PRIORITY_SD_WORKER, device3DS.secondaryCore, false);
    if (!saveWorkerThread) {
        log3dsWrite("Unable to start save worker thread");
        return false;
    }

    return true;
}

void sav3dsFinalize()
{
    if (saveWorkerThread) {
        saveWorkerTerminate = true;
        LightEvent_Signal(&saveWorkerJobEvent);
        threadJoin(saveWorkerThread, U64_MAX);
        threadFree(saveWorkerThread);
        saveWorkerThread = NULL;
    }

    free(saveWorkerBuffer);
    saveWorkerBuffer = NULL;
}

static void sav3dsReportQuickSaveResult(const SaveJob& job)
{
    // sdWriteFailPending stays untouched: the slot message covers the failure.
    if (!job.ok) {
        char message[64];
        snprintf(message, sizeof(message), "Unable to save into Slot #%d!", job.slot);
        notif3dsTrigger(Notif::Misc, Notif::Type::Error, NOTIF_DEFAULT_DURATION, message);
        return;
    }

    // Update the slot cache without another SD lookup.
    slotHasSavestate[job.slot - 1] = true;
    menu3dsMarkTabDirty(TAB_EMULATOR);
    img3dsInvalidateStateThumbnail();

    notif3dsTrigger(Notif::SaveState, Notif::Type::Success);
}

static void sav3dsReportSRAMSaveResult(const SaveJob& job)
{
    if (job.ok) return;

    log3dsWrite("SRAM save failed: %s", job.path);
    // The next timer retries.
    CPU.SRAMModified = TRUE;
    session3DS.sdWriteFailPending = true;
}

// A HOME press latched before this still suspends mid-job.
static void sav3dsDisableHomeAndSleep()
{
    homeAllowedBeforeSave = aptIsHomeAllowed();
    sleepAllowedBeforeSave = aptIsSleepAllowed();
    aptSetHomeAllowed(false);
    aptSetSleepAllowed(false);
}

static void sav3dsRestoreHomeAndSleep()
{
    aptSetHomeAllowed(homeAllowedBeforeSave);
    aptSetSleepAllowed(sleepAllowedBeforeSave);
}

static void sav3dsSignalJob(SaveJobType type)
{
    saveJob.type = type;
    saveInProgress = true;
    LightEvent_Signal(&saveWorkerJobEvent);
}

static void sav3dsStartSRAMSave()
{
    sramSavePending = false;

    // A synchronous save may have cleared the dirty flag while this job waited.
    if (!CPU.SRAMModified) return;
    CPU.SRAMModified = FALSE;

    SaveJob& job = saveJob;
    job.size = Memory.PrepareSRAMSave();
    sav3dsGetSRAMPath(job.path, sizeof(job.path));
    if (!job.size || job.path[0] == '\0') return;

    memcpy(saveWorkerBuffer, ::SRAM, job.size);
    sav3dsDisableHomeAndSleep();
    sav3dsSignalJob(SAVE_JOB_SRAM);
}

void sav3dsRequestSRAMSave()
{
    // SPC7110 RTC saves a second file synchronously.
    if (Settings.SPC7110RTC) {
        S9xAutoSaveSRAM();
        return;
    }

    if (saveInProgress)
        sramSavePending = true;
    else
        sav3dsStartSRAMSave();
}

static void sav3dsStartQuickSave(int slot)
{
    SaveJob& job = saveJob;
    job.slot = slot;

    sav3dsDisableHomeAndSleep();

    snd3dsDrainMixing();
    job.size = S9xFreezeToMemory(saveWorkerBuffer, SAVE_WORKER_STATE_MAX);
    snd3dsResumeMixing();

    if (!job.size) {
        log3dsWrite("quick save: state exceeds %d bytes", SAVE_WORKER_STATE_MAX);
        sav3dsRestoreHomeAndSleep();
        job.ok = false;
        sav3dsReportQuickSaveResult(job);
        return;
    }

    notif3dsTriggerPersistent(Notif::SavingState, Notif::Type::Success);

    sav3dsGetSlotPath(slot, job.path, sizeof(job.path));

    if (settings3DS.SaveStateScreenshots)
        job.size = sav3dsAppendThumbnail(job.size);

    sav3dsSignalJob(SAVE_JOB_QUICK_SAVE);
}

bool sav3dsIsSaveInProgress() {
    return saveInProgress;
}

bool sav3dsIsQuickSaveInProgress() {
    return saveInProgress && saveJob.type == SAVE_JOB_QUICK_SAVE;
}

void sav3dsProcessSaveCompletion(bool waitForCompletion) {
    while (saveInProgress) {
        if (waitForCompletion)
            LightEvent_Wait(&saveWorkerDoneEvent);
        else if (!LightEvent_TryWait(&saveWorkerDoneEvent))
            return;

        saveInProgress = false;
        sav3dsRestoreHomeAndSleep();
        if (saveJob.type == SAVE_JOB_SRAM)
            sav3dsReportSRAMSaveResult(saveJob);
        else
            sav3dsReportQuickSaveResult(saveJob);

        // Prioritize deferred SRAM over new quick saves.
        if (sramSavePending)
            sav3dsStartSRAMSave();
    }
}

// Reject save/load while the mixer is drained for a menu transition.
static bool sav3dsPrepareQuickSlot()
{
    if (snd3DS.generateSilence) return false;

    if (settings3DS.CurrentSaveSlot <= 0) {
        settings3DS.CurrentSaveSlot = 1;
    }
    return true;
}

void sav3dsQuickSave() {
    if (!sav3dsPrepareQuickSlot()) return;

    if (saveInProgress) {
        notif3dsTrigger(Notif::Misc, Notif::Type::Warning, NOTIF_DEFAULT_DURATION, "SRAM save in progress. Try again.");
        return;
    }
    sav3dsStartQuickSave(settings3DS.CurrentSaveSlot);
}

void sav3dsQuickLoad() {
    if (!sav3dsPrepareQuickSlot()) return;

    snd3dsDrainMixing();
    bool success = sav3dsLoadSlot(settings3DS.CurrentSaveSlot);
    snd3dsResumeMixing();

    if (success) {
        notif3dsTrigger(Notif::LoadState, Notif::Type::Success);
    } else {
        char message[64];
        snprintf(message, sizeof(message), "Unable to load from Slot #%d!", settings3DS.CurrentSaveSlot);
        notif3dsTrigger(Notif::Misc, Notif::Type::Error, NOTIF_DEFAULT_DURATION, message);
    }

    skipNextFpsUpdate = true;
}

bool sav3dsSlotHasState(int slotNumber) {
    return slotHasSavestate[slotNumber - 1];
}

void sav3dsUpdateSlotState(int slotNumber) {
    slotHasSavestate[slotNumber - 1] = sav3dsSlotFileExists(slotNumber);
    menu3dsMarkTabDirty(TAB_EMULATOR);
}

void sav3dsSelectSlot(int direction) {
    if (direction == 1)
        settings3DS.CurrentSaveSlot = settings3DS.CurrentSaveSlot % SAVESLOTS_MAX + 1;
    else
        settings3DS.CurrentSaveSlot = settings3DS.CurrentSaveSlot <= 1 ? SAVESLOTS_MAX : settings3DS.CurrentSaveSlot - 1;

    menu3dsMarkTabDirty(TAB_EMULATOR);
    notif3dsTrigger(Notif::SlotChanged, Notif::Type::Info);
}
