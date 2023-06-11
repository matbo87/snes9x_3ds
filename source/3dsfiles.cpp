#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <tuple>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <3ds.h>
#include <dirent.h>

#include "port.h"
#include "3dsfiles.h"

namespace fs = std::filesystem;

inline std::string operator "" s(const char* s, size_t length) {
    return std::string(s, length);
}

std::vector<StoredFile> storedFiles;
static char currentDir[_MAX_PATH] = "";

//----------------------------------------------------------------------
// Initialize the library
//----------------------------------------------------------------------
void file3dsInitialize(void)
{
    getcwd(currentDir, _MAX_PATH);
#ifdef RELEASE
    if (currentDir[0] == '/')
    {
        char tempDir[_MAX_PATH];

        sprintf(tempDir, "sdmc:%s", currentDir);
        strcpy(currentDir, tempDir);
    }
#endif
}


//----------------------------------------------------------------------
// Gets the current directory.
//----------------------------------------------------------------------
char *file3dsGetCurrentDir(void)
{
    return currentDir;
}


//----------------------------------------------------------------------
// Go up or down a level.
//----------------------------------------------------------------------
void file3dsGoUpOrDownDirectory(const DirectoryEntry& entry) {
    if (entry.Type == FileEntryType::ParentDirectory) {
        file3dsGoToParentDirectory();
    } else if (entry.Type == FileEntryType::ChildDirectory) {
        file3dsGoToChildDirectory(entry.Filename.c_str());
    }
}

//----------------------------------------------------------------------
// Count the directory depth. 1 = root folder
//----------------------------------------------------------------------
int file3dsCountDirectoryDepth(char *dir)
{
    int depth = 0;
    for (int i = 0; i < strlen(dir); i++)
        if (dir[i] == '/')
            depth++;
    return depth;
}

//----------------------------------------------------------------------
// Go up to the parent directory.
//----------------------------------------------------------------------
void file3dsGoToParentDirectory(void)
{
    int len = strlen(currentDir);

    if (len > 1)
    {
        for (int i = len - 2; i>=0; i--)
        {
            if (currentDir[i] == '/')
            {
                currentDir[i + 1] = 0;
                break;
            }
        }
    }
}

//----------------------------------------------------------------------
// Checks if file exists.
//----------------------------------------------------------------------
bool IsFileExists(const char * filename) {
    if (FILE * file = fopen(filename, "r")) {
        fclose(file);
        return true;
    }
    return false;
}


//----------------------------------------------------------------------
// Go up to the child directory.
//----------------------------------------------------------------------
void file3dsGoToChildDirectory(const char* childDir)
{
    strncat(currentDir, &childDir[2], _MAX_PATH);
    strncat(currentDir, "/", _MAX_PATH);
}

StoredFile* file3dsGetStoredFileByPath(const std::string& path) {
    for (auto& file : storedFiles) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

StoredFile* file3dsGetStoredFileByFilename(const std::string& filename) {
    for (auto& file : storedFiles) {
        if (file.filename == filename) {
            return &file;
        }
    }
    return nullptr;
}

bool file3dsAddFileToMemory(const std::string& filename, const std::string& path) {
    if (path.empty()) {
        return false;
    }

    // file already stored
    if (file3dsGetStoredFileByPath(path) != nullptr) {
       return true;
    }

    std::ifstream file(path, std::ios::binary);

    if (!file) {
        return false;
    }
    
    // Get the file size
    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // Read the file data into a buffer
    std::vector<unsigned char> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    
    // Create instance directly in place
    storedFiles.emplace_back(StoredFile{filename, path, std::move(buffer)});
    
    file.close();

    return true;
}

//----------------------------------------------------------------------
// Fetch all file names with any of the given extensions
//----------------------------------------------------------------------
void file3dsGetFiles(std::vector<DirectoryEntry>& files, const std::vector<std::string>& extensions)
{
    files.clear();

    if (currentDir[0] == '/')
    {
        char tempDir[_MAX_PATH];
        sprintf(tempDir, "sdmc:%s", currentDir);
        strcpy(currentDir, tempDir);
    }

    struct dirent* dir;
    DIR* d = opendir(currentDir);

    if (file3dsCountDirectoryDepth(currentDir) > 1)
    {
        // Insert the parent directory.
        files.emplace_back(".. (Up to Parent Directory)"s, FileEntryType::ParentDirectory);
    }

    if (d)
    {
        while ((dir = readdir(d)) != NULL)
        {
            if (dir->d_name[0] == '.')
                continue;
            if (dir->d_type == DT_DIR)
            {
                files.emplace_back(std::string("\x01 ") + std::string(dir->d_name), FileEntryType::ChildDirectory);
            }
            if (dir->d_type == DT_REG)
            {
                if (file3dsIsValidFilename(dir->d_name, extensions))
                {
                    files.emplace_back(std::string(dir->d_name), FileEntryType::File);
                }
            }
        }
        closedir(d);
    }

    std::sort( files.begin(), files.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
        return std::tie(a.Type, a.Filename) < std::tie(b.Type, b.Filename);
    } );
}
bool file3dsIsValidFilename(const char* filename,  const std::vector<std::string>& extensions) {
    fs::path filePath(filename);
    
    if (!filePath.has_extension() || filePath.filename().string().front() == '.') {
        return false;
    }

    std::string extension = filePath.extension().string();
    auto it = std::find(extensions.begin(), extensions.end(), extension);
    
    return it != extensions.end();
}
