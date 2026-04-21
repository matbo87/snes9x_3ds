#ifndef _3DSSOUND_H_
#define _3DSSOUND_H_

#include "3ds.h"

// 8 wavebufs x samplesPerLoop (256 @ 32 kHz) = 2048 frames = ~64ms total lookahead.
// Lower values (tested at 2) produce audible stutter when the emu
// thread stalls during menu draws or SD I/O.
#define SND3DS_WAVEBUF_COUNT    8

typedef struct
{
    bool        isPlaying = false;
    bool        generateSilence = false;

    int         audioType = 0;              // 0 - no audio, 2 - NDSP

    short       *pcmBuffer;                 // interleaved stereo s16 frames, linearAlloc'd
    int         pcmFramesPerBuffer;         // frames per wavebuf (samplesPerLoop)
    int         pcmBufferCount;             // SND3DS_WAVEBUF_COUNT
    int         fillBlock;                  // which wavebuf to refill next

    ndspWaveBuf waveBufs[SND3DS_WAVEBUF_COUNT];

    Thread      mixingThread = NULL;
    bool        terminateMixingThread;
} SSND3DS;


extern SSND3DS snd3DS;

//---------------------------------------------------------
// Set the sampling rate.
//
// This function should be called by the
// impl3dsInitialize function. It CANNOT be called
// after the snd3dsInitialize function is called.
//---------------------------------------------------------
void snd3dsSetSampleRate(int sampleRate, int samplesPerLoop);


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
// Force the mixing thread into "silence mode" and wait long
// enough to guarantee any in-flight SNES-state access has
// completed. Callers can then tear down / rebuild SNES state
// (load ROM, reset console, swap memory maps) without the
// mixer reading torn-down globals.
//
// Pair with snd3dsResumeMixing() once the new state is ready.
//---------------------------------------------------------
void snd3dsDrainMixing();
void snd3dsResumeMixing();

#endif
