#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "3dssettings.h"
#include "3dsfiles.h"
#include "3dslog.h"
#include "3dsui.h"


typedef struct {
    u32 magic;
    u32 version;
    u64 timestamp;
    u32 entryCount;
} FileCacheHeader;

typedef struct {
    char name[NAME_MAX + 1];
    FileEntryType type;
} FileCachedEntry;

static std::unordered_map<std::string, std::string> romNameMappings;

static char availableThumbnailTypes[MAX_THUMB_TYPES][32]; // boxart, title, gameplay
static int availableThumbCount = 0;

static char currentDir[PATH_MAX] = "";
static int currentDirRomCount = 0;

static const char* validExtensions[] = { ".smc", ".sfc", ".fig" };
static const int validExtensionsCount = sizeof(validExtensions) / sizeof(validExtensions[0]);
static const size_t intialFileCapacity = 1024;

// covers the largest possible UI texture (512x256 RGBA8)
// and should be large enough for snes9x save states
u32 g_fileBufferSize = 512 * 256 * 4; // 512kb
u8* g_fileBuffer = NULL;

// aligned stream buffer for optimized DMA/Cache performance
u8 g_streamBuffer[CACHE_LINE_SIZE * 1024] __attribute__((aligned(CACHE_LINE_SIZE)));

void file3dsInitialize(void)
{
    log3dsWrite("check for required directories:");

    const char* directories[] = { "", "configs", "saves", "savestates", "screenshots" };
    char reqDir[128];

    for (const char* dir : directories) {
        if (dir[0] == '\0') {
             snprintf(reqDir, sizeof(reqDir), "%s", settings3DS.RootDir);
        } else {
             snprintf(reqDir, sizeof(reqDir), "%s/%s", settings3DS.RootDir, dir);
        }

        DIR* d = opendir(reqDir);
        if (d) {
            closedir(d);
            log3dsWrite("%s v", reqDir);
        } else {
            mkdir(reqDir, 0777);
            log3dsWrite("%s x (created)", reqDir);
        }
    }
    
    const char* thumbnailTypes[] = { "boxart", "title", "gameplay" };

    log3dsWrite("check for available thumbnail types:");
    char cacheFile[128];

    availableThumbCount = 0; // Reset count

    for (const char* type : thumbnailTypes) {
        snprintf(cacheFile, sizeof(cacheFile), "%s/thumbnails/%s.cache", settings3DS.RootDir, type);

        if (IsFileExists(cacheFile)) {
            if (availableThumbCount < MAX_THUMB_TYPES) {
                snprintf(availableThumbnailTypes[availableThumbCount], 32, "%s", type);
                availableThumbCount++;
            }
            log3dsWrite("%s v", cacheFile);
        } else {
            log3dsWrite("%s x", cacheFile);
        }
    }

    const char* targetDir = NULL;

    // check first, if user has set a default directory
    if (settings3DS.defaultDir[0] != '\0') {
        targetDir = settings3DS.defaultDir;
    }
    else if (settings3DS.lastSelectedDir[0] != '\0') {
        targetDir = settings3DS.lastSelectedDir;
    }
    
    file3dsSetCurrentDir(targetDir);

    DIR* d = opendir(currentDir);
    if (d) {
        closedir(d);
        log3dsWrite("[file3dsInitialize] start directory set to %s", currentDir);
    } 
    else {
        log3dsWrite("[file3dsInitialize] failed to open %s. Fallback to smdc:/", currentDir);
        file3dsSetCurrentDir(NULL);
    }
}

bool IsFileExists(const char * filename) {
    if (filename == nullptr || filename[0] == '\0') {
        return false;
    }

    struct stat buffer;
    
    return (stat(filename, &buffer) == 0);
}

void file3dsSetDefaultDir(bool clear) {
    if (clear) {
        settings3DS.defaultDir[0] = '\0';
    } else {
        snprintf(settings3DS.defaultDir, sizeof(settings3DS.defaultDir), "%s", currentDir);
    }
}

void file3dsSetCurrentDir(const char* targetDir) {
    // default to "sdmc:/"
    if (targetDir == NULL || targetDir[0] == '\0') {
        snprintf(currentDir, sizeof(currentDir), "sdmc:/");

        return;
    }

    // If the path starts with '/' prepend "sdmc:"
    if (targetDir[0] == '/') {
        snprintf(currentDir, sizeof(currentDir), "sdmc:%s", targetDir);
    }
    else {
        snprintf(currentDir, sizeof(currentDir), "%s", targetDir);
    }

    // make sure we only end with a slash
    size_t len = strlen(currentDir);
    if (len > 0 && currentDir[len - 1] != '/') {
        strncat(currentDir, "/", sizeof(currentDir) - len - 1);
    }
}

