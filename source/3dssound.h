#ifndef _3DSSOUND_H_
#define _3DSSOUND_H_

#include <atomic>

#include "3ds.h"

#define SND3DS_SAMPLE_RATE       32000
#define SND3DS_SAMPLES_PER_LOOP  256

// 4 wavebufs x samplesPerLoop (256 @ 32 kHz) = 1024 frames = ~32ms queued audio.
// Lowering the count reduces latency but makes NDSP underruns more likely
// if the mixer is not woken/refilled quickly enough.
// 4 was tested across several games with no audible underruns.
#define SND3DS_WAVEBUF_COUNT     4

typedef struct
{
    bool                 isPlaying = false;

    // cross-thread flags written outside the lock
    std::atomic<bool>    generateSilence{false};
    std::atomic<bool>    terminateMixingThread{false};
    int         audioType = 0;              // 0 - no audio, 2 - NDSP

    short       *pcmBuffer;                 // interleaved stereo s16 frames, linearAlloc'd
    int         fillBlock;                  // which wavebuf to refill next

    ndspWaveBuf waveBufs[SND3DS_WAVEBUF_COUNT];

    Thread      mixingThread = NULL;

    // Protects SNES-state reads in the mixer and wavebuf-queue
    // mutations in Start/Stop. Drain acquires it to wait out any
    // in-flight mixer iteration before callers tear down APU/Memory.
    LightLock   snesAccessLock;

    // Wakes the mixing thread. 
    // Signaled by the NDSP frame callback (~5ms cadence per libctru)
    // and by Finalize at shutdown.
    LightEvent  ndspFrameEvent;
} SSND3DS;


extern SSND3DS snd3DS;

//---------------------------------------------------------
// Initialize the NDSP audio pipeline.
//---------------------------------------------------------
bool snd3dsInitialize();


//---------------------------------------------------------
// Finalize the audio pipeline.
//---------------------------------------------------------
void snd3dsFinalize();


//---------------------------------------------------------
// Mix one block of samples and submit the corresponding
// NDSP wavebuf. Called continuously by the mixing thread
// on both real hardware and Citra/Azahar (given dspfirm.cdc).
//---------------------------------------------------------
void snd3dsMixSamples();


//---------------------------------------------------------
// Start playing the samples.
//---------------------------------------------------------
void snd3dsStartPlaying();


//---------------------------------------------------------
// Stop playing the samples.
//---------------------------------------------------------
void snd3dsStopPlaying();


//---------------------------------------------------------
// Force the mixing thread into "silence mode" and synchronously
// wait until any in-flight SNES-state access has completed.
// Callers can then tear down / rebuild SNES state (load ROM,
// reset console, swap memory maps) without the mixer reading
// torn-down globals.
//
// Pair with snd3dsResumeMixing() once the new state is ready.
//---------------------------------------------------------
void snd3dsDrainMixing();
void snd3dsResumeMixing();


//---------------------------------------------------------
// Old3DS syscore CPU-budget management, called from the APT hook.
// No-ops on New3DS (core2).
//---------------------------------------------------------
void snd3dsApplyCpuLimit();
void snd3dsRestoreCpuLimit();

#endif
