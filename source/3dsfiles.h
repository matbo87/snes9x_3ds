
#ifndef _3DSFILES_H
#define _3DSFILES_H

#include <3ds.h>
#include <limits.h>
#include <cstring>
#include <string>
#include <vector>

#include "3dsmenu.h"

enum class FileEntryType { ParentDirectory, ChildDirectory, File };

#define PARENT_DIRECTORY_LABEL "  ... Parent Directory"

#define DIRECTORY_CACHE_THRESHOLD 50
#define DIRECTORY_CACHE_VERSION 1

#define CACHE_LINE_SIZE     32

#define MAX_THUMB_TYPES 3

struct DirectoryEntry {
    char Filename[NAME_MAX + 1];
    FileEntryType Type;

    DirectoryEntry() {
        Filename[0] = '\0';
        Type = FileEntryType::File;
        
    }

    DirectoryEntry(const char* name, FileEntryType type) {
        strncpy(Filename, name, sizeof(Filename));
        Filename[sizeof(Filename) - 1] = '\0';

        Type = type;
    }
    
    operator const char*() const { return Filename; }
};

// data buffer
// holds the actual file content (png pixel data, save state data, etc.)
extern u8* g_fileBuffer;      
extern u32 g_fileBufferSize;

// stream buffer (32KB)
// optimizes the transport layer (fread/fwrite/fseek)
extern u8 g_streamBuffer[CACHE_LINE_SIZE * 1024];

inline void file3dsAssignStreamBuffer(FILE* fp) {
    if (fp) {
        setvbuf(fp, (char*)g_streamBuffer, _IOFBF, sizeof(g_streamBuffer));
    }
}

void file3dsInitialize(void);
void file3dsFinalize();

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
void file3dsDeleteCurrentDirCache();

bool file3dsGetFiles(std::vector<DirectoryEntry>& files, std::vector<SMenuTab>& menuTabs, bool showCachingIndicator = false);
bool file3dsThumbnailsAvailable();
bool file3dsThumbnailsAvailableByType(const char* type);
void file3dsSetRomNameMappings(const char* file);

bool IsFileExists(const char * filename);
bool file3dsIsValidFilename(const char* filename);

// full path for related files (saves, configs, etc.)
void file3dsGetRelatedPath(const char* path, char* output, size_t bufferSize, const char* ext, const char* targetDir, bool trimmed = false);


#endif