void file3dsFinalize() 
{
    romNameMappings.clear();
    DUMP_UNORDERED_MAP_INFO("romNameMappings after cleanup", romNameMappings);
}

bool file3dsThumbnailsAvailableByType(const char* type) {
    for (int i = 0; i < availableThumbCount; i++) {
        if (strcmp(availableThumbnailTypes[i], type) == 0) {
            return true;
        }
    }
    return false;
}


bool file3dsThumbnailsAvailable() {
    return availableThumbCount > 0;
}

void file3dsSetRomNameMappings(const char* file) {
    std::ifstream inputFile(file);

    if (!inputFile.is_open()) {
        log3dsWrite("failed to open");


        return;
    }

    romNameMappings.clear();

    std::string line;
    int count = 0;
    while (std::getline(inputFile, line)) {
        size_t delimiterPos = line.find("|");
        if (delimiterPos != std::string::npos) {
            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);
            romNameMappings[key] = value;
            count++;
        }
    }

    log3dsWrite("loaded %d rom name mappings", count);

    inputFile.close();
}


void file3dsGetCurrentDirName(char* output, u32 bufferSize) {
    if (!output || bufferSize == 0) return;
    output[0] = '\0';

    size_t len = strlen(currentDir);
    
    // currentDir implies it always ends in '/'
    if (len < 2) return;

    // start at the character BEFORE the trailing slash
    // e.g. "sdmc:/roms/snes/" (len-1 is slash, len-2 is 's')
    const char* end = currentDir + len - 2;
    const char* start = end;

    // scan backwards to find the slash BEFORE the directory name
    // stop if we hit the beginning of the string
    while (start > currentDir && *start != '/') {
        start--;
    }

    // if we stopped at a slash, move forward one char to get the name start
    if (*start == '/') {
        start++;
    }

    size_t nameLen = (end - start) + 1;
    
    if (nameLen >= bufferSize) nameLen = bufferSize - 1;

    strncpy(output, start, nameLen);
    output[nameLen] = '\0';
}

char *file3dsGetCurrentDir(void)
{
    return currentDir;
}

int file3dsGetCurrentDirRomCount()
{
    return currentDirRomCount;
}

void file3dsGoUpOrDownDirectory(const DirectoryEntry& entry) {
    if (entry.Type == FileEntryType::ParentDirectory) {
        file3dsGoToParentDirectory();
    } else if (entry.Type == FileEntryType::ChildDirectory) {
        file3dsGoToChildDirectory(entry.Filename);
    }
}

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

void file3dsGoToChildDirectory(const char* childDir)
{
    size_t len = strlen(currentDir);
    snprintf(currentDir + len, sizeof(currentDir) - len, "%s/", childDir);
}

u64 file3dsGetDirectoryTimestamp(FS_Archive archive, const char* path) {
    u64 timestamp = 0;
    u16 pathUtf16[PATH_MAX];
    size_t units = utf8_to_utf16(pathUtf16, (const uint8_t*)path, PATH_MAX - 1);
    
    if (units < 0) return 0;

    pathUtf16[units] = 0;

    Result res = FSUSER_ControlArchive(
        archive, ARCHIVE_ACTION_GET_TIMESTAMP, 
        pathUtf16, (units + 1) * sizeof(u16), 
        &timestamp, sizeof(timestamp));

    if (R_FAILED(res)) return 0;
    
    return timestamp;
}

void file3dsConvertUtf16ToChar(const u16* nameUtf16, char* output, u32 bufferSize) {
    const u16* p = nameUtf16;
    size_t i = 0;

    while (*p && i < bufferSize - 1) {
        u16 c = *p++;
        if (c < 0x80) {
            output[i++] = static_cast<char>(c); // fast ascii
        } else {
            ssize_t units = utf16_to_utf8((uint8_t*)output, nameUtf16, bufferSize - 1);
            if (units >= 0) output[units] = '\0';
            else output[0] = '\0';

            return;
        }
    }

    output[i] = '\0';
}

