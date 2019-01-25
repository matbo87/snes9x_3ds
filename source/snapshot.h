#include "copyright.h"


#ifndef _SNAPSHOT_H_
#define _SNAPSHOT_H_

#include <stdio.h>
#include "snes9x.h"

class BufferedFileWriter;

#define SNAPSHOT_MAGIC "#!snes9x"
#define SNAPSHOT_VERSION 1

#define SUCCESS 1
#define WRONG_FORMAT (-1)
#define WRONG_VERSION (-2)
#define FILE_NOT_FOUND (-3)
#define WRONG_MOVIE_SNAPSHOT (-4)
#define NOT_A_MOVIE_SNAPSHOT (-5)

START_EXTERN_C
bool8 S9xOpenSnapshotFile (const char *base, bool8 read_only, STREAM *file);
void S9xCloseSnapshotFile (STREAM file);

bool8 S9xFreezeGame (const char *filename);
bool8 S9xUnfreezeGame (const char *filename);
bool8 Snapshot (const char *filename);
bool8 S9xLoadSnapshot (const char *filename);
bool8 S9xSPCDump (const char *filename);
void S9xFreezeToStream (BufferedFileWriter& stream);
int S9xUnfreezeFromStream (STREAM);
END_EXTERN_C

#endif

