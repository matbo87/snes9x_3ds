
#include <limits>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3dsconfig.h"



float config3dsGetVersionFromFile(bool writeMode, bool isGameConfig, char *versionStringFromFile) {
    bool latestVersion = isGameConfig ? GAME_CONFIG_FILE_TARGET_VERSION : GLOBAL_CONFIG_FILE_TARGET_VERSION;
    
    if (writeMode) {
        return latestVersion;
    }

    char *endptr;
    float detectedVersion = strtof(versionStringFromFile, &endptr);

    if (*endptr != '\0') {
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
    //if (strlen(format) == 0)
    //    return;

    if (writeMode)
    {
        if (value != NULL)
        {
            //printf ("Writing %s %d\n", format, *value);
            int len = snprintf(NULL, 0, format, *value) + 1;
            if (len < 0)
                return;
            char buf[len];
            snprintf(buf, len, format, *value);
            stream.write(buf, len - 1);
        }
        else
        {
            //printf ("Writing %s\n", format);
            stream.write(format, strlen(format));
        }

        return;
    }

    if (!config3dsMinVersionRequirementsFulfilled(format, versionFromFile)) {
        return;
    }

    if (value != NULL)
    {
        fscanf(stream.rawFilePointer(), format, value);
        if (*value < minValue)
            *value = minValue;
        if (*value > maxValue)
            *value = maxValue;
    }
    else
    {
        fscanf(stream.rawFilePointer(), format);
        //printf ("skipped line\n");
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
            //printf ("Writing %s %s\n", format, value);
            int len = snprintf(NULL, 0, writeFormat, value) + 1;
            if (len < 0)
                return;
            char buf[len];
            snprintf(buf, len, writeFormat, value);
            stream.write(buf, len - 1);
        }
        else
        {
            //printf ("Writing %s\n", format);
            stream.write(writeFormat, strlen(writeFormat));
        }
    }
    else
    {
        if (value != NULL)
        {
            fscanf(stream.rawFilePointer(), readFormat, value);
            //printf ("Scanned %s\n", value);
        }
        else
        {
            fscanf(stream.rawFilePointer(), readFormat);
            //fscanf(fp, "%s", dummyString);
            //printf ("skipped line\n");
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