bool file3dsLoadFromCache(std::vector<DirectoryEntry>& files, const char* cachePath, u64 currentDirTimestamp) {
    FILE* fp = fopen(cachePath, "rb");
    if (!fp) return false;

    file3dsAssignStreamBuffer(fp);

    FileCacheHeader header;
    if (fread(&header, sizeof(FileCacheHeader), 1, fp) != 1) {
        fclose(fp);
        return false;
    }

    // if folder modified, cache is invalid
    if (header.magic != 0x534E3958 || header.timestamp != currentDirTimestamp || header.version != DIRECTORY_CACHE_VERSION) {
        fclose(fp);

        return false;
    }

    files.resize(header.entryCount);

    if (header.entryCount > 0) {
        fread(files.data(), sizeof(DirectoryEntry), header.entryCount, fp);
    }
    
    fclose(fp);

    for (const auto& entry : files) {
        if (entry.Type == FileEntryType::File) {
            currentDirRomCount++;
        }
    }

    return true;
}

void file3dsSaveFilesToCache(const std::vector<DirectoryEntry>& files, const char* cachePath, u64 currentDirTimestamp) {
    FILE* fp = fopen(cachePath, "wb");
    if (!fp) return;

    // disable buffering (_IONBF)
    // We are writing the vector in one massive shot. 
    // We don't want the overhead of chopping it into 32KB buffer chunks.
    setvbuf(fp, NULL, _IONBF, 0);

    FileCacheHeader header = { 0x534E3958, DIRECTORY_CACHE_VERSION, currentDirTimestamp, (u32)files.size() };
    
    fwrite(&header, sizeof(FileCacheHeader), 1, fp);

    if (!files.empty()) {
        fwrite(files.data(), sizeof(DirectoryEntry), files.size(), fp);
    }
    
    fclose(fp);
}

