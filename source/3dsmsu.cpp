#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msu1.h"
#include "memmap.h"

#include "3ds.h"
#include "3dsmsu.h"
#include "3dssound.h"
#include "3dssettings.h"
#include "3dsutils.h"
#include "3dslog.h"

#define MSU3DS_CHANNEL          1
#define MSU3DS_RING_SIZE        (512 * 1024)    // 3 s of audio, to ride out SD stalls during quick saves
#define MSU3DS_CHUNK_SIZE       (64 * 1024)
#define MSU3DS_WAVEBUF_COUNT    4
#define MSU3DS_WAVEBUF_SAMPLES  1024
#define MSU3DS_WAVEBUF_BYTES    (MSU3DS_WAVEBUF_SAMPLES * MSU1_BYTES_PER_SAMPLE)

#define MSU3DS_PATH_SIZE        (_MAX_PATH + 1)

//---------------------------------------------------------
// Shared by the emulation, streaming and mixing threads:
// access under ringLock, never hold it during file I/O.
//---------------------------------------------------------
struct SMSU3DSRing
{
    u8      *buffer;
    u32     readIndex;
    u32     availableBytes;
    u32     playbackOffset;
    u32     streamOffset;
    u32     audioSizeBytes;
    u32     loopOffset;
    bool    opened;
    u32     generation;     // bumped by every track change or rewind; a read started under an older value is discarded
    bool    ended;
    bool    turbo;          // mixer stops consuming, position advanced per emulated frame; written by the emulation thread

    // emulation thread -> streamer
    bool    requestPending;
    bool    requestOpen;
    char    requestPath[MSU3DS_PATH_SIZE];

    // streamer / mixer -> emulation thread
    volatile u8 events;     // read without the lock by the status poll
};

static SMSU3DSRing  ring;
static LightLock    ringLock;
static LightEvent   streamEvent;

static volatile bool initialized = false;   // also the mixer's gate
static volatile bool streamTerminate = false;

static Thread       streamThread = NULL;
static FILE         *streamAudioFile = NULL;  // streaming thread only

static u8           *waveBufferMemory = NULL;
static ndspWaveBuf  waveBuffers[MSU3DS_WAVEBUF_COUNT];
static int          nextWaveBuffer = 0;
static u32          mixerGeneration = 0;
static bool         queuedSinceGeneration = false;
static bool         channelActive = false;      // mixer only: channel 1 is being fed
static int          underruns = 0;
static float        lastMix = -1.0f;
static float        volumeGain = 1.0f;


//---------------------------------------------------------
// Caller holds ringLock.
//---------------------------------------------------------
// Empties the ring at offset; the track stays open.
static void msu3dsRingRewind(u32 offset)
{
    ring.readIndex = 0;
    ring.availableBytes = 0;
    ring.playbackOffset = offset;
    ring.streamOffset = offset;
    ring.generation++;
}

// Empties the ring for a new track, opened later by the streamer.
static void msu3dsRingReset(u32 offset)
{
    msu3dsRingRewind(offset);
    ring.audioSizeBytes = 0;
    ring.loopOffset = 0;
    ring.opened = false;
    ring.ended = false;
}


//---------------------------------------------------------
// Core hooks, emulation thread.
//---------------------------------------------------------
static void msu3dsPostAudioRequest(const char *path, u32 startOffset)
{
    LightLock_Lock(&ringLock);
    // Whole stereo frames only; an odd offset from a savestate would misalign the whole track.
    msu3dsRingReset(startOffset & ~(MSU1_BYTES_PER_SAMPLE - 1));
    ring.requestPending = true;
    ring.requestOpen = path != NULL;
    if (path)
        snprintf(ring.requestPath, sizeof(ring.requestPath), "%s", path);
    ring.events = 0;
    LightLock_Unlock(&ringLock);

    LightEvent_Signal(&streamEvent);
}

static void msu3dsHookAudioOpen(const char *path, uint32 startOffset)
{
    msu3dsPostAudioRequest(path, startOffset);
    log3dsWrite("MSU-1: load %s at %lu", path, (unsigned long)startOffset);
}

static void msu3dsHookAudioClose()
{
    msu3dsPostAudioRequest(NULL, 0);
}

static void msu3dsHookAudioPlay()
{
    LightLock_Lock(&ringLock);
    ring.ended = false;
    // An ENDED the core has not polled yet would clear the PLAYING just set; drop it.
    ring.events &= ~MSU1_EVENT_ENDED;
    LightLock_Unlock(&ringLock);

    LightEvent_Signal(&streamEvent);
}

