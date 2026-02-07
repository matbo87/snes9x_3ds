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
static std::vector<std::string> availableThumbnailTypes;

static char currentDir[_MAX_PATH] = "";
static unsigned short currentDirRomCount = 0;

// covers the largest possible UI texture (512x256 RGBA8)
// and should be large enough for snes9x save states
size_t g_fileBufferSize = 512 * 256 * 4; // 512kb
u8* g_fileBuffer = NULL;

// aligned stream buffer for optimized DMA/Cache performance
u8 g_streamBuffer[CACHE_LINE_SIZE * 1024] __attribute__((aligned(CACHE_LINE_SIZE)));

inline std::string operator "" s(const char* s, size_t length) {
    return std::string(s, length);
}

//----------------------------------------------------------------------
// Initialize the library
//----------------------------------------------------------------------
void file3dsInitialize(void)
{
    log3dsWrite("check for required directories:");
    std::vector<std::string> directories = {"", "configs", "saves", "savestates", "screenshots"};
    for (const std::string& dir : directories) {
        static char reqDir[_MAX_PATH];
        snprintf(reqDir, sizeof(reqDir), "%s/%s", settings3DS.RootDir, dir.c_str());

        DIR* d = opendir(reqDir);
        if (d) {
            closedir(d);
            log3dsWrite("%s v", reqDir);
        } else {
            mkdir(reqDir, 0777);
            log3dsWrite("%s x (created)", reqDir);
        }
    }

    std::vector<std::string> thumbnailTypes = { "boxart", "title", "gameplay" };
    availableThumbnailTypes.reserve(thumbnailTypes.size());

    log3dsWrite("check for available thumbnail types:");
    for (const std::string& f : thumbnailTypes) {
        static char cacheFile[_MAX_PATH];
        snprintf(cacheFile, sizeof(cacheFile), "%s/%s/%s.cache", settings3DS.RootDir, "thumbnails", f.c_str());

        if (IsFileExists(cacheFile)) {
            availableThumbnailTypes.emplace_back(f);
            log3dsWrite("%s v  ", cacheFile);
        } else {
            log3dsWrite("%s x  ", cacheFile);
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

//----------------------------------------------------------------------
// Checks if file exists.
//----------------------------------------------------------------------
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
    availableThumbnailTypes.clear();
    romNameMappings.clear();
    DUMP_UNORDERED_MAP_INFO("romNameMappings after cleanup", romNameMappings);
}

bool file3dsthumbnailsAvailable(const char* type) {
    return std::find(availableThumbnailTypes.begin(), 
        availableThumbnailTypes.end(), 
        type) != availableThumbnailTypes.end();
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


// will return empty string if its root directory
std::string file3dsGetCurrentDirName() {
    std::string path = std::string(currentDir);
    std::string dirName = "";
    // Find the position of the last slash
    size_t lastSlashPos = path.rfind('/');

    // Check if a slash was found
    if (lastSlashPos != std::string::npos) {
        // Find the position of the second-to-last slash
        size_t secondLastSlashPos = path.rfind('/', lastSlashPos - 1);

        if (secondLastSlashPos != std::string::npos) {
            // Extract the substring between the two last slashes
            dirName = path.substr(secondLastSlashPos + 1, lastSlashPos - secondLastSlashPos - 1);
        }
    }

    return dirName;
}

//----------------------------------------------------------------------
// Gets the current directory.
//----------------------------------------------------------------------
char *file3dsGetCurrentDir(void)
{
    return currentDir;
}


//----------------------------------------------------------------------
// Gets total number of roms in current directory
//----------------------------------------------------------------------
unsigned short file3dsGetCurrentDirRomCount(void)
{
    return currentDirRomCount;
}


//----------------------------------------------------------------------
// Go up or down a level.
//----------------------------------------------------------------------
void file3dsGoUpOrDownDirectory(const DirectoryEntry& entry) {
    if (entry.Type == FileEntryType::ParentDirectory) {
        file3dsGoToParentDirectory();
    } else if (entry.Type == FileEntryType::ChildDirectory) {
        file3dsGoToChildDirectory(entry.Filename);
    }
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
// Go up to the child directory.
//----------------------------------------------------------------------
void file3dsGoToChildDirectory(const char* childDir)
{
    size_t len = strlen(currentDir);
    snprintf(currentDir + len, sizeof(currentDir) - len, "%s/", childDir);
}

u64 file3dsGetDirectoryTimestamp(FS_Archive archive, const char* path) {
    u64 timestamp = 0;
    u16 pathUtf16[_MAX_PATH];
    ssize_t units = utf8_to_utf16(pathUtf16, (const uint8_t*)path, _MAX_PATH - 1);
    
    if (units < 0) return 0;

    pathUtf16[units] = 0;

    Result res = FSUSER_ControlArchive(
        archive, ARCHIVE_ACTION_GET_TIMESTAMP, 
        pathUtf16, (units + 1) * sizeof(u16), 
        &timestamp, sizeof(timestamp));

    if (R_FAILED(res)) return 0;
    
    return timestamp;
}

void file3dsConvertUtf16ToChar(const u16* nameUtf16, char* output, size_t maxLen) {
    const u16* p = nameUtf16;
    size_t i = 0;

    while (*p && i < maxLen - 1) {
        u16 c = *p++;
        if (c < 0x80) {
            output[i++] = static_cast<char>(c); // fast ascii
        } else {
            ssize_t units = utf16_to_utf8((uint8_t*)output, nameUtf16, maxLen - 1);
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

//----------------------------------------------------------------------
// Fetch all file names
//----------------------------------------------------------------------
bool file3dsGetFiles(std::vector<DirectoryEntry>& files) {
    const std::vector<std::string>& extensions = file3dsGetValidRomExtensions();
    
    currentDirRomCount = 0;
    files.clear();

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
    
    char cachePath[_MAX_PATH];
    snprintf(cachePath, sizeof(cachePath), "%s%s.snes9x3ds_dir_cache", currentDir, nativePath[strlen(nativePath)-1] == '/' ? "" : "/");

    if (file3dsLoadFromCache(files, cachePath, currentTimestamp)) {
        FSUSER_CloseArchive(sdmcArchive);

        osTickCounterUpdate(&timer);
        log3dsWrite("[file3dsGetFiles] %d files loaded from cache in %.3fms", files.size(), osTickCounterRead(&timer));

        return true;
    }

    // slow path
    osTickCounterStart(&timer);

    files.reserve(1024); // prevent initial vector reallocations

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
    char nameBuffer[512];

    while (true) {
        if (R_FAILED(FSDIR_Read(dirHandle, &entriesRead, DIR_READ_BATCH_SIZE, entries)) || entriesRead == 0) break;

        for (u32 i = 0; i < entriesRead; i++) {
            file3dsConvertUtf16ToChar(entries[i].name, nameBuffer, sizeof(nameBuffer));

            if (nameBuffer[0] == '\0' || nameBuffer[0] == '.') continue;

            if (entries[i].attributes & FS_ATTRIBUTE_DIRECTORY) {
                files.emplace_back(nameBuffer, FileEntryType::ChildDirectory);
            } else {
                if (file3dsIsValidFilename(nameBuffer, extensions)) {
                    files.emplace_back(nameBuffer, FileEntryType::File);
                    currentDirRomCount++;
                }
            }
        }
    }

    FSDIR_Close(dirHandle);
    FSUSER_CloseArchive(sdmcArchive);
    
    files.shrink_to_fit();

    std::sort(files.begin(), files.end(), [](const DirectoryEntry& a, const DirectoryEntry& b) {
    if (a.Type != b.Type) return a.Type < b.Type;
        return strcasecmp(a.Filename, b.Filename) < 0;
    });

    osTickCounterUpdate(&timer);
    log3dsWrite("[file3dsGetFiles] %s: %d files prepared in %.3fms", cachePath, files.size(), osTickCounterRead(&timer));

    if (files.size() >= DIRECTORY_CACHE_THRESHOLD) {
        osTickCounterStart(&timer);
        file3dsSaveFilesToCache(files, cachePath, currentTimestamp);
        osTickCounterUpdate(&timer);
        log3dsWrite("[file3dsGetFiles] %s/.dir_cache created in %.3fms", cachePath, osTickCounterRead(&timer));
    }

    return true;
}

bool file3dsIsValidFilename(const char* filename, const std::vector<std::string>& extensions) {
    if (filename[0] == '.') return false;

    // find the last dot
    const char* dot = strrchr(filename, '.');
    if (!dot || dot[1] == '\0') return false;

    // compare extension against the list
    for (const auto& ext : extensions) {
        if (strcasecmp(dot, ext.c_str()) == 0) return true;
    }

    return false;
}

std::string file3dsGetFileBasename(const char* filename, bool ext) {
    std::string basename(filename);

    size_t start = basename.find_last_of("/\\");
    size_t end = ext ? basename.size() : basename.find_last_of(".");
    
    if (start != std::string::npos && end != std::string::npos && end > start) {
        basename = basename.substr(start + 1, end - start - 1);
    } else {
        basename = basename.substr(start + 1, end);
    }

    return basename;
}

std::string file3dsGetTrimmedFileBasename(const char* filename, bool ext) {
    std::string basename = file3dsGetFileBasename(filename, ext);

    // remove everything after the actual filename
    // e.g. "Donkey Kong Country   (USA) (V1.2) [!]" -> "Donkey Kong Country"
    std::size_t startPos = basename.find_first_of("([");
    if (startPos != std::string::npos) {
        std:: string extension;

        // quite messy but at least ensures that invalid filenames wouldn't cause a crash
        if (!ext)
            extension = "";
        else {
            size_t dotIndex = basename.find_last_of('.');
            if (dotIndex != std::string::npos && dotIndex < basename.size() - 1) {
                extension = basename.substr(dotIndex);
            }
        }
        
        // remove whitespace
        std::size_t endPos = basename.find_last_not_of(" \t\n\r\f\v", startPos - 1);
        return (basename.substr(0, endPos + 1) + extension);
    }

    return basename;
}

// get the associated filename of the current game (e.g. savestate, config, border, etc.)
std::string file3dsGetAssociatedFilename(const char* filename, const char* ext, const char* targetDir, bool trimmed) {
    if (filename == nullptr || filename[0] == '\0') {
        return "";
    }

    std::string associatedFilename;
    std::string basename = trimmed ? file3dsGetTrimmedFileBasename(filename, false) : file3dsGetFileBasename(filename, false);

    if (strcmp(ext, ".png") == 0 || strcmp(ext, ".chx") == 0 || strcmp(ext, ".cht") == 0) {
        // if filename is part of romNameMappings use its associated value instead
        // (e.g. "Mega Man & Bass.png" would look for "Rockman & Forte.png")
        auto it = romNameMappings.find(basename);

        if (it != romNameMappings.end()) {
            basename = it->second;
        }
    }

    std::string extension = ext != nullptr ? std::string(ext) : "";
    
    if (targetDir != nullptr) {
        associatedFilename = std::string(settings3DS.RootDir) + "/" + std::string(targetDir) + "/" + basename + extension;

        return associatedFilename;
    }

    // if targetDir is undefined, use current game directory
    std::string dir = std::string(filename);
    size_t lastSlashPos = dir.find_last_of('/');
    if (lastSlashPos != std::string::npos) {
        associatedFilename = dir.substr(0, lastSlashPos) + "/" + basename + extension;
    } else {
        associatedFilename = basename + extension;
    }

    return associatedFilename;
}

const std::vector<std::string>& file3dsGetValidRomExtensions() {
    static std::vector<std::string> extensions;
    if (extensions.empty()) {
        extensions.reserve(3);
        extensions.push_back(".smc");
        extensions.push_back(".sfc");
        extensions.push_back(".fig");
    }
    return extensions;
}