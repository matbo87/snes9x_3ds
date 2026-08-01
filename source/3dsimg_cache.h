#ifndef _3DSIMG_CACHE_H_
#define _3DSIMG_CACHE_H_

#include <3ds.h>
#include <cstdio>

// Shared on-disk format for pre-swizzled RGB565 image caches:
//   "TMB1" - thumbnail caches (gameplay/title/boxart), built offline
//   "RAB1" - RetroAchievements badge caches, downloaded at runtime
// The 4th magic char is the format version.
//
// Legacy thumbnail caches ("IMGZ") predate this: a 12-byte header (magic, count,
// width, height) with 8-byte uniform-dimension entries. Still read, never written.

typedef struct {
    char magic[4];       // "TMB1" / "RAB1" (family + version char); legacy thumbs "IMGZ"
    u16  _padding;
    u16  flags;          // IMG_CACHE_FLAG_*; complete sets = 0
    u32  count;          // entries present in this file
    u32  expectedCount;  // target entry count; == count when complete
    u16  width;          // max width across entries
    u16  height;         // max height across entries
} ImageCacheHeader;

// Swizzled RGB565 payload of width*height*2 bytes at offset.
typedef struct {
    u32 key;             // DJB2 filename hash (thumbs) / badge key (RA)
    u32 offset;          // byte offset to the pixel payload
    u16 width;
    u16 height;
} ImageCacheEntry;

#define IMG_CACHE_FLAG_INCOMPLETE  0x0001u

// Resolves `key` in index[0..count): reads its swizzled RGB565 payload into buf
// (bounded by bufSize) and reports the dimensions. Returns false on miss,
// oversize, or short read; buf and *widthOut/*heightOut are untouched on failure.
bool imgCacheRead(FILE* file, const ImageCacheEntry* index, u32 count, u32 key,
                  u16* buf, size_t bufSize, u16* widthOut, u16* heightOut);

// Reads and validates the header, then loads table[0..maxCount). Only "IMGZ" is
// special-cased here; callers verify the current-format family via headerOut.
// Returns the entry count, or 0 on failure.
u32 imgCacheReadIndex(FILE* file, ImageCacheEntry* table, u32 maxCount,
                      u16 maxWidth, u16 maxHeight, ImageCacheHeader* headerOut);

// On-disk layout is fixed; guard against accidental padding.
static_assert(sizeof(ImageCacheHeader) == 20, "ImageCacheHeader must be 20 bytes");
static_assert(sizeof(ImageCacheEntry)  == 12, "ImageCacheEntry must be 12 bytes");

#endif
