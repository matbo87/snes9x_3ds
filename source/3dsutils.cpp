#include "3dsutils.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void utils3dsInitialize() {
    srand(svcGetSystemTick());
}


bool utils3dsIsAllUppercase(const char* text) {
    if (!text) return false;

    for (; *text; ++text) {
        // cast to safely handle non-ASCII characters
        u8 c = (u8)*text;
        
        if (isalpha(c) && !isupper(c)) {
            return false;
        }
    }

    return true;
}

u32 utils3dsHashString(const char* str) {
    u32 hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }
    return hash;
}

int utils3dsGetRandomInt(int min, int max, int excluded) {
    if (max <= min) return min;

    // if excluded is not set, we want the full inclusive range (max - min + 1).
    int range = (excluded == -1) ? (max - min + 1) : (max - min);

    if (range <= 0) return min; // safety check
    
    int random = min + (rand() % range);
    
    if (excluded != -1 && random >= excluded) {
        random++;
    }
    
    return random;
}

bool utils3dsGetFormattedDate(time_t timestamp, char* output, size_t bufferSize) {
    if (!output || bufferSize == 0) return false;

    struct tm* t = localtime(&timestamp);
    if (!t) return false;

    // e.g. 2026-02-14 07:51
    size_t len = strftime(output, bufferSize, "%Y-%m-%d %H:%M", t);
    
    return (len > 0);
}


void utils3dsGetSanitizedPath(const char* path, char* output, size_t bufferSize) {
    if (bufferSize == 0) return;
    
    static const char* invalid_chars = "/\\:*?\"<>|";
    
    // skip "sdmc:/" prefix
    const char* start = (strncmp(path, "sdmc:/", 6) == 0) ? path + 6 : path;
    
    // skip leading slash
    if (*start == '/') start++;
    
    size_t i = 0;
    while (*start && i < bufferSize - 1) {
        char c = *start++;
        // replace invalid char with '_'
        output[i++] = strchr(invalid_chars, c) ? '_' : c;
    }
    
    // remove trailing underscore if present
    if (i > 0 && output[i-1] == '_') i--;
    
    output[i] = '\0';
}

void utils3dsGetBasename(const char* path, char* output, size_t bufferSize, bool keepExtension) {
    if (!path || !output || bufferSize == 0) return;

    // find the last slash to get the filename start
    const char* slash = strrchr(path, '/');
    const char* start = (slash) ? slash + 1 : path;

    snprintf(output, bufferSize, "%s", start);

    // remove extension if requested
    if (!keepExtension) {
        char* dot = strrchr(output, '.');
        if (dot) *dot = '\0';
    }
}

void utils3dsGetTrimmedBasename(const char* path, char* output, size_t bufferSize, bool keepExtension) {
    if (!path || !output || bufferSize == 0) return;

    utils3dsGetBasename(path, output, bufferSize, true);

    char* extPtr = strrchr(output, '.');
    char extension[16] = "";

    // if an extension exists AND we want to keep it
    if (extPtr && keepExtension) {
        snprintf(extension, sizeof(extension), "%s", extPtr);
    }

    // find first occurrence of '(' or '['
    // strpbrk finds the first character from the set that appears in the string
    char* bracket = strpbrk(output, "([");
    
    if (bracket) {
        // only trim if the bracket appears BEFORE the extension (or if there is no extension)
        if (!extPtr || bracket < extPtr) {
            *bracket = '\0';
            
            // trim trailing whitespace
            size_t len = strlen(output);
            while (len > 0 && (output[len - 1] == ' ' || output[len - 1] == '\t')) {
                output[--len] = '\0';
            }

            // re-append extension if requested
            if (keepExtension && extension[0] != '\0') {
                strncat(output, extension, bufferSize - strlen(output) - 1);
            }
            return;
        }
    }
    
    // no brackets found -> remove extension
    if (!keepExtension && extPtr) {
        *extPtr = '\0';
    }
}

void utils3dsDebugPause() {
    bool fastMode = false;
    while (aptMainLoop()) {
        hidScanInput();
        u32 keys = hidKeysHeld();
        fastMode = keys & (KEY_RIGHT | KEY_R | KEY_ZR);
        u32 kDown = fastMode ? keys : hidKeysDown();
        if (kDown) break;
    }
}

const char* utils3dsTextureIDToString(SGPU_TEXTURE_ID id) {
    switch (id) {
        case SNES_MAIN:                 return "main";
        case SNES_SUB:                  return "sub";
        case SNES_DEPTH:                return "depth";
        case SNES_MODE7_FULL:           return "m7 full";
        case SNES_MODE7_TILE_0:         return "m7 zero";
        case SNES_MAIN_R:              return "main R";
        case SNES_TILE_CACHE:           return "tile cache";
        case SNES_MODE7_TILE_CACHE:     return "m7 tile cache";
        case UI_OVERLAY:                return "overlay";
        case UI_BG_GAME:                return "bg game";
        case UI_BG_SECOND:              return "bg second";
        case UI_ATLAS:                  return "atlas";
        case UI_NOTIF_MSG:              return "notif msg";
        case UI_NOTIF_FPS:              return "notif fps";
        default:                        return "invalid";
    }
}

const char* utils3dsTexColorToString(GPU_TEXCOLOR color) {
    switch (color) {
        case GPU_RGBA8:     return "GPU_RGBA8";
        case GPU_RGB8:      return "GPU_RGB8";
        case GPU_RGBA5551:  return "GPU_RGBA5551";
        case GPU_RGB565:    return "GPU_RGB565";
        case GPU_RGBA4:     return "GPU_RGBA4";
        case GPU_A8:        return "GPU_A8";
        case GPU_ETC1:      return "GPU_ETC1";
        case GPU_ETC1A4:    return "GPU_ETC1A4";
        default:            return "unknown";
    }
}

const char* utils3dsVboIDToString(SGPU_VBO_ID id) {
    switch (id) {
        case VBO_SCENE_RECT:      return "vbo rect";
        case VBO_SCENE_TILE:      return "vbo tile";
        case VBO_SCENE_MODE7_LINE:return "vbo m7 line";
        case VBO_MODE7_TILE:      return "vbo m7 tile";
        case VBO_SCREEN:          return "vbo screen";
        default:                  return "invalid";
    }
}