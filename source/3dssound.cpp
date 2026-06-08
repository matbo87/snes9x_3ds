#include <stdio.h>
#include <string.h>

#include "snes9x.h"
#include "spc700.h"
#include "apu.h"
#include "soundux.h"

#include "3ds.h"
#include "3dsgpu.h"
#include "3dssound.h"
#include "3dstimer.h"
#include "3dsimpl.h"
#include "3dslog.h"


SSND3DS snd3DS;

// O3DS/O2DS syscore (core1) CPU budget for the mixing thread
#define SND3DS_O3DS_CPU_LIMIT  60

static u32  oldCpuLimit      = UINT32_MAX;
static bool oldCpuLimitSaved = false;

// Staging buffers for the SNES9x mixer — it writes separate L/R streams
// and we interleave into the NDSP wavebuf.
static short stagingL[SND3DS_SAMPLES_PER_LOOP];
static short stagingR[SND3DS_SAMPLES_PER_LOOP];


//---------------------------------------------------------
// NDSP frame callback,runs on the DSP service thread.
// Just wakes the mixer — keeps critical sections off this thread.
//---------------------------------------------------------
static void snd3dsNdspFrameCallback(void *user)
{
    LightEvent_Signal(&snd3DS.ndspFrameEvent);
}


//---------------------------------------------------------
// Refill and re-queue every free wavebuf. 
// `LightLock` so Drain/Start/Stop can synchronise against the SNES-state read.
//---------------------------------------------------------
void snd3dsMixSamples()
{
    LightLock_Lock(&snd3DS.snesAccessLock);

    while (true)
    {
        ndspWaveBuf *wb = &snd3DS.waveBufs[snd3DS.fillBlock];

        if (wb->status != NDSP_WBUF_DONE && wb->status != NDSP_WBUF_FREE)
            break;

        bool generateSound = snd3DS.isPlaying && !snd3DS.generateSilence;
        short *dst = (short *)wb->data_vaddr;
        int frames = SND3DS_SAMPLES_PER_LOOP;

        if (generateSound)
        {
            impl3dsGenerateSoundSamples();
            if (settings3DS.TurboMode)
            {
                memset(dst, 0, (size_t)frames * 2 * sizeof(short));
            }
            else
            {
                impl3dsOutputSoundSamples(stagingL, stagingR);

                for (int i = 0; i < frames; i++)
                {
                    dst[i * 2]     = stagingL[i];
                    dst[i * 2 + 1] = stagingR[i];
                }
            }
        }
        else
        {
            memset(dst, 0, (size_t)frames * 2 * sizeof(short));
        }
        
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)dst, (u32)(frames * 2 * sizeof(short)));

        ndspChnWaveBufAdd(0, wb);

        snd3DS.fillBlock = (snd3DS.fillBlock + 1) % SND3DS_WAVEBUF_COUNT;
    }

    LightLock_Unlock(&snd3DS.snesAccessLock);
}


//---------------------------------------------------------
// Mixing thread entry point.
//
// Blocks on ndspFrameEvent.
//---------------------------------------------------------
static void snd3dsMixingThread(void *p)
{
    while (!snd3DS.terminateMixingThread)
    {
        LightEvent_Wait(&snd3DS.ndspFrameEvent);
        snd3dsMixSamples();
    }
}


//---------------------------------------------------------
// Start playing the samples.
//
// Clear NDSP's queue before resetting our local status array 
// to keep the two in sync.
//---------------------------------------------------------
void snd3dsStartPlaying()
{
    if (snd3DS.audioType != 2)
        return;

    // Lock so the mixer can't be mid-iteration while we clear the queue.
    LightLock_Lock(&snd3DS.snesAccessLock);

    if (snd3DS.isPlaying)
    {
        LightLock_Unlock(&snd3DS.snesAccessLock);
        return;
    }

    ndspChnWaveBufClear(0);

    for (int i = 0; i < SND3DS_WAVEBUF_COUNT; i++)
    {
        snd3DS.waveBufs[i].status = NDSP_WBUF_DONE;
    }

    snd3DS.fillBlock = 0;

    snd3DS.isPlaying = true;
    snd3DS.generateSilence = false;

    LightLock_Unlock(&snd3DS.snesAccessLock);

    // same as snd3dsInitialize:
    // ensures the mixer wakes and refills the cleared queue 
    // even if no NDSP callback is currently pending.
    LightEvent_Signal(&snd3DS.ndspFrameEvent);
}