static uint32 msu3dsHookAudioOffset()
{
    LightLock_Lock(&ringLock);
    u32 offset = ring.playbackOffset;
    LightLock_Unlock(&ringLock);
    return offset;
}

// Games may read the status register thousands of times a frame;
// skip the lock until an event is posted.
static u8 msu3dsHookPollEvents()
{
    if (!ring.events)
        return 0;

    LightLock_Lock(&ringLock);
    u8 events = ring.events;
    ring.events = 0;
    LightLock_Unlock(&ringLock);
    return events;
}

static const SMSU1Hooks msu3dsHooks = {
    msu3dsHookAudioOpen, msu3dsHookAudioClose, msu3dsHookAudioPlay, msu3dsHookAudioOffset, msu3dsHookPollEvents
};


//---------------------------------------------------------
// Streaming thread.
//---------------------------------------------------------
static void msu3dsStreamAudioClose()
{
    if (streamAudioFile)
    {
        fclose(streamAudioFile);
        streamAudioFile = NULL;
    }
}

// The caller closes the file when this fails.
static bool msu3dsStreamAudioOpen(const char *path, u32 *size, u32 *loopOffset)
{
    u8 header[MSU1_PCM_HEADER_SIZE];

    msu3dsStreamAudioClose();

    streamAudioFile = fopen(path, "rb");
    if (!streamAudioFile)
        return false;

    setvbuf(streamAudioFile, NULL, _IONBF, 0);

    if (fread(header, 1, sizeof(header), streamAudioFile) != sizeof(header) ||
        memcmp(header, "MSU1", 4) != 0 ||
        fseek(streamAudioFile, 0, SEEK_END) != 0)
        return false;

    long end = ftell(streamAudioFile);
    if (end < (long)MSU1_PCM_HEADER_SIZE)
        return false;

    u32 loopSamples = header[4] | (header[5] << 8) | (header[6] << 16) | ((u32)header[7] << 24);
    // Drop a partial last frame; otherwise every frame after the loop point is misaligned
    // (channels swap or turn to noise).
    *size = ((u32)end - MSU1_PCM_HEADER_SIZE) & ~(MSU1_BYTES_PER_SAMPLE - 1);
    *loopOffset = loopSamples * MSU1_BYTES_PER_SAMPLE;
    if (*loopOffset >= *size)
        *loopOffset = 0;
    return true;
}

static void msu3dsStreamServeAudioRequest()
{
    char path[MSU3DS_PATH_SIZE];

    LightLock_Lock(&ringLock);
    bool pending = ring.requestPending;
    bool open = ring.requestOpen;
    u32  requestGeneration = ring.generation;
    ring.requestPending = false;
    if (pending && open)
        snprintf(path, sizeof(path), "%s", ring.requestPath);
    LightLock_Unlock(&ringLock);

    if (!pending)
        return;

    if (!open)
    {
        msu3dsStreamAudioClose();
        return;
    }

    u32 size = 0, loopOffset = 0;
    bool ok = msu3dsStreamAudioOpen(path, &size, &loopOffset);
    if (!ok)
        log3dsWrite("MSU-1: track missing: %s", path);

    LightLock_Lock(&ringLock);
    if (ring.generation == requestGeneration)
    {
        if (ok)
        {
            ring.audioSizeBytes = size;
            ring.loopOffset = loopOffset;
            ring.opened = true;
            if (ring.streamOffset >= size)
                ring.playbackOffset = ring.streamOffset = 0;
        }
        else
            ring.events |= MSU1_EVENT_OPEN_FAILED;
    }
    else
        ok = false;
    LightLock_Unlock(&ringLock);

    if (!ok)
        msu3dsStreamAudioClose();
}

