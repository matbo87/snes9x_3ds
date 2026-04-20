#ifndef _3DSSOUND_H_
#define _3DSSOUND_H_

#include "3ds.h"

#define SND3DS_WAVEBUF_COUNT    2

typedef struct
{
    bool        isPlaying = false;
    bool        generateSilence = false;

    int         audioType = 0;              // 0 - no audio, 2 - NDSP (CSND path was removed in the 1.7f-era audio migration)

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
// on both real hardware and emulators (NDSP is emulated by
// Citra/Azahar; CSND was not, which is why we migrated).
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

#endif