//---------------------------------------------------------
// Stop playing the samples.
//
// Clear NDSP's queue before resetting our local status array 
// to keep the two in sync.
//---------------------------------------------------------
void snd3dsStopPlaying()
{
    // Lock so the mixer can't be mid-iteration while we clear the queue.
    LightLock_Lock(&snd3DS.snesAccessLock);

    if (!snd3DS.isPlaying)
    {
        LightLock_Unlock(&snd3DS.snesAccessLock);
        return;
    }

    ndspChnWaveBufClear(0);

    for (int i = 0; i < SND3DS_WAVEBUF_COUNT; i++)
    {
        snd3DS.waveBufs[i].status = NDSP_WBUF_DONE;
    }

    snd3DS.isPlaying = false;

    LightLock_Unlock(&snd3DS.snesAccessLock);
}


//---------------------------------------------------------
// Pause the mixing thread from touching SNES state.
// Sets generateSilence so the next iteration emits zeros,
// then takes/releases the lock to wait out any iteration already in flight.
//
// Replaces the prior 30ms svcSleepThread, which a slow mix
// iteration could outrun and crash on ROM switch.
//---------------------------------------------------------
void snd3dsDrainMixing()
{
    snd3DS.generateSilence = true;

    // Barrier: ensures the mixer is not currently reading SNES state.
    LightLock_Lock(&snd3DS.snesAccessLock);
    LightLock_Unlock(&snd3DS.snesAccessLock);
}


void snd3dsResumeMixing()
{
    snd3DS.generateSilence = false;
}


