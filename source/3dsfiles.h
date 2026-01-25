
#ifndef _3DSFILES_H
#define _3DSFILES_H

#include <string>
#include <vector>
#include <3ds.h>

enum class FileEntryType { ParentDirectory, ChildDirectory, File };

#define PARENT_DIRECTORY_LABEL "  ... Parent Directory"
#define CACHE_LINE_SIZE     32

struct DirectoryEntry {
    std::string Filename;
    FileEntryType Type;

    DirectoryEntry(const std::string& filename, FileEntryType type)
    : Filename(filename), Type(type) { }
};

struct DirectoryStatusEntry {
    bool completed;
    unsigned short int currentRomCount;
    unsigned short int totalRomCount;

};

struct StoredFile {
   std::vector<unsigned char> Buffer;
   std::string Filename;
};



// data buffer
// holds the actual file content (png pixel data, save state data, etc.)
extern u8* g_fileBuffer;      
extern size_t g_fileBufferSize;

// stream buffer (32KB)
// optimizes the transport layer (fread/fwrite/fseek)
extern u8 g_streamBuffer[CACHE_LINE_SIZE * 1024];

inline void file3dsAssignStreamBuffer(FILE* fp) {
    if (fp) {
        setvbuf(fp, (char*)g_streamBuffer, _IOFBF, sizeof(g_streamBuffer));
    }
}

//----------------------------------------------------------------------
// Initialize the library
//----------------------------------------------------------------------
void file3dsInitialize(void);

//----------------------------------------------------------------------
// Finalize the library
//----------------------------------------------------------------------
void file3dsCleanStores(bool exit = false);


//----------------------------------------------------------------------
// Gets the current directory.
//----------------------------------------------------------------------
char *file3dsGetCurrentDir(void);


//----------------------------------------------------------------------
// Gets total number of roms in current directory
//----------------------------------------------------------------------
unsigned short file3dsGetCurrentDirRomCount(void);


//----------------------------------------------------------------------
// Go up or down a level.
//----------------------------------------------------------------------
void file3dsGoUpOrDownDirectory(const DirectoryEntry& entry);


//----------------------------------------------------------------------
// Go up to the parent directory.
//----------------------------------------------------------------------
void file3dsGoToParentDirectory(void);


//----------------------------------------------------------------------
// Go up to the child directory.
//----------------------------------------------------------------------
void file3dsGoToChildDirectory(const char* childDir);


//----------------------------------------------------------------------
// Fetch all file names with any of the given extensions
//----------------------------------------------------------------------
bool file3dsGetFiles(std::vector<DirectoryEntry>& files, const std::vector<std::string>& extensions, const char* startDir);
bool file3dsSetThumbnailSubDirectories(const char* type);
bool file3dsthumbnailsAvailable(const char* type);
void file3dsSetRomNameMappings(const char* file);
void file3dsSetCurrentDir();

bool isRomFileNamesUpdating();
bool IsFileExists(const char * filename);
bool file3dsIsValidFilename(const char* filename, const std::vector<std::string>& extensions);
StoredFile file3dsAddFileBufferToMemory(const std::string& id, const std::string& filename);


std::string file3dsGetCurrentDirName();
std::string file3dsGetFileBasename(const char* filename, bool ext);
std::string file3dsGetTrimmedFileBasename(const char* filename, bool ext);
std::string file3dsGetAssociatedFilename(const char* filename, const char* ext, const char* targetDir, bool trimmed = false);
StoredFile file3dsGetStoredFileById(const std::string& id);

#endif