bool file3dsGetFiles(std::vector<DirectoryEntry>& files) {
    currentDirRomCount = 0;
    files.clear();

    // only reserve if we are below our "minimum baseline". 
    if (files.capacity() < intialFileCapacity) {
        files.reserve(intialFileCapacity);
    }

    FS_Archive sdmcArchive;
    if (R_FAILED(FSUSER_OpenArchive(&sdmcArchive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) {
        return false;
    }

    TickCounter timer;
    osTickCounterStart(&timer);

    // strip "sdmc:" prefix
    const char* nativePath = currentDir;
    if (strncmp(nativePath, "sdmc:", 5) == 0) nativePath += 5;
    if (nativePath[0] == '\0') nativePath = "/";

    u64 currentTimestamp = file3dsGetDirectoryTimestamp(sdmcArchive, nativePath);
    
    char cachePath[PATH_MAX];
    snprintf(cachePath, sizeof(cachePath), "%s%s.snes9x3ds_dir_cache", currentDir, nativePath[strlen(nativePath)-1] == '/' ? "" : "/");

    if (file3dsLoadFromCache(files, cachePath, currentTimestamp)) {
        FSUSER_CloseArchive(sdmcArchive);

        osTickCounterUpdate(&timer);
        log3dsWrite("[file3dsGetFiles] %d files loaded from cache in %.3fms", files.size(), osTickCounterRead(&timer));

        return true;
    }

    // slow path
    osTickCounterStart(&timer);

    Handle dirHandle;
    FS_Path dirPath = fsMakePath(PATH_ASCII, nativePath);
    if (R_FAILED(FSUSER_OpenDirectory(&dirHandle, sdmcArchive, dirPath))) {
        FSUSER_CloseArchive(sdmcArchive);
        
        return false;
    }

    if (strlen(nativePath) > 1) {
        files.emplace_back(PARENT_DIRECTORY_LABEL, FileEntryType::ParentDirectory);
    }

    static const u32 DIR_READ_BATCH_SIZE = 32;
    FS_DirectoryEntry entries[DIR_READ_BATCH_SIZE];
    u32 entriesRead = 0;
    char nameBuffer[NAME_MAX + 1];

    while (true) {
        if (R_FAILED(FSDIR_Read(dirHandle, &entriesRead, DIR_READ_BATCH_SIZE, entries)) || entriesRead == 0) break;

        for (u32 i = 0; i < entriesRead; i++) {
            file3dsConvertUtf16ToChar(entries[i].name, nameBuffer, sizeof(nameBuffer));

            if (nameBuffer[0] == '\0' || nameBuffer[0] == '.') continue;

            if (entries[i].attributes & FS_ATTRIBUTE_DIRECTORY) {
                files.emplace_back(nameBuffer, FileEntryType::ChildDirectory);
            } else {
                if (file3dsIsValidFilename(nameBuffer)) {
                    files.emplace_back(nameBuffer, FileEntryType::File);
                    currentDirRomCount++;
                }
            }
        }
    }

    FSDIR_Close(dirHandle);
    FSUSER_CloseArchive(sdmcArchive);

    std::sort(files.begin(), files.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
    if (a.Type != b.Type) return a.Type < b.Type;
        return strcasecmp(a.Filename, b.Filename) < 0;
    });

    osTickCounterUpdate(&timer);
    log3dsWrite("[file3dsGetFiles] %s: %d files prepared in %.3fms", currentDir, files.size(), osTickCounterRead(&timer));

    if (files.size() >= DIRECTORY_CACHE_THRESHOLD) {
        osTickCounterStart(&timer);
        file3dsSaveFilesToCache(files, cachePath, currentTimestamp);
        osTickCounterUpdate(&timer);
        log3dsWrite("[file3dsGetFiles] %s/.dir_cache created in %.3fms", cachePath, osTickCounterRead(&timer));
    }

    return true;
}

bool file3dsIsValidFilename(const char* filename) {
    if (!filename || filename[0] == '.') return false;

    const char* dot = strrchr(filename, '.');
    if (!dot || dot[1] == '\0') return false;

    for (int i = 0; i < validExtensionsCount; i++) {
        if (strcasecmp(dot, validExtensions[i]) == 0) {
            return true;
        }
    }

    return false;
}

void file3dsGetBasename(const char* path, char* output, u32 bufferSize, bool keepExtension) {
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

void file3dsGetTrimmedBasename(const char* path, char* output, u32 bufferSize, bool keepExtension) {
    if (!path || !output || bufferSize == 0) return;

    file3dsGetBasename(path, output, bufferSize, true);

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

void file3dsGetRelatedPath(const char* path, char* output, u32 bufferSize, const char* ext, const char* targetDir, bool trimmed) {
    if (!path || !output || bufferSize == 0) {
        if (bufferSize > 0) output[0] = '\0';
        return;
    }

    char basename[NAME_MAX + 1];

    if (trimmed) {
        // e.g. "Kirby Super Star (USA) (V1.2) [!].sfc" -> "Kirby Super Star"
        file3dsGetTrimmedBasename(path, basename, sizeof(basename), false);

        // if filename is part of romNameMappings use its associated value instead
        // (e.g. "Kirby's Fun Pak" is looking for "Kirby Super Star")
        auto it = romNameMappings.find(std::string(basename));
        if (it != romNameMappings.end()) {
            snprintf(basename, sizeof(basename), "%s", it->second.c_str());
        }
    } else {
        // e.g. "Kirby Super Star (USA) (V1.2) [!].sfc" -> "Kirby Super Star (USA) (V1.2) [!]"
        file3dsGetBasename(path, basename, sizeof(basename), false);
    }

    const char* extension = (ext) ? ext : "";

    if (targetDir) {
        // (e.g. sdmc:/3ds/snes9x_3ds/borders/Super Mario World.png)
        snprintf(output, bufferSize, "%s/%s/%s%s", settings3DS.RootDir, targetDir, basename, extension);
    } 
    else {
        // we need the directory from 'filename'
        const char* slash = strrchr(path, '/');
        
        if (slash) {
            // calculate the length of the directory part
            int dirLen = (int)(slash - path);
            
            // "%.*s" prints exactly 'dirLen' characters from 'filename'
            snprintf(output, bufferSize, "%.*s/%s%s", dirLen, path, basename, extension);
        } 
        else {
            // fallback: No slash found (filename is just "game.sfc")
            snprintf(output, bufferSize, "%s%s", basename, extension);
        }
    }
}

void file3dsDeleteDirCache(const char* dir) {
    if (dir == nullptr || dir[0] == '\0') return;

    char cachePath[PATH_MAX];
    size_t len = strlen(dir);

    // construct the full path: dir + "/" (if missing) + ".snes9x3ds_dir_cache"
    snprintf(cachePath, sizeof(cachePath), "%s%s.snes9x3ds_dir_cache", 
             dir, 
             (dir[len - 1] == '/') ? "" : "/");

    // remove() returns 0 on success
    if (remove(cachePath) == 0) {
        log3dsWrite("Deleted cache: %s", cachePath);
    } else {
        // log3dsWrite("Failed to delete cache (or didn't exist): %s", cachePath);
    }
}