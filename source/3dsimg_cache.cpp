#include "3dsimg_cache.h"

#include <cstring>

bool imgCacheRead(FILE* file, const ImageCacheEntry* index, u32 count, u32 key,
                  u16* buf, size_t bufSize, u16* widthOut, u16* heightOut)
{
    if (!file || !index || !buf)
        return false;

    const ImageCacheEntry* e = nullptr;
    for (u32 i = 0; i < count; i++) {
        if (index[i].key == key) { e = &index[i]; break; }
    }
    if (!e)
        return false;

    size_t payloadSize = (size_t)e->width * e->height * sizeof(u16);
    if (payloadSize == 0 || payloadSize > bufSize)
        return false;

    if (fseek(file, (long)e->offset, SEEK_SET) != 0)
        return false;
    if (fread(buf, payloadSize, 1, file) != 1)
        return false;

    *widthOut = e->width;
    *heightOut = e->height;
    return true;
}

u32 imgCacheReadIndex(FILE* file, ImageCacheEntry* table, u32 maxCount,
                      u16 maxWidth, u16 maxHeight, ImageCacheHeader* headerOut)
{
    if (!file || !table)
        return 0;

    ImageCacheHeader h = {};
    if (fread(h.magic, 1, 4, file) != 4)
        return 0;

    bool legacyUniform = memcmp(h.magic, "IMGZ", 4) == 0;
    if (legacyUniform) {
        if (fread(&h.count, 4, 1, file) != 1 ||
            fread(&h.width, 2, 1, file) != 1 ||
            fread(&h.height, 2, 1, file) != 1)
            return 0;
        h.expectedCount = h.count;
    } else {
        if (fread(&h._padding, 16, 1, file) != 1)
            return 0;
    }

    if (h.width > maxWidth || h.height > maxHeight || h.count == 0 || h.count > maxCount)
        return 0;

    size_t headerSize  = legacyUniform ? 12 : 20;
    size_t entryStride = legacyUniform ? 8 : sizeof(ImageCacheEntry);
    if (fseek(file, 0, SEEK_END) != 0)
        return 0;
    long size = ftell(file);
    if (size < (long)(headerSize + (size_t)h.count * entryStride))
        return 0;

    if (fseek(file, (long)headerSize, SEEK_SET) != 0)
        return 0;
    if (legacyUniform) {
        for (u32 i = 0; i < h.count; i++) {
            u32 pair[2];
            if (fread(pair, sizeof(u32), 2, file) != 2)
                return 0;
            table[i].key    = pair[0];
            table[i].offset = pair[1];
            table[i].width  = h.width;
            table[i].height = h.height;
        }
    } else if (fread(table, sizeof(ImageCacheEntry), h.count, file) != h.count) {
        return 0;
    }

    if (headerOut)
        *headerOut = h;
    return h.count;
}
