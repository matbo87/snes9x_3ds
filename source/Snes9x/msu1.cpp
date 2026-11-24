#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>

#include "msu1.h"

// LRU block cache for the .msu, read on the emulation thread. Games jump around a working
// set many times a second and every SD read stalls the frame, so it has to stay in RAM.
// Allocated on the first data read and kept.
#define MSU1_DATA_BLOCK_SIZE	(64 * 1024)
#define MSU1_DATA_BLOCK_COUNT	64
#define MSU1_NO_BLOCK			0xFFFFFFFF

struct SMSU1 MSU1;

struct SMSU1DataBlock
{
	uint32	index;
	uint32	lastUse;
};

static uint8 *dataCache = NULL;
static SMSU1DataBlock dataBlocks [MSU1_DATA_BLOCK_COUNT];
static uint32 dataUseClock = 0;
static int dataLastSlot = 0;

static void S9xMSU1DataInvalidate (void)
{
	for (int i = 0; i < MSU1_DATA_BLOCK_COUNT; i++)
	{
		dataBlocks [i].index = MSU1_NO_BLOCK;
		dataBlocks [i].lastUse = 0;
	}
	dataLastSlot = 0;
}

// Slot holding the block, loading it into the least recently used slot on a miss.
static int S9xMSU1DataSlot (uint32 index)
{
	if (dataBlocks [dataLastSlot].index == index)
		return (dataLastSlot);

	int slot = 0;
	for (int i = 0; i < MSU1_DATA_BLOCK_COUNT; i++)
	{
		if (dataBlocks [i].index == index)
			return (dataLastSlot = i);
		if (dataBlocks [i].lastUse < dataBlocks [slot].lastUse)
			slot = i;
	}

	uint8 *dst = dataCache + slot * MSU1_DATA_BLOCK_SIZE;
	size_t got = 0;
	if (fseek (MSU1.DataFile, (long) index * MSU1_DATA_BLOCK_SIZE, SEEK_SET) == 0)
		got = fread (dst, 1, MSU1_DATA_BLOCK_SIZE, MSU1.DataFile);
	clearerr (MSU1.DataFile);

	if (got == 0)
	{
		dataBlocks [slot].index = MSU1_NO_BLOCK;
		dataBlocks [slot].lastUse = 0;
		return (-1);
	}
	if (got < MSU1_DATA_BLOCK_SIZE)
		memset (dst + got, 0, MSU1_DATA_BLOCK_SIZE - got);	// last block of the file
	dataBlocks [slot].index = index;
	return (dataLastSlot = slot);
}

static uint32 S9xMSU1DataCopy (uint8 *dst, uint32 count)
{
	uint32 done = 0;

	if (!dataCache && MSU1.DataOffset < MSU1.DataSize)
	{
		dataCache = (uint8 *) malloc (MSU1_DATA_BLOCK_COUNT * MSU1_DATA_BLOCK_SIZE);
		if (!dataCache)
			MSU1.DataSize = 0;	// out of memory: the data port reads zeros, audio still plays
	}

	while (done < count && MSU1.DataOffset < MSU1.DataSize)
	{
		uint32 offset = MSU1.DataOffset;
		uint32 blockOffset = offset % MSU1_DATA_BLOCK_SIZE;

		int slot = S9xMSU1DataSlot (offset / MSU1_DATA_BLOCK_SIZE);
		if (slot < 0)
			break;
		dataBlocks [slot].lastUse = ++dataUseClock;

		uint32 n = MSU1_DATA_BLOCK_SIZE - blockOffset;
		if (n > count - done)
			n = count - done;
		if (n > MSU1.DataSize - offset)
			n = MSU1.DataSize - offset;
		memcpy (dst + done, dataCache + slot * MSU1_DATA_BLOCK_SIZE + blockOffset, n);
		done += n;
		MSU1.DataOffset += n;
	}
	return (done);
}

static const SMSU1Hooks *platformHooks = NULL;
static uint8 trackPresent [0x10000 / 8];
static const char msu1Id [6] = { 'S', '-', 'M', 'S', 'U', '1' };

void S9xMSU1SetHooks (const SMSU1Hooks *hooks)
{
	platformHooks = hooks;
}

static bool8 S9xMSU1BuildBasePath (const char *romFilename, char *out, size_t outSize)
{
	size_t len = strlen (romFilename);
	size_t cut = len;

	if (len == 0 || len >= outSize)
		return (FALSE);

	for (size_t i = len; i > 0; i--)
	{
		char c = romFilename [i - 1];
		if (c == '/' || c == '\\')
			break;
		if (c == '.')
		{
			cut = i - 1;
			break;
		}
	}

	memcpy (out, romFilename, cut);
	out [cut] = 0;
	return (TRUE);
}

static bool8 S9xMSU1BuildTrackPath (uint16 track, char *out, size_t outSize)
{
	int n = snprintf (out, outSize, "%s-%u.pcm", MSU1.BasePath, (unsigned) track);
	return (n > 0 && (size_t) n < outSize);
}

