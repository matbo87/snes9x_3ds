
#ifndef _3DSFILES_H
#define _3DSFILES_H

#include <string>
#include <vector>

enum class FileEntryType { ParentDirectory, ChildDirectory, File };

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

//----------------------------------------------------------------------
// Initialize the library
//----------------------------------------------------------------------
void file3dsInitialize(void);

//----------------------------------------------------------------------
// Finalize the library
//----------------------------------------------------------------------
void file3dsFinalize(void);


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
void file3dsGetFiles(std::vector<DirectoryEntry>& files, const std::vector<std::string>& extensions);
void file3dsSetthumbnailDirectories(const char* type);
bool file3dsGetThumbnailsUpdated();
void file3dsSetThumbnailsUpdated(bool updated);

void file3dsSetRomNameMappings(const char* file);

bool IsFileExists(const char * filename);
bool file3dsIsValidFilename(const char* filename, const std::vector<std::string>& extensions);
StoredFile file3dsAddFileBufferToMemory(const std::string& id, const std::string& filename);


std::string file3dsGetFileBasename(const char* filename, bool ext);
std::string file3dsGetTrimmedFileBasename(const char* filename, bool ext);
std::string file3dsGetAssociatedFilename(const char* filename, const char* ext, const char* targetDir, bool trimmed = false);
StoredFile file3dsGetStoredFileById(const std::string& id);

#endif