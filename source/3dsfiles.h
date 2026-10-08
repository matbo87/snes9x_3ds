
#ifndef _3DSFILES_H
#define _3DSFILES_H

#include <3ds.h>
#include <limits.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "3dsmenu.h"
#include "3dssettings.h"

enum class FileEntryType { ParentDirectory, ChildDirectory, File };

#define PARENT_DIRECTORY_LABEL "  ... Parent Directory"

#define DIRECTORY_CACHE_THRESHOLD 50
#define DIRECTORY_CACHE_VERSION 1

#define GAME_SCREEN_BG_DIR      "backgrounds/game_screen"
#define SECOND_SCREEN_BG_DIR    "backgrounds/second_screen"
#define OVERLAYS_DIR            "overlays"
#define CHEATS_DIR              "cheats"

#define CACHE_LINE_SIZE     32

// 512kb buffer
// sufficient for snes9x save states and our ui textures (<= 512x256xRGBA8)
#define MAX_IO_BUFFER_SIZE (512 * 256 * 4)

struct DirectoryEntry {
    char Filename[NAME_MAX + 1];
    FileEntryType Type;

    DirectoryEntry() {
        Filename[0] = '\0';
        Type = FileEntryType::File;
        
    }

    DirectoryEntry(const char* name, FileEntryType type) {
        snprintf(Filename, sizeof(Filename), "%.*s",
                 static_cast<int>(sizeof(Filename) - 1),
                 name ? name : "");

        Type = type;
    }
    
    operator const char*() const { return Filename; }
};

// data buffer
// holds the actual file content (png pixel data, save state data, etc.)
extern u8* g_fileBuffer;      

// stream buffer (32KB)
// optimizes the transport layer (fread/fwrite/fseek)
extern u8 g_streamBuffer[CACHE_LINE_SIZE * 1024];
extern FILE* g_streamBufferOwner;

// Opens a file and assigns the shared stream buffer if available.
// Only one file at a time can use the buffer (guarded by g_streamBufferOwner).
inline FILE* file3dsOpen(const char* filename, const char* mode) {
    FILE* fp = fopen(filename, mode);

    if (fp && g_streamBufferOwner == NULL) {
        g_streamBufferOwner = fp;
        setvbuf(fp, (char*)g_streamBuffer, _IOFBF, sizeof(g_streamBuffer));
    }

    return fp;
}

inline int file3dsClose(FILE* fp) {
    if (!fp) return 0;

    if (g_streamBufferOwner == fp) {
        g_streamBufferOwner = NULL;
    }
    
    return fclose(fp);
}

bool file3dsInitialize();
void file3dsFinalize();

void file3dsRefreshThumbnailTypes();

void file3dsGoUpOrDownDirectory(const DirectoryEntry& entry);
void file3dsGoToParentDirectory(void);
void file3dsGoToChildDirectory(const char* childDir);

void file3dsSetDefaultDir(bool clear);
void file3dsSetCurrentDir(const char* targetDir = NULL);
char *file3dsGetCurrentDir(void);
void file3dsGetCurrentDirName(char* output, size_t bufferSize);
int file3dsGetCurrentDirRomCount(void);
void file3dsGetCurrentDirCacheName(char* output, size_t bufferSize);
const char* file3dsGetCurrentDirCacheDate();
bool file3dsIsCurrentDirLoadedFromCache();
void file3dsDeleteCurrentDirCache();

bool file3dsGetFiles(std::vector<DirectoryEntry>& files, std::vector<SMenuTab>& menuTabs, bool showCachingIndicator = false);
bool file3dsThumbnailsAvailableByType(Setting::ThumbnailMode mode);
const char* file3dsThumbnailCacheName(Setting::ThumbnailMode mode);
const char* file3dsThumbnailLabel(Setting::ThumbnailMode mode);
void file3dsSetRomNameMappings(const char* file);

bool IsFileExists(const char * filename);

struct FileInfo {
    bool exists;
    u64 mtime;   // 0 when unknown
};

// Cached per session to avoid SD lookups; refresh re-probes the path.
const FileInfo& file3dsGetFileInfo(const char* path, bool refresh = false);
void file3dsSetFileInfo(const char* path, const FileInfo& info);

// Removes targetPath before renaming. Failed removal deletes temporaryPath;
// failed rename preserves temporaryPath for recovery.
bool file3dsReplaceFile(const char* temporaryPath, const char* targetPath);

bool file3dsIsValidFilename(const char* filename);

// full path for related files (saves, configs, etc.)
void file3dsGetRelatedPath(const char* path, char* output, size_t bufferSize, const char* ext, const char* targetDir, bool trimmed = false);


#endif