// Lists the pack's .pcm files at load so a missing track reports at $2005, without
// waiting for an SD lookup.
static void S9xMSU1ScanTracks (void)
{
	char dir [_MAX_PATH + 1];
	const char *slash = strrchr (MSU1.BasePath, '/');

	memset (trackPresent, 0, sizeof (trackPresent));
	MSU1.TrackCount = -1;
	if (!slash)
		return;

	size_t dirLen = slash - MSU1.BasePath + 1;
	memcpy (dir, MSU1.BasePath, dirLen);
	dir [dirLen] = 0;
	const char *base = slash + 1;
	size_t baseLen = strlen (base);

	DIR *d = opendir (dir);
	if (!d)
		return;

	MSU1.TrackCount = 0;
	struct dirent *entry;
	while ((entry = readdir (d)) != NULL)
	{
		const char *name = entry->d_name;
		if (strncasecmp (name, base, baseLen) != 0 || name [baseLen] != '-')
			continue;

		// Same digits as the path built for the track: no sign, no leading zero.
		const char *digits = name + baseLen + 1;
		char *end;
		if (!isdigit ((unsigned char) digits [0]) || (digits [0] == '0' && isdigit ((unsigned char) digits [1])))
			continue;
		unsigned long track = strtoul (digits, &end, 10);
		if (track > 0xFFFF || strcasecmp (end, ".pcm") != 0)
			continue;

		if (!(trackPresent [track >> 3] & (1 << (track & 7))))
			MSU1.TrackCount++;
		trackPresent [track >> 3] |= 1 << (track & 7);
	}
	closedir (d);
}

static bool8 S9xMSU1TrackPresent (uint16 track)
{
	return (MSU1.TrackCount < 0 || (trackPresent [track >> 3] & (1 << (track & 7))));
}

static void S9xMSU1AudioClose (void)
{
	if (platformHooks)
		platformHooks->AudioClose ();
	MSU1.TrackLoaded = FALSE;
}

// Only switches play and repeat; the track position never moves here.
static void S9xMSU1ApplyControl (uint8 byte)
{
	MSU1.Status = (MSU1.Status & ~(MSU1_AUDIO_PLAYING | MSU1_AUDIO_REPEAT)) | ((byte & 0x03) << 4);

	if ((byte & 0x05) == 0x04)
	{
		MSU1.ResumeTrack = MSU1.CurrentTrack;
		MSU1.ResumeOffset = platformHooks ? platformHooks->AudioOffset () : 0;
	}
	else if ((byte & 0x01) && platformHooks)
		platformHooks->AudioPlay ();
}

// Applies what the platform reported (a failed open or a track end) to the status
// register; only the emulation thread writes the status.
static void S9xMSU1PollEvents (void)
{
	if (!platformHooks)
		return;

	uint8 events = platformHooks->PollEvents ();

	if (events & MSU1_EVENT_OPEN_FAILED)
	{
		MSU1.Status |= MSU1_TRACK_MISSING;
		MSU1.TrackLoaded = FALSE;
	}

	if (events & (MSU1_EVENT_OPEN_FAILED | MSU1_EVENT_ENDED))
		MSU1.Status &= ~(MSU1_AUDIO_PLAYING | MSU1_AUDIO_REPEAT);
}

// The spec sets audio busy until the track is ready; like 1.63, busy is never set here,
// since games that spin on it can glitch. The mixer plays silence until the open lands.
static void S9xMSU1AudioOpen (uint16 track, uint32 startOffset)
{
	char path [_MAX_PATH + 1];

	MSU1.Status &= ~(MSU1_AUDIO_PLAYING | MSU1_AUDIO_REPEAT | MSU1_TRACK_MISSING);

	if (!platformHooks || !S9xMSU1TrackPresent (track) || !S9xMSU1BuildTrackPath (track, path, sizeof (path)))
	{
		S9xMSU1AudioClose ();
		MSU1.Status |= MSU1_TRACK_MISSING;
		return;
	}

	MSU1.TrackLoaded = TRUE;
	platformHooks->AudioOpen (path, startOffset);
}

bool8 S9xMSU1Init (const char *romFilename)
{
	char path [_MAX_PATH + 1];

	S9xMSU1Shutdown ();

	if (!S9xMSU1BuildBasePath (romFilename, MSU1.BasePath, sizeof (MSU1.BasePath)))
		return (FALSE);

	int n = snprintf (path, sizeof (path), "%s.msu", MSU1.BasePath);
	if (n <= 0 || (size_t) n >= sizeof (path))
		return (FALSE);

	MSU1.DataFile = fopen (path, "rb");
	if (!MSU1.DataFile)
		return (FALSE);

	setvbuf (MSU1.DataFile, NULL, _IONBF, 0);

	long size = -1;
	if (fseek (MSU1.DataFile, 0, SEEK_END) == 0)
		size = ftell (MSU1.DataFile);

	MSU1.DataSize = size < 0 ? 0 : (uint32) size;
	S9xMSU1ScanTracks ();
	return (TRUE);
}

