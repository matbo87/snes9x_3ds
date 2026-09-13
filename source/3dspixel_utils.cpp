#include "3dspixel_utils.h"

// Per-lane byte averages of packed RGBA words. Each channel is shifted into a
// 0x00FF00FF layout before summing so no lane can carry into the next.
static inline u32 avg2Rgba(u32 a, u32 b)
{
    u32 rb = (a & 0x00FF00FF) + (b & 0x00FF00FF);
    u32 ga = ((a >> 8) & 0x00FF00FF) + ((b >> 8) & 0x00FF00FF);

    return ((rb >> 1) & 0x00FF00FF) |
           (((ga >> 1) & 0x00FF00FF) << 8);
}

static inline u32 avg4Rgba(u32 a, u32 b, u32 c, u32 d)
{
    u32 rb = (a & 0x00FF00FF) + (b & 0x00FF00FF) +
             (c & 0x00FF00FF) + (d & 0x00FF00FF);
    u32 ga = ((a >> 8) & 0x00FF00FF) + ((b >> 8) & 0x00FF00FF) +
             ((c >> 8) & 0x00FF00FF) + ((d >> 8) & 0x00FF00FF);

    return ((rb >> 2) & 0x00FF00FF) |
           (((ga >> 2) & 0x00FF00FF) << 8);
}

void boxDownscale3to2Rgba(const u32* src, int srcW, int srcH, u32* dst, int dstW)
{
    const u32 *s = src;
    u32 *d = dst;
    for(int sy = 0; sy + 3 <= srcH; sy += 3) {
        const u32 *s0 = s;
        const u32 *s1 = s + srcW;
        const u32 *s2 = s + srcW * 2;
        u32 *d0 = d;
        u32 *d1 = d + dstW;

        for(int sx = 0; sx + 3 <= srcW; sx += 3) {
            *d0++ = s0[0];
            *d0++ = avg2Rgba(s0[1], s0[2]);
            *d1++ = avg2Rgba(s1[0], s2[0]);
            *d1++ = avg4Rgba(s1[1], s1[2], s2[1], s2[2]);
            s0 += 3; s1 += 3; s2 += 3;
        }

        s += srcW * 3;
        d += dstW * 2;
    }
}
