//=============================================================================
// Savestate slots, quick save/load and the save worker that writes
// savestate files off the main thread.
//=============================================================================

#include <atomic>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

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

static bool slotHasSavestate[SAVESLOTS_MAX];

// Save worker reserve: serialized state, savestate screenshot pixels, PNG stream buffer.
#define SAVE_WORKER_STATE_MAX        (640 * 1024)
#define SAVE_WORKER_SHOT_MAX         ((SNES_WIDTH / 2) * (SNES_HEIGHT_EXTENDED / 2) * 3)
#define SAVE_WORKER_PNG_BUFFER_SIZE  (32 * 1024)
#define SAVE_WORKER_BUFFER_SIZE      (SAVE_WORKER_STATE_MAX + SAVE_WORKER_SHOT_MAX + SAVE_WORKER_PNG_BUFFER_SIZE)

static u8* saveWorkerBuffer = NULL;

static void sav3dsGetStateScreenshotDir(char* out, size_t bufferSize)
{
    char basename[NAME_MAX + 1];
    utils3dsGetBasename(Memory.ROMFilename, basename, sizeof(basename), false);
    snprintf(out, bufferSize, "%s/savestates/screenshots/%s", settings3DS.RootDir, basename);
}

void sav3dsGetStateScreenshotPath(int slotNumber, char* out, size_t bufferSize)
{
    char dir[PATH_MAX];
    sav3dsGetStateScreenshotDir(dir, sizeof(dir));
    snprintf(out, bufferSize, "%s/%d.png", dir, slotNumber);
}

static void sav3dsGetSlotPath(int slotNumber, char* out, size_t bufferSize)
{
    char ext[16];
    snprintf(ext, sizeof(ext), ".%d.frz", slotNumber);
    file3dsGetRelatedPath(Memory.ROMFilename, out, bufferSize, ext, "savestates");
}

static void sav3dsGetAutoPath(char* out, size_t bufferSize)
{
    file3dsGetRelatedPath(Memory.ROMFilename, out, bufferSize, ".auto.frz", "savestates");
}

static bool sav3dsSlotFileExists(int slotNumber)
{
    char path[PATH_MAX];
    sav3dsGetSlotPath(slotNumber, path, sizeof(path));
    return IsFileExists(path);
}

void sav3dsEnsureStateScreenshotDir()
{
    char dir[PATH_MAX];
    sav3dsGetStateScreenshotDir(dir, sizeof(dir));
    mkdir(dir, 0777);
}

void sav3dsDeleteStateScreenshots()
{
    char dir[PATH_MAX];
    sav3dsGetStateScreenshotDir(dir, sizeof(dir));

    DIR* d = opendir(dir);
    if (d) {
        struct dirent* entry;

        while ((entry = readdir(d)) != NULL) {
            if (entry->d_name[0] == '.')
                continue;

            char path[PATH_MAX];
            size_t dirLen = strlen(dir);
            size_t nameLen = strlen(entry->d_name);
            if (dirLen + 1 + nameLen >= sizeof(path))
                continue;

            memcpy(path, dir, dirLen);
            path[dirLen] = '/';
            memcpy(path + dirLen + 1, entry->d_name, nameLen + 1);
            remove(path);
        }

        closedir(d);
        rmdir(dir);
    }

    img3dsInvalidateStateScreenshot();
}

static bool sav3dsSaveState(const char* filename)
{
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

    return Snapshot(filename);
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
    if (sav3dsSaveState(path)) {
        log3dsWrite("saving to slot %d succeeded", slotNumber);

        sav3dsUpdateSlotState(slotNumber);
        return true;
    }

    log3dsWrite("saving to slot %d failed", slotNumber);

    session3DS.sdWriteFailPending = sdWriteFailWasPending;
    return false;
}

void sav3dsSaveAuto()
{
    if (!session3DS.isRomLoaded || !settings3DS.AutoSavestate)
        return;

    char path[PATH_MAX];
    sav3dsGetAutoPath(path, sizeof(path));
    sav3dsSaveState(path);
}