// Reads the next chunk of the track into the ring without holding the lock;
// the chunk is discarded if the ring was reset meanwhile. False when there is nothing more to read now.
static bool msu3dsStreamFillAudioChunk()
{
    if (!streamAudioFile)
        return false;

    LightLock_Lock(&ringLock);
    u32  generation = ring.generation;
    u32  size = ring.audioSizeBytes;
    u32  target = ring.streamOffset;
    u32  loopOffset = ring.loopOffset;
    u32  space = MSU3DS_RING_SIZE - ring.availableBytes;
    u32  writeIndex = (ring.readIndex + ring.availableBytes) % MSU3DS_RING_SIZE;
    bool newer = ring.requestPending;
    LightLock_Unlock(&ringLock);

    if (newer || size == 0 || space < MSU3DS_CHUNK_SIZE)
        return false;

    // A rewind can leave the stream at the track end; wrap like the mixer.
    if (target >= size)
        target = loopOffset;

    u32 requestedBytes = MSU3DS_CHUNK_SIZE;
    if (requestedBytes > MSU3DS_RING_SIZE - writeIndex)
        requestedBytes = MSU3DS_RING_SIZE - writeIndex;
    if (requestedBytes > size - target)
        requestedBytes = size - target;

    size_t got = 0;
    if (fseek(streamAudioFile, (long)(MSU1_PCM_HEADER_SIZE + target), SEEK_SET) == 0)
        got = fread(ring.buffer + writeIndex, 1, requestedBytes, streamAudioFile);

    LightLock_Lock(&ringLock);
    if (ring.generation == generation)
    {
        if (got == 0)
        {
            // The card stopped delivering: end the track as a short file would.
            ring.ended = true;
            ring.events |= MSU1_EVENT_ENDED;
        }
        ring.availableBytes += got;
        ring.streamOffset = target + got;
        if (ring.streamOffset >= ring.audioSizeBytes)
            ring.streamOffset = ring.loopOffset;
    }
    LightLock_Unlock(&ringLock);

    return got != 0;
}

static void msu3dsStreamMain(void *p)
{
    while (true)
    {
        LightEvent_Wait(&streamEvent);
        if (streamTerminate)
            break;
        msu3dsStreamServeAudioRequest();
        while (msu3dsStreamFillAudioChunk())
            ;
    }
    msu3dsStreamAudioClose();
}


//---------------------------------------------------------
// Mixing thread, inside snesAccessLock.
//---------------------------------------------------------
// A queued buffer leaves the DONE state, which keeps the fill loops
// from picking it again until NDSP has played it.
static void msu3dsQueueWaveBuffer(ndspWaveBuf *waveBuffer)
{
    u32 size = waveBuffer->nsamples * MSU1_BYTES_PER_SAMPLE;
    if (device3DS.isReal3DS)
        svcFlushProcessDataCache(CUR_PROCESS_HANDLE, (u32)waveBuffer->data_vaddr, size);
    else
        DSP_FlushDataCache(waveBuffer->data_vaddr, size);
    ndspChnWaveBufAdd(MSU3DS_CHANNEL, waveBuffer);
    nextWaveBuffer = (nextWaveBuffer + 1) % MSU3DS_WAVEBUF_COUNT;
}

static void msu3dsClearQueue()
{
    ndspChnWaveBufClear(MSU3DS_CHANNEL);
    for (int i = 0; i < MSU3DS_WAVEBUF_COUNT; i++)
        waveBuffers[i].status = NDSP_WBUF_DONE;
    nextWaveBuffer = 0;
    queuedSinceGeneration = false;
}

