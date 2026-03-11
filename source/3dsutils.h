#ifndef _3DSUTILS_H_
#define _3DSUTILS_H_

#include <3ds.h>
#include <cstdio>

#include "3dsgpu.h"

void utils3dsInitialize();
int utils3dsGetRandomInt(int min, int max, int excluded = -1);
bool utils3dsIsAllUppercase(const char* text);

// time_t timestamp to "YYYY-MM-DD HH:MM".
// returns true if successful, false if buffer too small.
bool utils3dsGetFormattedDate(time_t timestamp, char* output, size_t bufferSize);

// DJB2
u32 utils3dsHashString(const char* str);

void utils3dsGetSanitizedPath(const char* path, char* output, size_t bufferSize);
void utils3dsGetBasename(const char* path, char* output, size_t bufferSize, bool keepExtension);

// Removes " (USA)", "[!]", etc.
void utils3dsGetTrimmedBasename(const char* path, char* output, size_t bufferSize, bool keepExtension);

void utils3dsDebugPause();

const char* utils3dsTextureIDToString(SGPU_TEXTURE_ID id);
const char* utils3dsTexColorToString(GPU_TEXCOLOR color);
const char* utils3dsVboIDToString(SGPU_VBO_ID color);

#endif