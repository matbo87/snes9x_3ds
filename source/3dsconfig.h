//===================================================================
// 3DS Emulator
//===================================================================

#ifndef _3DSCONFIG_H_
#define _3DSCONFIG_H_

#include "bufferedfilewriter.h"
#include "3ds.h"

#define GLOBAL_CONFIG_FILE_TARGET_VERSION   1.3f
#define GAME_CONFIG_FILE_TARGET_VERSION     1.1f
#define CONFIG_FILE_DEFAULT_VERSION         1

float config3dsGetVersionFromFile(bool isGameConfig, char *versionStringFromFile);
void  config3dsResetParseWarning();

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
                                 const char *writeFormat, const char *readFormat,
                                 char *value);


void    config3dsReadWriteBitmask(BufferedFileWriter& stream, bool writeMode,
                                  const char* name, u32* bitmask);


template <typename T>
void config3dsReadWriteEnum(BufferedFileWriter& stream, bool writeMode, 
                             const char *format, T *enumValue, 
                             int minValue, int maxValue) 
{
    // handle ghost read
    if (enumValue == NULL) {
        config3dsReadWriteInt32(stream, writeMode, format, NULL, minValue, maxValue);
        return;
    }

    int temp = static_cast<int>(*enumValue);
    config3dsReadWriteInt32(stream, writeMode, format, &temp, minValue, maxValue);

    if (!writeMode) {
        *enumValue = static_cast<T>(temp);
    }
}

#endif
