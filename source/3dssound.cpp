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

static int snd3dsSampleRate = 32000;
static int snd3dsSamplesPerLoop = 256;

static u32 old_time_limit = UINT32_MAX;

// Staging buffers for the SNES9x mixer — it writes separate L/R streams
// and we interleave into the NDSP wavebuf. Sized well above the usual
// 256 samplesPerLoop; snd3dsSetSampleRate caps samplesPerLoop at this.
#define SND3DS_STAGING_FRAMES_MAX   2048
static short stagingL[SND3DS_STAGING_FRAMES_MAX];
static short stagingR[SND3DS_STAGING_FRAMES_MAX];


//---------------------------------------------------------
// Set the sampling rate + block size.
//
// Must be called before snd3dsInitialize().
//---------------------------------------------------------
void snd3dsSetSampleRate(int sampleRate, int samplesPerLoop)
{
    if (samplesPerLoop > SND3DS_STAGING_FRAMES_MAX)
        samplesPerLoop = SND3DS_STAGING_FRAMES_MAX;

    snd3dsSampleRate = sampleRate;
    snd3dsSamplesPerLoop = samplesPerLoop;
}


//---------------------------------------------------------
// Mix one block and hand it off to NDSP.
//
// Polls the fillBlock wavebuf; when it has been consumed by
// the DSP (NDSP_WBUF_DONE) or never queued (NDSP_WBUF_FREE),
// we produce snd3dsSamplesPerLoop frames into it and re-queue.
// If no buffer is ready we sleep briefly and return — the
// mixing thread loops immediately.
//---------------------------------------------------------
void snd3dsMixSamples()
{
    if (snd3DS.audioType != 2)
        return;

    ndspWaveBuf *wb = &snd3DS.waveBufs[snd3DS.fillBlock];

    if (wb->status != NDSP_WBUF_DONE && wb->status != NDSP_WBUF_FREE)
    {
        // DSP still chewing on this buffer — back off briefly so we
        // don't spin. NDSP frame tick is ~5ms, a 500us sleep is plenty.
        svcSleepThread(500000);
        return;
    }

    bool generateSound = snd3DS.isPlaying && !snd3DS.generateSilence;
    short *dst = (short *)wb->data_vaddr;
    int frames = snd3dsSamplesPerLoop;

    if (generateSound)
    {
        impl3dsGenerateSoundSamples();
        impl3dsOutputSoundSamples(stagingL, stagingR);

        for (int i = 0; i < frames; i++)
        {
            dst[i * 2]     = stagingL[i];
            dst[i * 2 + 1] = stagingR[i];
        }
    }
    else
    {
        memset(dst, 0, (size_t)frames * 2 * sizeof(short));
    }

    DSP_FlushDataCache(dst, (u32)(frames * 2 * sizeof(short)));
    ndspChnWaveBufAdd(0, wb);

    snd3DS.fillBlock = (snd3DS.fillBlock + 1) % SND3DS_WAVEBUF_COUNT;
}


//---------------------------------------------------------
// Mixing thread entry point.
//
// Runs on both real hardware and emulators. NDSP is emulated
// by Citra/Azahar (given dspfirm.cdc), so audio now works in
// either case — no more Citra-gated thread creation.
//---------------------------------------------------------
static void snd3dsMixingThread(void *p)
{
    while (!snd3DS.terminateMixingThread)
    {
        snd3dsMixSamples();
    }
    snd3DS.terminateMixingThread = -1;
    svcExitThread();
}


//---------------------------------------------------------
// Start playing the samples.
//
// Primes both NDSP wavebufs so the channel has something to
// play immediately. The mixing thread will keep them fed.
//---------------------------------------------------------
void snd3dsStartPlaying()
{
    if (snd3DS.isPlaying)
        return;

    if (snd3DS.audioType != 2)
        return;

    // Mark both wavebufs as free so the first snd3dsMixSamples iterations
    // fill them in order.
    for (int i = 0; i < SND3DS_WAVEBUF_COUNT; i++)
    {
        snd3DS.waveBufs[i].status = NDSP_WBUF_FREE;
    }
    snd3DS.fillBlock = 0;

    snd3DS.isPlaying = true;
    snd3DS.generateSilence = false;
}


//---------------------------------------------------------
// Stop playing the samples.
//---------------------------------------------------------
void snd3dsStopPlaying()
{
    if (!snd3DS.isPlaying)
        return;

    if (snd3DS.audioType == 2)
    {
        ndspChnWaveBufClear(0);
    }

    snd3DS.isPlaying = false;
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
    S9xSetPlaybackRate(snd3dsSampleRate);

    snd3DS.isPlaying = false;
    snd3DS.audioType = 0;

    Result ret = ndspInit();
    log3dsWrite("ndspInit ret = 0x%lx", (unsigned long)ret);
    if (R_FAILED(ret))
    {
        log3dsWrite("NDSP init failed — continuing without audio");
        return false;
    }

    snd3DS.audioType = 2;

    // Single linearAlloc covers all wavebufs back-to-back.
    int framesPerBuffer = snd3dsSamplesPerLoop;
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
    snd3DS.pcmFramesPerBuffer = framesPerBuffer;
    snd3DS.pcmBufferCount = SND3DS_WAVEBUF_COUNT;

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspSetOutputCount(1);
    ndspSetMasterVol(1.0f);

    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)snd3dsSampleRate);
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
        snd3dsSampleRate, SND3DS_WAVEBUF_COUNT, framesPerBuffer);

    // Borrow CPU time on real hardware for the mixing thread, same as before.
    snd3DS.terminateMixingThread = false;

    if (GPU3DS.isReal3DS)
    {
        APT_GetAppCpuTimeLimit(&old_time_limit);
        u32 newLimitInPercent = 30;
        APT_SetAppCpuTimeLimit(newLimitInPercent);
        log3dsWrite("snd3dsInit - SetAppCpuTimeLimit: %u (old: %u)", newLimitInPercent, old_time_limit);
    }

    IAPU.DSPReplayIndex = 0;
    IAPU.DSPWriteIndex = 0;

    snd3DS.mixingThread = threadCreate(snd3dsMixingThread, NULL, 0x4000, 0x18, 1, false);
    if (snd3DS.mixingThread == NULL)
    {
        log3dsWrite("Unable to start mixing thread");
        snd3dsFinalize();
        return false;
    }

    log3dsWrite("Mixing thread started: %lx", (unsigned long)threadGetHandle(snd3DS.mixingThread));
    return true;
}


//---------------------------------------------------------
// Finalize the audio pipeline.
//---------------------------------------------------------
void snd3dsFinalize()
{
    snd3DS.terminateMixingThread = true;

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

    if (old_time_limit != UINT32_MAX)
    {
        log3dsWrite("restore AppCpuTimeLimit: %u", old_time_limit);
        APT_SetAppCpuTimeLimit(old_time_limit);
        old_time_limit = UINT32_MAX;
    }
}
