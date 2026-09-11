#ifndef _3DSPIXEL_UTILS_H
#define _3DSPIXEL_UTILS_H

#include <3ds.h>


// RGBA8 little-endian (byte0=R) -> RGB565.
inline u16 __attribute__((always_inline)) rgba8ToRgb565(u32 p) {
    return ((p & 0xF8) << 8) | ((p & 0xFC00) >> 5) | ((p & 0xF80000) >> 19);
}

// Box downscale of a packed-RGBA image at an exact 3:2 ratio. 
// srcW/srcH must be multiples of 3
void boxDownscale3to2Rgba(const u32* src, int srcW, int srcH, u32* dst, int dstW);

#endif // _3DSPIXEL_UTILS_H
