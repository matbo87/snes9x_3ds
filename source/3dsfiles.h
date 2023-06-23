
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
void file3dsGetFiles(std::vector<DirectoryEntry>& files);

bool IsFileExists(const char * filename);
bool file3dsIsValidFilename(const char* filename);
bool file3dsAddFileBufferToMemory(const std::string& filename);

void file3dsGetDirStatus(const std::string& lookupId, bool& completed, unsigned short& currentRomCount, unsigned short& totalRomCount);
void file3dsSetDirStatus(const std::string& lookupId, unsigned short currentRomCount, unsigned short totalRomCount);

std::string file3dsGetFileBasename(const char* filename, bool ext);
std::string file3dsGetTrimmedFileBasename(const char* filename, bool ext);
std::string file3dsGetAssociatedFilename(const char* filename, const char* ext, const char* targetDir, bool trimmed = false);
std::vector<unsigned char> file3dsGetStoredBufferByFilename(const std::string& filename);

#endif