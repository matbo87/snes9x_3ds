// Savestates, screenshots and background SD writes.

#include <atomic>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
#include "png_utils.h"

// Fixed-size THM block at EOF: big-endian u16 width/height, then RGB565 pixels
// in column-major order, bottom row first.
static_assert(SAVE_THUMBNAIL_WIDTH == SNES_WIDTH / 2 && SAVE_THUMBNAIL_MAX_HEIGHT == SNES_HEIGHT_EXTENDED / 2,
    "thumbnail size must match the half-size screenshot region");
#define SAVE_THUMBNAIL_DATA_SIZE     (4 + SAVE_THUMBNAIL_PIXELS_SIZE)
#define SAVE_THUMBNAIL_HEADER_SIZE   11  // "THM:<6 digits>:"
#define SAVE_THUMBNAIL_BLOCK_SIZE    (SAVE_THUMBNAIL_HEADER_SIZE + SAVE_THUMBNAIL_DATA_SIZE)

// Largest state without RA: ~497 KB (SPC7110 + RTC); the rest is for the RA block
// (Super Bomberman + RA + thumbnail: 431 + 159 + 30 ≈ 620 KB)
#define SAVE_WORKER_STATE_MAX        (640 * 1024)
#define SAVE_WORKER_BUFFER_SIZE      (SAVE_WORKER_STATE_MAX + SAVE_THUMBNAIL_PIXELS_SIZE)

// Manual screenshots use the state region: RGB8 pixels, then the PNG stream buffer.
#define SAVE_SCREENSHOT_PIXELS_MAX   (SNES_WIDTH * SNES_HEIGHT_EXTENDED * 3)
#define SAVE_SCREENSHOT_STREAM_OFFSET \
    ((SAVE_SCREENSHOT_PIXELS_MAX + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1))
#define SAVE_SCREENSHOT_STREAM_SIZE  (32 * 1024)
static_assert(SAVE_SCREENSHOT_STREAM_OFFSET + SAVE_SCREENSHOT_STREAM_SIZE <= SAVE_WORKER_STATE_MAX,
    "screenshot does not fit the state region");

#define SAVE_TOAST_MIN_MS            500

typedef enum {
    SAVE_JOB_QUICK_SAVE,
    SAVE_JOB_SCREENSHOT,
    SAVE_JOB_SRAM,
} SaveJobType;

typedef struct {
    SaveJobType type;
    char path[PATH_MAX];
    size_t size;
    int slot;           // quick save only
    int width, height;  // screenshot image, or quick save thumbnail (0 = none)
    SaveWriteResult result;
} SaveJob;

static bool slotHasSavestate[SAVESLOTS_MAX];

static u8* saveWorkerBuffer = NULL;
static Thread saveWorkerThread = NULL;
static LightEvent saveWorkerJobEvent;    // main -> worker: job ready or terminate
static LightEvent saveWorkerDoneEvent;   // worker -> main: job finished
static std::atomic<bool> saveWorkerTerminate{false};

// Main publishes the job and buffer through saveWorkerJobEvent; both remain
// reserved until main collects saveWorkerDoneEvent.
static SaveJob saveJob;

// Main thread only.
static bool saveInProgress = false;
static u64 saveJobStartedAt;
static bool sramSavePending = false;
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

static void sav3dsGetScreenshotPath(char* out, size_t bufferSize)
{
    time_t rawtime = time(NULL);
    struct tm* t = localtime(&rawtime);
    char suffix[64];
    strftime(suffix, sizeof(suffix), ".%Y%m%d_%H%M%S.png", t);
    file3dsGetRelatedPath(Memory.ROMFilename, out, bufferSize, suffix, "screenshots");
}

static bool sav3dsSlotFileExists(int slotNumber)
{
    char path[PATH_MAX];
    sav3dsGetSlotPath(slotNumber, path, sizeof(path));
    return IsFileExists(path);
}

// File writers require exclusive access to saveWorkerBuffer.
static SaveWriteResult sav3dsWriteBufferToFile(const char* path, size_t size)
{
    FILE* fp = fopen(path, "wb");
    if (!fp) return SAVE_WRITE_SKIPPED;

    bool ok = fwrite(saveWorkerBuffer, 1, size, fp) == size;
    return fclose(fp) == 0 && ok ? SAVE_WRITE_OK : SAVE_WRITE_FAILED;
}

// Loading a truncated state can reset the game before detecting missing blocks.
static SaveWriteResult sav3dsWriteState(const char* path, size_t size)
{
    SaveWriteResult result = sav3dsWriteBufferToFile(path, size);
    if (result == SAVE_WRITE_FAILED)
        remove(path);
    return result;
}

static SaveWriteResult sav3dsWriteScreenshot(const char* path, int width, int height)
{
    FILE* fp = fopen(path, "wb");
    if (!fp) return SAVE_WRITE_SKIPPED;

    setvbuf(fp, (char*)saveWorkerBuffer + SAVE_SCREENSHOT_STREAM_OFFSET, _IOFBF, SAVE_SCREENSHOT_STREAM_SIZE);
    bool ok = savePng(fp, saveWorkerBuffer, width, height);
    return fclose(fp) == 0 && ok ? SAVE_WRITE_OK : SAVE_WRITE_FAILED;
}