void msu3dsMixerTick()
{
    if (!initialized)
        return;

    // A game without MSU-1 after one with it:
    // stop channel 1 once and leave it idle, as before the first MSU-1 game.
    if (!Settings.MSU1)
    {
        if (channelActive)
        {
            msu3dsClearQueue();
            channelActive = false;
        }
        return;
    }
    channelActive = true;

    bool wakeStreamer = false;

    LightLock_Lock(&ringLock);

    if (ring.generation != mixerGeneration)
    {
        msu3dsClearQueue();
        mixerGeneration = ring.generation;
    }

    u8   status = MSU1.Status;
    bool playing = (status & MSU1_AUDIO_PLAYING) && !ring.ended && ring.opened;
    bool turbo = ring.turbo;
    bool consume = snd3DS.isPlaying && !snd3DS.generateSilence && !turbo;
    u32  spaceBefore = MSU3DS_RING_SIZE - ring.availableBytes;

    while (playing && consume)
    {
        ndspWaveBuf *waveBuffer = &waveBuffers[nextWaveBuffer];
        if (waveBuffer->status != NDSP_WBUF_DONE && waveBuffer->status != NDSP_WBUF_FREE)
            break;

        u8  *dst = (u8 *)waveBuffer->data_vaddr;
        u32 got = 0;

        while (got < MSU3DS_WAVEBUF_BYTES)
        {
            if (ring.playbackOffset >= ring.audioSizeBytes)
            {
                if (status & MSU1_AUDIO_REPEAT)
                    ring.playbackOffset = ring.loopOffset;
                else
                {
                    // Track ended without repeat: rewind to the start.
                    // The queued tail still plays; the streamer's read in flight is discarded.
                    ring.events |= MSU1_EVENT_ENDED;
                    msu3dsRingRewind(0);
                    mixerGeneration = ring.generation;
                    ring.ended = true;
                    playing = false;
                    wakeStreamer = true;
                    break;
                }
            }
            // Whole stereo frames only, so a short read cannot misalign the queue.
            u32 availableFrameBytes = ring.availableBytes & ~(MSU1_BYTES_PER_SAMPLE - 1);
            if (availableFrameBytes == 0)
                break;

            u32 n = MSU3DS_WAVEBUF_BYTES - got;
            if (n > availableFrameBytes) n = availableFrameBytes;
            if (n > ring.audioSizeBytes - ring.playbackOffset) n = ring.audioSizeBytes - ring.playbackOffset;
            if (n > MSU3DS_RING_SIZE - ring.readIndex) n = MSU3DS_RING_SIZE - ring.readIndex;

            memcpy(dst + got, ring.buffer + ring.readIndex, n);
            ring.readIndex = (ring.readIndex + n) % MSU3DS_RING_SIZE;
            ring.availableBytes -= n;
            ring.playbackOffset += n;
            got += n;
        }

        if (got == 0)
        {
            if (playing && queuedSinceGeneration)
                underruns++;
            break;
        }

        waveBuffer->nsamples = got / MSU1_BYTES_PER_SAMPLE;
        msu3dsQueueWaveBuffer(waveBuffer);
        queuedSinceGeneration = true;
    }

    // Free buffers get silence while nothing is consumed
    // (idle, paused, menu, underrun), so the channel never runs dry.
    while (true)
    {
        ndspWaveBuf *waveBuffer = &waveBuffers[nextWaveBuffer];
        if (waveBuffer->status != NDSP_WBUF_DONE && waveBuffer->status != NDSP_WBUF_FREE)
            break;
        memset((void *)waveBuffer->data_vaddr, 0, MSU3DS_WAVEBUF_BYTES);
        waveBuffer->nsamples = MSU3DS_WAVEBUF_SAMPLES;
        msu3dsQueueWaveBuffer(waveBuffer);
    }

    u32 spaceAfter = MSU3DS_RING_SIZE - ring.availableBytes;

    LightLock_Unlock(&ringLock);

    if (wakeStreamer || (spaceBefore < MSU3DS_CHUNK_SIZE && spaceAfter >= MSU3DS_CHUNK_SIZE))
        LightEvent_Signal(&streamEvent);

    float mix = turbo ? 0.0f : volumeGain * (float)MSU1.Volume / 255.0f;
    if (mix != lastMix)
    {
        float mixLevels[12] = { mix, mix, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        ndspChnSetMix(MSU3DS_CHANNEL, mixLevels);
        lastMix = mix;
    }
}


//---------------------------------------------------------
// Emulation thread, after each emulated frame.
//---------------------------------------------------------
// In turbo the mixer stops consuming and the track advances by emulated time instead,
// as in 1.63, so the audio is in sync when turbo ends. Turbo off refills the ring from that position.
void msu3dsEmulatedFrame()
{
    if (!initialized || !Settings.MSU1)
        return;

    bool turbo = session3DS.turboMode;
    if (!turbo && !ring.turbo)  // ring.turbo is written only on this thread
        return;

    bool wakeStreamer = false;

    LightLock_Lock(&ringLock);
    if (!turbo)
    {
        // An open still in flight is left alone: a rewind would discard it.
        if (ring.opened && !ring.ended)
        {
            msu3dsRingRewind(ring.playbackOffset);
            wakeStreamer = true;
        }
    }
    else if ((MSU1.Status & MSU1_AUDIO_PLAYING) && ring.opened && !ring.ended)
    {
        ring.playbackOffset += MSU1_SAMPLE_RATE * MSU1_BYTES_PER_SAMPLE / Memory.ROMFramesPerSecond;
        if (ring.playbackOffset >= ring.audioSizeBytes)
        {
            if (MSU1.Status & MSU1_AUDIO_REPEAT)
            {
                ring.playbackOffset = ring.loopOffset + (ring.playbackOffset - ring.audioSizeBytes);
                if (ring.playbackOffset >= ring.audioSizeBytes)
                    ring.playbackOffset = ring.loopOffset;
            }
            else
            {
                // Same end as the mixer's, at the emulated frame the track runs out.
                ring.events |= MSU1_EVENT_ENDED;
                msu3dsRingRewind(0);
                ring.ended = true;
                wakeStreamer = true;
            }
        }
    }
    ring.turbo = turbo;
    LightLock_Unlock(&ringLock);

    if (wakeStreamer)
        LightEvent_Signal(&streamEvent);
}


//---------------------------------------------------------
// Lifecycle, main thread.
//---------------------------------------------------------

void msu3dsApplyOutputVolume()
{
    int volumeStep = settings3DS.UseGlobalVolume ? settings3DS.GlobalMsu1Volume : settings3DS.Msu1Volume;
    volumeGain = 0.25f * (float)(volumeStep + 1);
}

void msu3dsLogStats()
{
    log3dsWrite("MSU-1 underruns: %d", underruns);
}

static void msu3dsFreeBuffers()
{
    free(ring.buffer);
    ring.buffer = NULL;
    if (waveBufferMemory)
        linearFree(waveBufferMemory);
    waveBufferMemory = NULL;
}

void msu3dsStart()
{
    if (initialized || snd3DS.audioType != 2)
        return;

    LightLock_Init(&ringLock);
    LightEvent_Init(&streamEvent, RESET_ONESHOT);

    ring.buffer = (u8 *)malloc(MSU3DS_RING_SIZE);
    waveBufferMemory = (u8 *)linearAlloc(MSU3DS_WAVEBUF_COUNT * MSU3DS_WAVEBUF_BYTES);
    if (!ring.buffer || !waveBufferMemory)
    {
        log3dsWrite("MSU-1: out of memory, audio disabled");
        msu3dsFreeBuffers();
        return;
    }
    msu3dsRingReset(0);

    ndspChnReset(MSU3DS_CHANNEL);
    ndspChnSetInterp(MSU3DS_CHANNEL, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(MSU3DS_CHANNEL, (float)MSU1_SAMPLE_RATE);
    ndspChnSetFormat(MSU3DS_CHANNEL, NDSP_FORMAT_STEREO_PCM16);

    memset(waveBufferMemory, 0, MSU3DS_WAVEBUF_COUNT * MSU3DS_WAVEBUF_BYTES);
    memset(waveBuffers, 0, sizeof(waveBuffers));
    for (int i = 0; i < MSU3DS_WAVEBUF_COUNT; i++)
    {
        waveBuffers[i].data_vaddr = waveBufferMemory + i * MSU3DS_WAVEBUF_BYTES;
        waveBuffers[i].status = NDSP_WBUF_DONE;
    }
    nextWaveBuffer = 0;
    mixerGeneration = ring.generation;
    queuedSinceGeneration = false;
    lastMix = -1.0f;

    streamTerminate = false;
    streamThread = threadCreate(msu3dsStreamMain, NULL, 0x4000, THREAD_PRIORITY_SD_WORKER, device3DS.secondaryCore, false);
    if (!streamThread)
    {
        log3dsWrite("MSU-1: unable to start the streaming thread, audio disabled");
        msu3dsFreeBuffers();
        return;
    }

    S9xMSU1SetHooks(&msu3dsHooks);
    initialized = true;

    log3dsWrite("MSU-1 ready: %d KB ring, %d wavebufs x %d samples, streamer on core %d",
        MSU3DS_RING_SIZE / 1024, MSU3DS_WAVEBUF_COUNT, MSU3DS_WAVEBUF_SAMPLES, device3DS.secondaryCore);
}

void msu3dsFinalize()
{
    S9xMSU1Shutdown();
    S9xMSU1SetHooks(NULL);

    if (!initialized)
        return;

    // Close the gate, then take the mixer's lock once:
    // after that no mixer tick can still be running.
    initialized = false;
    LightLock_Lock(&snd3DS.snesAccessLock);
    LightLock_Unlock(&snd3DS.snesAccessLock);

    streamTerminate = true;
    LightEvent_Signal(&streamEvent);
    bool joined = threadJoin(streamThread, 2000 * 1000000LL) == 0;
    if (joined)
        threadFree(streamThread);
    else
        log3dsWrite("MSU-1: streaming thread did not exit, leaving its memory");
    streamThread = NULL;

    ndspChnWaveBufClear(MSU3DS_CHANNEL);

    if (joined)
        msu3dsFreeBuffers();
}
