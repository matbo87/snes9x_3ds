
#include <limits>
#include <stdio.h>
#include <string.h>

#include "3dsconfig.h"

//----------------------------------------------------------------------
// Load / Save an int32 value specific to game.
//----------------------------------------------------------------------
void config3dsReadWriteInt32(BufferedFileWriter& stream, bool writeMode,
                             const char *format, int *value,
                             int minValue, int maxValue)
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
    }
    else
    {
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
                               const char* format, uint32* bitmask)
{
    int tmp = static_cast<int>(*bitmask);
    config3dsReadWriteInt32(stream, writeMode, format, &tmp,
                            std::numeric_limits<int>::min(),
                            std::numeric_limits<int>::max());
    *bitmask = static_cast<uint32>(tmp);
}
