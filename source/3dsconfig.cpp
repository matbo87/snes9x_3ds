
#include <limits>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3dsconfig.h"

// 256 should be enough for "Key=Value\n" lines
#define CONFIG_BUF_SIZE 256

float config3dsGetVersionFromFile(bool isGameConfig, char *versionStringFromFile) {
    bool latestVersion = isGameConfig ? GAME_CONFIG_FILE_TARGET_VERSION : GLOBAL_CONFIG_FILE_TARGET_VERSION;
    
    char *endptr;
    float detectedVersion = strtof(versionStringFromFile, &endptr);

    // 1. versionStringFromFile == endptr: no digits were found at all
    // 2. *endptr != '\0': the string contained extra garbage (not a clean float)
    // In either case, fall back to the latest version to be safe.
    if (versionStringFromFile == endptr) {
        return latestVersion;
    }
    
    return detectedVersion;
}

// skip reading option from file when detected version from cfg file doesn't match required version
// 
// we do this to ensure all existing settings in settings.cfg are still read correctly.
// this wouldn't be the case when `fscanf` fails to detect current `format` value in settings.cfg
bool config3dsMinVersionRequirementsFulfilled(const char *format, float versionFromFile) {
    if (strstr(format, "ScreenFilter=") != NULL) {
        return versionFromFile >= 1.1f;
    }

    if (strstr(format, "LogFileEnabled=") != NULL) {
        return versionFromFile >= 1.2f;
    }
    
    return true;
}

//----------------------------------------------------------------------
// Load / Save an int32 value specific to game.
//----------------------------------------------------------------------
void config3dsReadWriteInt32(BufferedFileWriter& stream, bool writeMode,
                             const char *format, int *value,
                             int minValue, int maxValue, float versionFromFile)
{
    if (!stream)
        return;

    if (writeMode)
    {
        if (value != NULL)
        {
            char buf[CONFIG_BUF_SIZE];
            int charsWritten = snprintf(buf, sizeof(buf), format, *value);
            
            // snprintf returns required length, so we check if it actually fit
            if (charsWritten > 0 && charsWritten < (int)sizeof(buf)) {
                stream.write(buf, charsWritten);
            }
        }
        else
        {
            stream.write(format, strlen(format));
        }
        return;
    }

    if (!config3dsMinVersionRequirementsFulfilled(format, versionFromFile)) {
        return;
    }

    if (value != NULL)
    {
        fscanf(stream.get(), format, value);
        if (*value < minValue)
            *value = minValue;
        if (*value > maxValue)
            *value = maxValue;
    }
    else
    {
        fscanf(stream.get(), format);
    }
}

//----------------------------------------------------------------------
// Load / Save a string specific to game.
//----------------------------------------------------------------------
void config3dsReadWriteString(BufferedFileWriter& stream, bool writeMode,
                              const char *writeFormat, char *readFormat,
                              char *value, float versionFromFile)
{
    if (!stream)
        return;

    if (writeMode)
    {
        if (value != NULL)
        {
            char buf[CONFIG_BUF_SIZE];
            int charsWritten = snprintf(buf, sizeof(buf), writeFormat, value);
            
            if (charsWritten > 0 && charsWritten < (int)sizeof(buf)) {
                stream.write(buf, charsWritten);
            }
        }
        else
        {
            stream.write(writeFormat, strlen(writeFormat));
        }
    }
    else
    {
        if (value != NULL)
        {
            fscanf(stream.get(), readFormat, value);
        }
        else
        {
            fscanf(stream.get(), readFormat);
        }
    }
}


void config3dsReadWriteBitmask(BufferedFileWriter& stream, bool writeMode,
                               const char* format, uint32* bitmask, float versionFromFile)
{
    int tmp = static_cast<int>(*bitmask);
    config3dsReadWriteInt32(stream, writeMode, format, &tmp,
                            std::numeric_limits<int>::min(),
                            std::numeric_limits<int>::max());
    *bitmask = static_cast<uint32>(tmp);
}
