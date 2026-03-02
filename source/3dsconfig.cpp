
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

//----------------------------------------------------------------------
// Load / Save an int32 value specific to game.
//----------------------------------------------------------------------
void config3dsReadWriteInt32(BufferedFileWriter& stream, bool writeMode,
                             const char *format, int *value,
                             int minValue, int maxValue)
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
            // only write if it's a comment
            if (format[0] == '#') {
                stream.write(format, strlen(format));
            }
        }

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
        // safe skip: provide a dummy to discard the read value
        int dummy = 0;
        fscanf(stream.get(), format, &dummy);
    }
}

//----------------------------------------------------------------------
// Load / Save a string specific to game.
//----------------------------------------------------------------------
void config3dsReadWriteString(BufferedFileWriter& stream, bool writeMode,
                              const char *writeFormat, char *readFormat,
                              char *value)
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
            // only write if it's a comment
            if (writeFormat[0] == '#') {
                stream.write(writeFormat, strlen(writeFormat));
            }
        }

        return;
    }
    
    if (value != NULL)
    {
        int itemsRead = fscanf(stream.get(), readFormat, value);

        // if itemsRead is 0, the value was empty (e.g. "DefaultDir=\n")
        if (itemsRead == 0) 
        {
            // set string to empty + anually consume the newline that caused the failure
            value[0] = '\0';
            char c = fgetc(stream.get());
            
            // if we get something other than a newline (rare), put it back
            if (c != '\n' && c != '\r' && c != EOF) {
                ungetc(c, stream.get());
            }
        }
    }
    else
    {
        // safe skip: provide a dummy to discard the read value
        // CONFIG_BUF_SIZE should be large enough to hold whatever line we are skipping
        char dummy[CONFIG_BUF_SIZE];
        fscanf(stream.get(), readFormat, dummy);
    }
}


void config3dsReadWriteBitmask(BufferedFileWriter& stream, bool writeMode,
                               const char* format, u32* bitmask)
{
    int tmp = static_cast<int>(*bitmask);
    config3dsReadWriteInt32(stream, writeMode, format, &tmp,
                            std::numeric_limits<int>::min(),
                            std::numeric_limits<int>::max());
    *bitmask = static_cast<u32>(tmp);
}