//---------------------------------------------------------
// Initialize the NDSP audio pipeline.
//
// Returns true on success. On failure (ndspInit fails because
// dspfirm.cdc is absent, or service blocked) the app continues
// silently — audio is not a hard dependency.
//---------------------------------------------------------
bool snd3dsInitialize()
{
    S9xSetPlaybackRate(SND3DS_SAMPLE_RATE);

    snd3DS.isPlaying = false;
    snd3DS.audioType = 0;

    LightLock_Init(&snd3DS.snesAccessLock);
    LightEvent_Init(&snd3DS.ndspFrameEvent, RESET_ONESHOT);

    Result ret = ndspInit();
    log3dsWrite("ndspInit ret = 0x%lx", (unsigned long)ret);

    if (R_FAILED(ret))
    {
        log3dsWrite("NDSP init failed — continuing without audio");
        return true;
    }

    snd3DS.audioType = 2;

    // Single linearAlloc covers all wavebufs back-to-back.
    int framesPerBuffer = SND3DS_SAMPLES_PER_LOOP;
    int bytesPerBuffer = framesPerBuffer * 2 * sizeof(short); // stereo PCM16
    int totalBytes = bytesPerBuffer * SND3DS_WAVEBUF_COUNT;

    snd3DS.pcmBuffer = (short *)linearAlloc(totalBytes);
    if (!snd3DS.pcmBuffer)
    {
        log3dsWrite("NDSP linearAlloc failed (%d bytes)", totalBytes);
        ndspExit();
        snd3DS.audioType = 0;
        return false;
    }
    memset(snd3DS.pcmBuffer, 0, totalBytes);

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspSetOutputCount(1);
    ndspSetMasterVol(1.0f);

    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, (float)SND3DS_SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    float stereoMix[12] = { 1.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    ndspChnSetMix(0, stereoMix);

    memset(snd3DS.waveBufs, 0, sizeof(snd3DS.waveBufs));
    for (int i = 0; i < SND3DS_WAVEBUF_COUNT; i++)
    {
        snd3DS.waveBufs[i].data_vaddr = snd3DS.pcmBuffer + (i * framesPerBuffer * 2);
        snd3DS.waveBufs[i].nsamples = framesPerBuffer;
        snd3DS.waveBufs[i].looping = false;
        snd3DS.waveBufs[i].status = NDSP_WBUF_FREE;
    }
    snd3DS.fillBlock = 0;

    log3dsWrite("NDSP ready: %d Hz stereo PCM16, %d wavebufs x %d frames",
        SND3DS_SAMPLE_RATE, SND3DS_WAVEBUF_COUNT, framesPerBuffer);

    snd3DS.terminateMixingThread = false;

    int coreId = 1;

    if (GPU3DS.isReal3DS)
    {
        if (settings3DS.isNew3DS)
        {
            coreId = 2;
        }
        else
        {
            APT_GetAppCpuTimeLimit(&oldCpuLimit);

            // core1 (syscore) via APT_SetAppCpuTimeLimit, fallback to core0
            if (R_SUCCEEDED(APT_SetAppCpuTimeLimit(SND3DS_O3DS_CPU_LIMIT)))
            {
                coreId = 1;
                oldCpuLimitSaved = true;
            }
            else
                coreId = 0;

            log3dsWrite("snd3dsInit - SetAppCpuTimeLimit: %u (old: %u)", SND3DS_O3DS_CPU_LIMIT, oldCpuLimit);
        }
    }

    IAPU.DSPReplayIndex = 0;
    IAPU.DSPWriteIndex = 0;

    snd3DS.mixingThread = threadCreate(snd3dsMixingThread, NULL, 0x4000, 0x18, coreId, false);
    if (snd3DS.mixingThread == NULL)
    {
        log3dsWrite("Unable to start mixing thread");
        snd3dsFinalize();
        return false;
    }

    // Signal once to make the mixer fill+queue all wavebufs
    LightEvent_Signal(&snd3DS.ndspFrameEvent);

    // Mixer is up -> register callback
    ndspSetCallback(snd3dsNdspFrameCallback, NULL);

    log3dsWrite("Mixing thread started: %lx", (unsigned long)threadGetHandle(snd3DS.mixingThread));
    return true;
}


//---------------------------------------------------------
// Finalize the audio pipeline.
//---------------------------------------------------------
void snd3dsFinalize()
{
    snd3DS.terminateMixingThread = true;

    // Wake the mixer so it observes terminateMixingThread and exits,
    // instead of blocking on a callback that may never fire post-teardown.
    LightEvent_Signal(&snd3DS.ndspFrameEvent);

    if (snd3DS.mixingThread)
    {
        log3dsWrite("join mixing thread");
        threadJoin(snd3DS.mixingThread, 1000 * 1000000);
        threadFree(snd3DS.mixingThread);
        snd3DS.mixingThread = NULL;
    }

    if (snd3DS.audioType == 2)
    {
        log3dsWrite("ndspExit");
        // Drop callback before teardown so it can't fire into freed state.
        ndspSetCallback(NULL, NULL);
        ndspChnWaveBufClear(0);
        ndspExit();
        snd3DS.audioType = 0;
    }

    if (snd3DS.pcmBuffer)
    {
        log3dsWrite("linearFree snd3DS.pcmBuffer");
        linearFree(snd3DS.pcmBuffer);
        snd3DS.pcmBuffer = NULL;
    }
    snd3dsRestoreCpuLimit();
}

void snd3dsApplyCpuLimit()
{
    if (oldCpuLimitSaved)
        APT_SetAppCpuTimeLimit(SND3DS_O3DS_CPU_LIMIT);
}

void snd3dsRestoreCpuLimit()
{
    if (oldCpuLimitSaved && oldCpuLimit != UINT32_MAX)
        APT_SetAppCpuTimeLimit(oldCpuLimit);
}