void S9xMSU1Shutdown (void)
{
	S9xMSU1AudioClose ();
	if (MSU1.DataFile)
		fclose (MSU1.DataFile);
	S9xMSU1DataInvalidate ();
	memset (&MSU1, 0, sizeof (MSU1));
	MSU1.ResumeTrack = MSU1_NO_RESUME;
}

void S9xMSU1Reset (void)
{
	S9xMSU1AudioClose ();
	MSU1.Status = 0;
	MSU1.Volume = 0;
	MSU1.TrackLatch = 0;
	MSU1.CurrentTrack = 0;
	MSU1.ResumeTrack = MSU1_NO_RESUME;
	MSU1.ResumeOffset = 0;
	MSU1.DataOffsetLatch = 0;
	MSU1.DataOffset = 0;
}

uint8 S9xMSU1ReadPort (uint8 port)
{
	switch (port)
	{
	case 0:
		S9xMSU1PollEvents ();
		return (MSU1.Status | MSU1_REVISION);
	case 1:
	{
		uint8 c = 0;
		S9xMSU1DataCopy (&c, 1);
		return (c);
	}
	default:
		return (msu1Id [port - 2]);
	}
}

void S9xMSU1WritePort (uint8 port, uint8 byte)
{
	switch (port)
	{
	case 0:
		MSU1.DataOffsetLatch = (MSU1.DataOffsetLatch & 0xFFFFFF00) | byte;
		break;
	case 1:
		MSU1.DataOffsetLatch = (MSU1.DataOffsetLatch & 0xFFFF00FF) | ((uint32) byte << 8);
		break;
	case 2:
		MSU1.DataOffsetLatch = (MSU1.DataOffsetLatch & 0xFF00FFFF) | ((uint32) byte << 16);
		break;
	case 3:
		MSU1.DataOffsetLatch = (MSU1.DataOffsetLatch & 0x00FFFFFF) | ((uint32) byte << 24);
		// A seek past the end of the .msu is kept as written (1.63); reads there return zeros.
		MSU1.DataOffset = MSU1.DataOffsetLatch;
		break;
	case 4:
		MSU1.TrackLatch = (MSU1.TrackLatch & 0xFF00) | byte;
		break;
	case 5:
	{
		MSU1.TrackLatch = (MSU1.TrackLatch & 0x00FF) | ((uint32) byte << 8);
		MSU1.CurrentTrack = MSU1.TrackLatch;

		uint32 startOffset = 0;
		if (MSU1.CurrentTrack == MSU1.ResumeTrack)
		{
			startOffset = MSU1.ResumeOffset;
			MSU1.ResumeTrack = MSU1_NO_RESUME;
			MSU1.ResumeOffset = 0;
		}

		S9xMSU1AudioOpen (MSU1.CurrentTrack, startOffset);
		break;
	}
	case 6:
		MSU1.Volume = byte;
		break;
	case 7:
		S9xMSU1PollEvents ();
		if (!(MSU1.Status & MSU1_TRACK_MISSING))
			S9xMSU1ApplyControl (byte);
		break;
	}
}

void S9xMSU1DataRead (uint8 *dst, uint32 count)
{
	uint32 got = S9xMSU1DataCopy (dst, count);
	if (got < count)
		memset (dst + got, 0, count - got);
}

void S9xMSU1Capture (SMSU1Snapshot *snapshot)
{
	S9xMSU1PollEvents ();
	snapshot->Status = MSU1.Status;
	snapshot->Volume = MSU1.Volume;
	snapshot->TrackLoaded = MSU1.TrackLoaded;
	snapshot->TrackLatch = MSU1.TrackLatch;
	snapshot->CurrentTrack = MSU1.CurrentTrack;
	snapshot->ResumeTrack = MSU1.ResumeTrack;
	snapshot->ResumeOffset = MSU1.ResumeOffset;
	snapshot->DataOffsetLatch = MSU1.DataOffsetLatch;
	snapshot->DataOffset = MSU1.DataOffset;
	snapshot->AudioOffset = platformHooks ? platformHooks->AudioOffset () : 0;
}

// The track reopens in the background like a $2005 write; play bits are restored at once.
void S9xMSU1Restore (const SMSU1Snapshot *snapshot)
{
	MSU1.Volume = snapshot->Volume;
	MSU1.TrackLatch = snapshot->TrackLatch;
	MSU1.CurrentTrack = snapshot->CurrentTrack;
	MSU1.ResumeTrack = snapshot->ResumeTrack;
	MSU1.ResumeOffset = snapshot->ResumeOffset;
	MSU1.DataOffsetLatch = snapshot->DataOffsetLatch;

	MSU1.Status = snapshot->Status & (MSU1_AUDIO_PLAYING | MSU1_AUDIO_REPEAT | MSU1_TRACK_MISSING);
	MSU1.DataOffset = snapshot->DataOffset;

	if (snapshot->TrackLoaded)
	{
		S9xMSU1AudioOpen (MSU1.CurrentTrack, snapshot->AudioOffset);
		if (!(MSU1.Status & MSU1_TRACK_MISSING))
			MSU1.Status |= snapshot->Status & (MSU1_AUDIO_PLAYING | MSU1_AUDIO_REPEAT);
	}
}
