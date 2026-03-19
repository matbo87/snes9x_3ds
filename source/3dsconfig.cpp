
#include <limits>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3dsconfig.h"
#include "3dslog.h"

// 256 should be enough for "Key=Value\n" lines
#define CONFIG_BUF_SIZE 256

static bool parseMismatchWarningLogged = false;

void config3dsResetParseWarning() {
    parseMismatchWarningLogged = false;
}

static void config3dsLogParseMismatchOnce(const char *format) {
    if (!parseMismatchWarningLogged) {
        log3dsWrite("Config parse mismatch near format '%s'; keeping defaults for remaining settings", format);
        parseMismatchWarningLogged = true;
    }
}

float config3dsGetVersionFromFile(bool isGameConfig, char *versionStringFromFile) {
    float latestVersion = isGameConfig ? GAME_CONFIG_FILE_TARGET_VERSION : GLOBAL_CONFIG_FILE_TARGET_VERSION;
    
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
        int itemsRead = fscanf(stream.get(), format, value);
        if (itemsRead != 1) {
            config3dsLogParseMismatchOnce(format);
            return;
        }

        if (*value < minValue)
            *value = minValue;
        if (*value > maxValue)
            *value = maxValue;
    }
    else
    {
        // safe skip: provide a dummy to discard the read value
        int dummy = 0;
        int itemsRead = fscanf(stream.get(), format, &dummy);
        if (itemsRead != 1) {
            config3dsLogParseMismatchOnce(format);
        }
    }
}

//----------------------------------------------------------------------
// Load / Save a string specific to game.
//----------------------------------------------------------------------
void config3dsReadWriteString(BufferedFileWriter& stream, bool writeMode,
                              const char *writeFormat, const char *readFormat,
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
            // set string to empty + manually consume the newline that caused the failure
            value[0] = '\0';
            int c = fgetc(stream.get());

            // if we get something other than a newline, this was likely a key mismatch
            if (c != '\n' && c != '\r' && c != EOF) {
                config3dsLogParseMismatchOnce(readFormat);
                ungetc(c, stream.get());
            }
            else if (c == EOF) {
                config3dsLogParseMismatchOnce(readFormat);
            }
        }
        else if (itemsRead == EOF) {
            value[0] = '\0';
            config3dsLogParseMismatchOnce(readFormat);
        }
    }
    else
    {
        // safe skip: provide a dummy to discard the read value
        // CONFIG_BUF_SIZE should be large enough to hold whatever line we are skipping
        char dummy[CONFIG_BUF_SIZE];
        int itemsRead = fscanf(stream.get(), readFormat, dummy);
        if (itemsRead != 1) {
            config3dsLogParseMismatchOnce(readFormat);
        }
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
