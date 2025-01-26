//===================================================================
// 3DS Emulator
//===================================================================

#ifndef _3DSCONFIG_H_
#define _3DSCONFIG_H_

#include "bufferedfilewriter.h"
#include "port.h"

#define GLOBAL_CONFIG_FILE_TARGET_VERSION   1.1f
#define GAME_CONFIG_FILE_TARGET_VERSION     1
#define CONFIG_FILE_DEFAULT_VERSION         1

float config3dsGetVersionFromFile(bool writeMode, bool isGameConfig, char *versionStringFromFile);

//----------------------------------------------------------------------
// Load / Save an int32 value specific to game.
//----------------------------------------------------------------------
void    config3dsReadWriteInt32(BufferedFileWriter& stream, bool writeMode,
                                const char *format, int *value,
                                int minValue = 0, int maxValue = 0xffff, float versionFromFile = CONFIG_FILE_DEFAULT_VERSION);


//----------------------------------------------------------------------
// Load / Save a string specific to game.
//----------------------------------------------------------------------
void    config3dsReadWriteString(BufferedFileWriter& stream, bool writeMode,
                                 const char *writeFormat, char *readFormat,
                                 char *value, float versionFromFile = CONFIG_FILE_DEFAULT_VERSION);


void    config3dsReadWriteBitmask(BufferedFileWriter& stream, bool writeMode,
                                  const char* name, uint32* bitmask, float versionFromFile = CONFIG_FILE_DEFAULT_VERSION);

#endif