static void sav3dsFormatThumbnailHeader(char* out, size_t bufferSize)
{
    snprintf(out, bufferSize, "THM:%06d:", SAVE_THUMBNAIL_DATA_SIZE);
}

// The THM block may be unaligned. Separate scratch pixels stay intact until
// quick-save completion copies them into the thumbnail cache.
static u16* sav3dsGetThumbnailPixels()
{
    return (u16*)(saveWorkerBuffer + SAVE_WORKER_STATE_MAX);
}

// Main thread only. An omitted thumbnail leaves stateSize unchanged and returns zero dimensions.
static size_t sav3dsAppendThumbnail(size_t stateSize, int* thumbWidth, int* thumbHeight)
{
    *thumbWidth = *thumbHeight = 0;

    if (stateSize + SAVE_THUMBNAIL_BLOCK_SIZE > SAVE_WORKER_STATE_MAX) {
        log3dsWrite("save state: no room for the thumbnail");
        return stateSize;
    }

    u16* pixels = sav3dsGetThumbnailPixels();
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

    *thumbWidth = width;
    *thumbHeight = height;
    return stateSize + SAVE_THUMBNAIL_BLOCK_SIZE;
}

// Main thread only.
static void sav3dsFinishSlotWrite(int slot, SaveWriteResult result, int thumbWidth, int thumbHeight)
{
    switch (result) {
    case SAVE_WRITE_OK:
        slotHasSavestate[slot - 1] = true;
        img3dsSetStateThumbnail(slot, thumbWidth ? sav3dsGetThumbnailPixels() : NULL, thumbWidth, thumbHeight);
        break;
    case SAVE_WRITE_FAILED:
        slotHasSavestate[slot - 1] = false;
        img3dsInvalidateStateThumbnail(slot);
        break;
    case SAVE_WRITE_SKIPPED:
        return;
    }
    menu3dsMarkTabDirty(TAB_EMULATOR);
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

// thumbWidth/thumbHeight (optional) receive the appended thumbnail size, 0 = none.
static SaveWriteResult sav3dsSaveState(const char* filename, bool withThumbnail,
    int* thumbWidth = nullptr, int* thumbHeight = nullptr)
{
    if (filename == nullptr || filename[0] == '\0') {
        return SAVE_WRITE_SKIPPED;
    }

    // Wait before reusing the worker's buffer.
    sav3dsProcessSaveCompletion(true);

    size_t size = S9xFreezeToMemory(saveWorkerBuffer, SAVE_WORKER_STATE_MAX);
    if (!size) {
        log3dsWrite("save state: state exceeds %d bytes", SAVE_WORKER_STATE_MAX);
        session3DS.sdWriteFailPending = true;
        return SAVE_WRITE_SKIPPED;
    }

    int width = 0, height = 0;
    if (withThumbnail)
        size = sav3dsAppendThumbnail(size, &width, &height);

    SaveWriteResult result = sav3dsWriteState(filename, size);
    if (result != SAVE_WRITE_OK) {
        session3DS.sdWriteFailPending = true;
        return result;
    }

    if (thumbWidth) *thumbWidth = width;
    if (thumbHeight) *thumbHeight = height;
    return SAVE_WRITE_OK;
}

static bool sav3dsLoadState(const char* filename)
{
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

    bool success = S9xLoadSnapshot(filename);
    if (success) {
        gpu3dsInitializeMode7Vertexes();
    }
    return success;
}

SaveWriteResult sav3dsSaveSlot(int slotNumber)
{
    char path[PATH_MAX];
    sav3dsGetSlotPath(slotNumber, path, sizeof(path));

    const bool sdWriteFailWasPending = session3DS.sdWriteFailPending;
    int thumbWidth = 0, thumbHeight = 0;
    SaveWriteResult result = sav3dsSaveState(path, settings3DS.SaveStateScreenshots, &thumbWidth, &thumbHeight);
    sav3dsFinishSlotWrite(slotNumber, result, thumbWidth, thumbHeight);
    // The slot dialog reports this failure; preserve unrelated pending SD errors.
    if (result != SAVE_WRITE_OK)
        session3DS.sdWriteFailPending = sdWriteFailWasPending;
    return result;
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

// Worker code must not use session3DS, g_fileBuffer or g_streamBuffer.
static void saveWorkerMain(void* arg)
{
    while (true)
    {
        LightEvent_Wait(&saveWorkerJobEvent);
        if (saveWorkerTerminate) break;

        if (saveJob.type == SAVE_JOB_SCREENSHOT)
            saveJob.result = sav3dsWriteScreenshot(saveJob.path, saveJob.width, saveJob.height);
        else if (saveJob.type == SAVE_JOB_QUICK_SAVE)
            saveJob.result = sav3dsWriteState(saveJob.path, saveJob.size);
        else
            saveJob.result = sav3dsWriteBufferToFile(saveJob.path, saveJob.size);
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
    sav3dsFinishSlotWrite(job.slot, job.result, job.width, job.height);

    // Report via toast without changing pending errors from other SD writes.
    if (job.result != SAVE_WRITE_OK) {
        char message[NOTIF_TEXT_MAX];
        if (job.result == SAVE_WRITE_FAILED)
            snprintf(message, sizeof(message), "Slot #%d save failed. Damaged save removed. Check SD card.", job.slot);
        else
            snprintf(message, sizeof(message), "Unable to save into Slot #%d!", job.slot);
        notif3dsResolvePersistent(Notif::Misc, Notif::Type::Error, NOTIF_DEFAULT_DURATION, message);
        return;
    }

    notif3dsResolvePersistent(Notif::SaveState, Notif::Type::Success);
}

static void sav3dsReportScreenshotResult(const SaveJob& job)
{
    if (job.result == SAVE_WRITE_OK) {
        notif3dsResolvePersistent(Notif::Screenshot, Notif::Type::Success);
    } else {
        log3dsWrite("screenshot failed: %s", job.path);
        notif3dsResolvePersistent(Notif::Misc, Notif::Type::Error, NOTIF_DEFAULT_DURATION, "Failed to save screenshot!");
    }
}

static void sav3dsReportSRAMSaveResult(const SaveJob& job)
{
    if (job.result == SAVE_WRITE_OK) return;

    log3dsWrite("SRAM save failed: %s", job.path);
    // Mark SRAM dirty so the auto-save timer retries.
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

static void sav3dsStartWorkerJob(SaveJobType type)
{
    saveJob.type = type;
    saveInProgress = true;
    saveJobStartedAt = svcGetSystemTick();
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
    sav3dsStartWorkerJob(SAVE_JOB_SRAM);
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
        job.result = SAVE_WRITE_SKIPPED;
        sav3dsReportQuickSaveResult(job);
        return;
    }

    notif3dsTriggerPersistent(Notif::SavingState, Notif::Type::Success);

    sav3dsGetSlotPath(slot, job.path, sizeof(job.path));

    job.width = job.height = 0;
    if (settings3DS.SaveStateScreenshots)
        job.size = sav3dsAppendThumbnail(job.size, &job.width, &job.height);

    // Quick save takes priority over a pending hotkey screenshot.
    screenshot.pending = false;
    sav3dsStartWorkerJob(SAVE_JOB_QUICK_SAVE);
}

bool sav3dsIsSaveInProgress() {
    return saveInProgress;
}

bool sav3dsIsQuickSaveOrScreenshotInProgress() {
    return saveInProgress && saveJob.type != SAVE_JOB_SRAM;
}

void sav3dsProcessSaveCompletion(bool waitForCompletion) {
    while (saveInProgress) {
        // Keep the job open for a minimum time so the saving toast doesn't flash.
        if (!waitForCompletion && saveJob.type != SAVE_JOB_SRAM &&
            svcGetSystemTick() - saveJobStartedAt < (u64)(SAVE_TOAST_MIN_MS * CPU_TICKS_PER_MSEC))
            return;

        if (waitForCompletion)
            LightEvent_Wait(&saveWorkerDoneEvent);
        else if (!LightEvent_TryWait(&saveWorkerDoneEvent))
            return;

        saveInProgress = false;
        sav3dsRestoreHomeAndSleep();
        if (saveJob.type == SAVE_JOB_SRAM)
            sav3dsReportSRAMSaveResult(saveJob);
        else if (saveJob.type == SAVE_JOB_SCREENSHOT)
            sav3dsReportScreenshotResult(saveJob);
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

void sav3dsTakeScreenshot() {
    // SRAM can claim the buffer between the hotkey scan and frame capture.
    if (saveInProgress) {
        screenshot.pending = false;
        notif3dsTrigger(Notif::Misc, Notif::Type::Warning, NOTIF_DEFAULT_DURATION, "SRAM save in progress. Try again.");
        return;
    }

    SaveJob& job = saveJob;
    impl3dsCaptureScreenshot(saveWorkerBuffer, false, &job.width, &job.height);
    sav3dsGetScreenshotPath(job.path, sizeof(job.path));
    sav3dsDisableHomeAndSleep();
    notif3dsTriggerPersistent(Notif::SavingScreenshot, Notif::Type::Success);
    sav3dsStartWorkerJob(SAVE_JOB_SCREENSHOT);
}

bool sav3dsTakeMenuScreenshot(char* path, size_t bufferSize) {
    // Wait before reusing the worker's buffer.
    sav3dsProcessSaveCompletion(true);

    int width, height;
    impl3dsCaptureScreenshot(saveWorkerBuffer, true, &width, &height);
    sav3dsGetScreenshotPath(path, bufferSize);
    return sav3dsWriteScreenshot(path, width, height) == SAVE_WRITE_OK;
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
