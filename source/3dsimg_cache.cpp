#include "3dsimg_cache.h"

#include <cstdlib>
#include <cstring>

bool imgCacheAlloc(ImageCacheReader* r, u32 maxCount, size_t pixelBufferSize)
{
    if (!r)
        return false;

    memset(r, 0, sizeof(*r));
    r->currentKey = IMG_CACHE_KEY_NONE;

    r->index  = (ImageCacheEntry*)malloc((size_t)maxCount * sizeof(ImageCacheEntry));
    r->pixels = (u16*)linearAlloc(pixelBufferSize);

    if (!r->index || !r->pixels) {
        free(r->index);
        if (r->pixels) linearFree(r->pixels);
        memset(r, 0, sizeof(*r));
        r->currentKey = IMG_CACHE_KEY_NONE;
        return false;
    }

    r->maxCount        = maxCount;
    r->pixelBufferSize = pixelBufferSize;
    return true;
}

void imgCacheClose(ImageCacheReader* r)
{
    if (!r)
        return;

    if (r->file) {
        fclose(r->file);
        r->file = NULL;
    }
    r->count = 0;
    imgCacheInvalidate(r);
}

void imgCacheFree(ImageCacheReader* r)
{
    if (!r)
        return;

    imgCacheClose(r);
    free(r->index);
    r->index = NULL;
    if (r->pixels) {
        linearFree(r->pixels);
        r->pixels = NULL;
    }
    r->maxCount        = 0;
    r->pixelBufferSize = 0;
}

bool imgCacheLoad(ImageCacheReader* r, u32 key)
{
    if (!r)
        return false;

    if (key == r->currentKey)
        return r->currentValid;

    r->currentKey   = key;
    r->currentValid = imgCacheRead(r->file, r->index, r->count, key, r->pixels,
                                   r->pixelBufferSize, &r->currentWidth, &r->currentHeight);
    return r->currentValid;
}

void imgCacheSetCurrent(ImageCacheReader* r, u32 key, u16 width, u16 height)
{
    if (!r)
        return;

    r->currentKey    = key;
    r->currentWidth  = width;
    r->currentHeight = height;
    r->currentValid  = true;
}

void imgCacheInvalidate(ImageCacheReader* r)
{
    if (!r)
        return;

    r->currentKey   = IMG_CACHE_KEY_NONE;
    r->currentValid = false;
}

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
