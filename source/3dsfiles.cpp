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
#include <initializer_list>

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

const std::initializer_list<std::string> VALID_ROM_EXTENSIONS = {".smc", ".sfc", ".fig"};
std::unordered_map<std::string, StoredDirectoryEntry> checkedDirectories;
std::vector<StoredFile> storedFiles;
std::vector<std::string> thumbnailFolders;
std::string thumbnailDirectory;

static char currentDir[_MAX_PATH] = "";

void file3dsSetThumbnailFolders(std::string type) {
    thumbnailDirectory = "/3ds/snes9x_3ds/thumbnails/" + type;
    
    DIR* directory = opendir(thumbnailDirectory.c_str());
    if (directory == nullptr) {
        return;
    }

    struct dirent* entry;
    
    while ((entry = readdir(directory)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            std::string folderName = entry->d_name;
            if (folderName != "." && folderName != "..") {
                thumbnailFolders.emplace_back(folderName);
            }
        }
    }

    closedir(directory);
    std::sort(thumbnailFolders.begin(), thumbnailFolders.end());
}

//----------------------------------------------------------------------
// Initialize the library
//----------------------------------------------------------------------
void file3dsInitialize(void)
{
    file3dsSetThumbnailFolders("snaps");
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

void file3dsFinalize(void)
{
    thumbnailFolders.clear();
    storedFiles.clear();
    checkedDirectories.clear();
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

std::string file3dsGetThumbnailPathByFilename(const std::string& filename) {
    if (thumbnailFolders.empty()) {
        return "";
    }

    std::string filenameUppercase;
    std::transform(filename.begin(), filename.end(), std::back_inserter(filenameUppercase), [](unsigned char c) {
        return std::toupper(c);
    });

    char firstChar = std::toupper(filenameUppercase[0]);

    // filenames starting with a non-alpha char
    if (!std::isalpha(firstChar)) {
        return thumbnailDirectory + "/" + thumbnailFolders[0] + "/" + filename + ".png";
    }
    
    std::string relatedFolderName;

    for (const std::string& folderName : thumbnailFolders) {
        std::string folderNameUppercase;
        std::transform(folderName.begin(), folderName.end(), std::back_inserter(folderNameUppercase), [](unsigned char c) {
            return std::toupper(c);
        });

        size_t separatorPos = folderNameUppercase.find("-");

        if (separatorPos != std::string::npos) {
            std::string firstPart = folderNameUppercase.substr(0, separatorPos);
            std::string secondPart = folderNameUppercase.substr(separatorPos + 1);
            
            if (firstChar != firstPart[0] && firstChar != secondPart[0]) {
                continue;
            }

            // e.g. filename "s" would match "Sa-Si"
            if (filenameUppercase.length() <= 1) {
                relatedFolderName = folderName;
                break;
            }

            char secondChar = filenameUppercase[1];
            char secondCharStart = (firstPart.length() > 1) ? firstPart[1] : secondChar;
            char secondCharEnd = (secondPart.length() > 1) ? secondPart[1] : secondChar;
            
            for (char c = secondCharStart; c <= secondCharEnd; ++c) {
                if (c == secondChar) {
                    relatedFolderName = folderName;
                    break;
                }
            }

            if (!relatedFolderName.empty()) {
                break;
            }
        } else {
            if (filenameUppercase.compare(0, folderNameUppercase.length(), folderNameUppercase) == 0) {
                relatedFolderName = folderName;
                break;
            }
        }
    }

    if (relatedFolderName.empty()) {
        return "";
    }

    return thumbnailDirectory + "/" + relatedFolderName + "/" + filename + ".png";
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
       return false;
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
void file3dsGetFiles(std::vector<DirectoryEntry>& files)
{
    files.clear();

    if (currentDir[0] == '/')
    {
        char tempDir[_MAX_PATH];
        sprintf(tempDir, "sdmc:%s", currentDir);
        strcpy(currentDir, tempDir);
    }

    if (checkedDirectories.find(currentDir) == checkedDirectories.end()) {
        checkedDirectories[currentDir] = { false, 0 };
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
        int romCount = 0;
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
                if (file3dsIsValidFilename(dir->d_name))
                {
                    files.emplace_back(std::string(dir->d_name), FileEntryType::File);
                    romCount++;
                }
            }
        }

        closedir(d);

        checkedDirectories[currentDir].romCount = romCount;
    }

    std::sort( files.begin(), files.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
        return std::tie(a.Type, a.Filename) < std::tie(b.Type, b.Filename);
    } );
}

std::unordered_map<std::string, StoredDirectoryEntry>::iterator file3dsGetDirStatus(const std::string& lookupId) {
    return checkedDirectories.find(lookupId);
}

void file3dsSetDirStatus(const std::string& lookupId, StoredDirectoryEntry value) {
    checkedDirectories[lookupId] = value;
}

bool file3dsIsValidFilename(const char* filename) {
    fs::path filePath(filename);
    
    if (!filePath.has_extension() || filePath.filename().string().front() == '.') {
        return false;
    }

    std::string extension = filePath.extension().string();
    auto it = std::find(VALID_ROM_EXTENSIONS.begin(), VALID_ROM_EXTENSIONS.end(), extension);
    
    return it != VALID_ROM_EXTENSIONS.end();
}

// e.g. "Donkey Kong Country   (USA) (V1.2) [!]" -> "Donkey Kong Country"
std::string file3dsGetTrimmedFilename(const char* filename) {
    std::string trimmedFilename(filename);

    // Find the position of the last slash or backslash
    size_t lastSlashPos = trimmedFilename.find_last_of("/\\");
    
    // Find the position of the last dot (.)
    size_t lastDotPos = trimmedFilename.find_last_of(".");
    
    // Extract the substring between the last slash and the last dot (if both are found)
    if (lastSlashPos != std::string::npos && lastDotPos != std::string::npos && lastDotPos > lastSlashPos) {
        trimmedFilename = trimmedFilename.substr(lastSlashPos + 1, lastDotPos - lastSlashPos - 1);
    }

    std::size_t startPos = trimmedFilename.find_first_of("([");
    if (startPos != std::string::npos) {
        // remove whitespace
        std::size_t endPos = trimmedFilename.find_last_not_of(" \t\n\r\f\v", startPos - 1);
        return trimmedFilename.substr(0, endPos + 1).c_str();
    }

    return trimmedFilename;
}