bool sav3dsLoadSlot(int slotNumber)
{
    char path[PATH_MAX];
    sav3dsGetSlotPath(slotNumber, path, sizeof(path));

    bool success = sav3dsLoadState(path);

    if (success) {
        log3dsWrite("loading slot %d succeeded", slotNumber);
    } else {
        log3dsWrite("loading slot %d failed", slotNumber);
    }

    return success;
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

//---------------------------------------------------------
// Save worker: for a quick save the main thread serializes the state and
// captures the screenshot into saveWorkerBuffer; the worker only writes files.
// The worker never touches session3DS, g_fileBuffer or g_streamBuffer.
//---------------------------------------------------------

enum QuickSaveResult {
    QUICKSAVE_OK,
    QUICKSAVE_STATE_FAILED,
    QUICKSAVE_SHOT_FAILED,  // state saved; savestate screenshots report nothing
};

struct QuickSaveJob {
    char statePath[PATH_MAX];
    char shotPath[PATH_MAX];
    size_t stateSize;
    bool stateExists;  // from slotHasSavestate
    int shotWidth;  // 0 = no screenshot
    int shotHeight;
    int slot;
    QuickSaveResult result;
};

static QuickSaveJob quickSaveJob;
static Thread saveWorkerThread = NULL;
static LightEvent saveWorkerJobEvent;    // main -> worker: job ready or terminate
static LightEvent saveWorkerDoneEvent;   // worker -> main: job finished
static std::atomic<bool> saveWorkerTerminate{false};
static bool saveInProgress = false;      // main thread only
static bool homeAllowedBeforeSave, sleepAllowedBeforeSave;

static void saveWorkerWriteFiles(QuickSaveJob* job)
{
    job->result = QUICKSAVE_STATE_FAILED;

    // Overwriting in place keeps the file's clusters; "wb" frees and reallocates them on every save.
    FILE* fp = fopen(job->statePath, job->stateExists ? "r+b" : "wb");
    if (!fp) return;
    bool ok = fwrite(saveWorkerBuffer, 1, job->stateSize, fp) == job->stateSize;
    // A smaller state would leave the old tail behind.
    if (job->stateExists)
        ok = fflush(fp) == 0 && ftruncate(fileno(fp), job->stateSize) == 0 && ok;
    ok = fclose(fp) == 0 && ok;
    if (!ok) return;

    job->result = QUICKSAVE_OK;
    if (!job->shotWidth) return;

    fp = fopen(job->shotPath, "wb");
    if (!fp) {
        job->result = QUICKSAVE_SHOT_FAILED;
        return;
    }
    // Batch libpng's small writes.
    setvbuf(fp, (char*)saveWorkerBuffer + SAVE_WORKER_STATE_MAX + SAVE_WORKER_SHOT_MAX, _IOFBF, SAVE_WORKER_PNG_BUFFER_SIZE);
    ok = savePng(fp, saveWorkerBuffer + SAVE_WORKER_STATE_MAX, job->shotWidth, job->shotHeight);
    ok = fclose(fp) == 0 && ok;
    if (!ok) job->result = QUICKSAVE_SHOT_FAILED;
}

static void saveWorkerMain(void* arg)
{
    while (true)
    {
        LightEvent_Wait(&saveWorkerJobEvent);
        if (saveWorkerTerminate) break;

        saveWorkerWriteFiles(&quickSaveJob);
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

    // Below main on core 0 the worker starves in CPU-bound games.
    saveWorkerThread = threadCreate(saveWorkerMain, NULL, 0x8000, THREAD_PRIORITY_SD_WORKER, device3DS.secondaryCore, false);
    if (!saveWorkerThread) {
        log3dsWrite("Unable to start save worker thread");
        return false;
    }

    return true;
}

// emulatorLoop drains pending saves before finalization.
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

static void sav3dsReportQuickSaveResult(const QuickSaveJob& job)
{
    // sdWriteFailPending stays untouched: the slot message covers the failure.
    if (job.result == QUICKSAVE_STATE_FAILED) {
        log3dsWrite("saving to slot %d failed", job.slot);

        char message[64];
        snprintf(message, sizeof(message), "Unable to save into Slot #%d!", job.slot);
        notif3dsTrigger(Notif::Misc, Notif::Type::Error, NOTIF_DEFAULT_DURATION, message);
        return;
    }

    log3dsWrite("saving to slot %d succeeded", job.slot);

    // Update the slot cache without another SD lookup.
    slotHasSavestate[job.slot - 1] = true;
    menu3dsMarkTabDirty(TAB_EMULATOR);

    if (job.shotWidth) {
        log3dsWrite("screenshot saved %s: %s", job.shotPath, job.result == QUICKSAVE_OK ? "v" : "x");
        if (job.result == QUICKSAVE_OK)
            img3dsInvalidateStateScreenshot();
    }

    notif3dsTrigger(Notif::SaveState, Notif::Type::Success);
}

static void sav3dsRestoreHomeAndSleep()
{
    aptSetHomeAllowed(homeAllowedBeforeSave);
    aptSetSleepAllowed(sleepAllowedBeforeSave);
}

static void sav3dsStartQuickSave(int slot)
{
    QuickSaveJob& job = quickSaveJob;
    job.slot = slot;

    // Disable before capture; a HOME press latched earlier still suspends mid-job.
    homeAllowedBeforeSave = aptIsHomeAllowed();
    sleepAllowedBeforeSave = aptIsSleepAllowed();
    aptSetHomeAllowed(false);
    aptSetSleepAllowed(false);

    snd3dsDrainMixing();
    job.stateSize = S9xFreezeToMemory(saveWorkerBuffer, SAVE_WORKER_STATE_MAX);
    snd3dsResumeMixing();

    // Only an oversized RA progress block gets here.
    if (!job.stateSize) {
        log3dsWrite("quick save: state exceeds %d bytes", SAVE_WORKER_STATE_MAX);
        sav3dsRestoreHomeAndSleep();
        job.result = QUICKSAVE_STATE_FAILED;
        sav3dsReportQuickSaveResult(job);
        return;
    }

    notif3dsTriggerPersistent(Notif::SavingState, Notif::Type::Success);

    sav3dsGetSlotPath(slot, job.statePath, sizeof(job.statePath));
    job.stateExists = slotHasSavestate[slot - 1];
    job.shotWidth = job.shotHeight = 0;

    if (settings3DS.SaveStateScreenshots) {
        screenshot.type = SCREENSHOT_SAVESTATE;
        screenshot.slot = slot;
        if (impl3dsTakeScreenshot(job.shotPath, sizeof(job.shotPath), true,
                saveWorkerBuffer + SAVE_WORKER_STATE_MAX, SAVE_WORKER_SHOT_MAX)) {
            job.shotWidth = screenshot.width;
            job.shotHeight = screenshot.height;
        }
    }

    saveInProgress = true;
    LightEvent_Signal(&saveWorkerJobEvent);
}

bool sav3dsIsSaveInProgress() {
    return saveInProgress;
}

void sav3dsProcessSaveCompletion(bool waitForCompletion) {
    if (!saveInProgress) return;

    if (waitForCompletion)
        LightEvent_Wait(&saveWorkerDoneEvent);
    else if (!LightEvent_TryWait(&saveWorkerDoneEvent))
        return;

    saveInProgress = false;
    sav3dsRestoreHomeAndSleep();
    sav3dsReportQuickSaveResult(quickSaveJob);
}

// False while the mixer is drained, i.e. the menu was requested earlier in this input scan.
static bool sav3dsPrepareQuickSlot()
{
    if (snd3DS.generateSilence) return false;

    if (settings3DS.CurrentSaveSlot <= 0) {
        settings3DS.CurrentSaveSlot = 1;
    }
    return true;
}

void sav3dsQuickSave() {
    if (sav3dsPrepareQuickSlot())
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
