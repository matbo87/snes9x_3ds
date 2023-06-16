
#ifndef _3DSFILES_H
#define _3DSFILES_H

#include <string>
#include <vector>
#include <unordered_map>

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
    std::string filename;
    std::string path;
    std::vector<unsigned char> buffer;
};

bool file3dsAddFileToMemory(const std::string& filename, const std::string& path);

std::unordered_map<std::string, DirectoryStatusEntry>::iterator file3dsGetDirStatus(const std::string& lookupId);
void file3dsSetDirStatus(const std::string& lookupId, DirectoryStatusEntry value);

StoredFile* file3dsGetStoredFileByFilename(const std::string& filename);
StoredFile* file3dsGetStoredFileByPath(const std::string& path);
std::string file3dsGetThumbnailPathByFilename(const std::string& filename);

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
std::string file3dsGetTrimmedFilename(const char* filename);

#endif