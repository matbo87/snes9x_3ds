#ifndef _MSU1_H_
#define _MSU1_H_

#include "snes9x.h"

#define MSU1_REVISION          0x02
#define MSU1_PCM_HEADER_SIZE   8
#define MSU1_BYTES_PER_SAMPLE  4
#define MSU1_SAMPLE_RATE       44100
#define MSU1_NO_RESUME         0xFFFFFFFF

// MSU_STATUS flags; the low three bits carry MSU1_REVISION on read.
enum
{
	MSU1_TRACK_MISSING = 0x08,
	MSU1_AUDIO_PLAYING = 0x10,
	MSU1_AUDIO_REPEAT  = 0x20,
	MSU1_AUDIO_BUSY    = 0x40,
	MSU1_DATA_BUSY     = 0x80
};

enum
{
	MSU1_EVENT_OPEN_FAILED = 0x01,	// track missing or malformed
	MSU1_EVENT_ENDED       = 0x02
};

struct SMSU1
{
	uint8	Status;
	uint8	Volume;
	uint16	TrackLatch;
	uint16	CurrentTrack;
	bool8	TrackLoaded;
	uint32	ResumeTrack;
	uint32	ResumeOffset;
	uint32	DataOffsetLatch;
	uint32	DataOffset;
	uint32	DataSize;
	FILE	*DataFile;
	int32	TrackCount;		// .pcm files found at load; -1 if the folder could not be listed
	char	BasePath [_MAX_PATH + 1];
};

// Audio offsets count from after the 8-byte .pcm header;
// data offsets from the start of the .msu.
struct SMSU1Snapshot
{
	uint8	Status;
	uint8	Volume;
	uint8	TrackLoaded;
	uint16	TrackLatch;
	uint16	CurrentTrack;
	uint32	ResumeTrack;
	uint32	ResumeOffset;
	uint32	DataOffsetLatch;
	uint32	DataOffset;
	uint32	AudioOffset;
};

// Audio side, implemented by the platform and called on the emulation thread; the platform
// does its file I/O on its own thread. The .msu data port is read here directly.
struct SMSU1Hooks
{
	void	(*AudioOpen) (const char *path, uint32 startOffset);
	void	(*AudioClose) (void);
	void	(*AudioPlay) (void);		// play written: replays the current track if it ended
	uint32	(*AudioOffset) (void);		// next byte the mixer reads, up to 93 ms ahead of what is heard; in turbo the emulated position
	uint8	(*PollEvents) (void);
};

extern struct SMSU1	MSU1;

void S9xMSU1SetHooks (const SMSU1Hooks *hooks);

bool8 S9xMSU1Init (const char *romFilename);
void S9xMSU1Shutdown (void);
void S9xMSU1Reset (void);

uint8 S9xMSU1ReadPort (uint8 port);
void S9xMSU1WritePort (uint8 port, uint8 byte);

void S9xMSU1DataRead (uint8 *dst, uint32 count);

void S9xMSU1Capture (SMSU1Snapshot *snapshot);
void S9xMSU1Restore (const SMSU1Snapshot *snapshot);

#endif
