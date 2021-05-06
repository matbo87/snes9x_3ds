//===================================================================
// 3DS Emulator
//===================================================================

#ifndef _3DSCONFIG_H_
#define _3DSCONFIG_H_

#include "bufferedfilewriter.h"
#include "port.h"

//----------------------------------------------------------------------
// Load / Save an int32 value specific to game.
//----------------------------------------------------------------------
void    config3dsReadWriteInt32(BufferedFileWriter& stream, bool writeMode,
                                const char *format, int *value,
                                int minValue = 0, int maxValue = 0xffff);


//----------------------------------------------------------------------
// Load / Save a string specific to game.
//----------------------------------------------------------------------
void    config3dsReadWriteString(BufferedFileWriter& stream, bool writeMode,
                                 const char *writeFormat, char *readFormat,
                                 char *value);


void    config3dsReadWriteBitmask(BufferedFileWriter& stream, bool writeMode,
                                  const char* name, uint32* bitmask);

#endif

