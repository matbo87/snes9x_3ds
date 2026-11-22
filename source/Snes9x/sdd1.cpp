#include "copyright.h"


#include "snes9x.h"
#include "memmap.h"
#include "ppu.h"
#include "sdd1.h"
#include "sdd1emu.h"
#include "3dsimpl.h"

void S9xSetSDD1MemoryMap (uint32 bank, uint32 value)
{
    bank = 0xc00 + bank * 0x100;
    value = value * 1024 * 1024;

    for (int c = 0; c < 0x100; c += 16)
    {
        uint8 *block = &Memory.ROM [value + (c << 12)];
        for (int i = c; i < c + 16; i++)
            Memory.Map [i + bank] = block;
    }
}

void S9xResetSDD1 ()
{
    memset (&Memory.FillRAM [0x4800], 0, 4);
    for (int i = 0; i < 4; i++)
    {
        Memory.FillRAM [0x4804 + i] = i;
        S9xSetSDD1MemoryMap (i, i);
    }
}

void S9xSDD1PostLoadState ()
{
    for (int i = 0; i < 4; i++)
	    S9xSetSDD1MemoryMap (i, Memory.FillRAM [0x4804 + i]);
}

#define SDD1_CACHE_BYTES (256 * 1024)
#define SDD1_CACHE_SLOTS 1024

struct SDD1CacheEntry {
    const uint8 *src;
    uint32 len;
    uint32 offset;
};

static struct {
    uint8 *pool;
    bool   allocFailed;
    uint32 romCrc;
    uint32 used, entries;
    SDD1CacheEntry slots[SDD1_CACHE_SLOTS];
} SDD1Cache;

static void S9xSDD1CacheClear ()
{
    memset (SDD1Cache.slots, 0, sizeof (SDD1Cache.slots));
    SDD1Cache.used = 0;
    SDD1Cache.entries = 0;
}

uint8 *S9xSDD1Decompress (uint8 *in, uint32 len, uint8 *fallback)
{
    // len 0 means a 64 KB DMA; it does not fit, nor do blocks the pool cannot hold.
    if (len == 0 || len > SDD1_CACHE_BYTES || SDD1Cache.allocFailed)
    {
        SDD1_decompress (fallback, in, len);
        return fallback;
    }

    if (!SDD1Cache.pool)
    {
        SDD1Cache.pool = (uint8 *) malloc (SDD1_CACHE_BYTES);
        if (!SDD1Cache.pool)
        {
            SDD1Cache.allocFailed = true;
            SDD1_decompress (fallback, in, len);
            return fallback;
        }
    }

    // Source pointers point into Memory.ROM, which a new ROM reuses.
    if (SDD1Cache.romCrc != Memory.ROMCRC32)
    {
        S9xSDD1CacheClear ();
        SDD1Cache.romCrc = Memory.ROMCRC32;
    }

    const uint32 h = (((uint32) (uintptr_t) in) ^ (len * 2654435761u)) * 2654435761u;
    uint32 slot = h & (SDD1_CACHE_SLOTS - 1);
    while (SDD1Cache.slots[slot].src)
    {
        const SDD1CacheEntry &e = SDD1Cache.slots[slot];
        if (e.src == in && e.len == len)
            return SDD1Cache.pool + e.offset;
        slot = (slot + 1) & (SDD1_CACHE_SLOTS - 1);
    }

    // Wipe at 3/4 load so a miss always reaches an empty slot.
    if (SDD1Cache.used + len > SDD1_CACHE_BYTES || SDD1Cache.entries >= SDD1_CACHE_SLOTS * 3 / 4)
    {
        S9xSDD1CacheClear ();
        slot = h & (SDD1_CACHE_SLOTS - 1);
    }

    uint8 *out = SDD1Cache.pool + SDD1Cache.used;
    SDD1_decompress (out, in, len);
    SDD1Cache.slots[slot].src = in;
    SDD1Cache.slots[slot].len = len;
    SDD1Cache.slots[slot].offset = SDD1Cache.used;
    SDD1Cache.used += len;
    SDD1Cache.entries++;
    return out;
}

static int __attribute__((unused)) S9xCompareSDD1LoggedDataEntries (const void *p1, const void *p2)
{
    uint8 *b1 = (uint8 *) p1;
    uint8 *b2 = (uint8 *) p2;
    uint32 a1 = (*b1 << 16) + (*(b1 + 1) << 8) + *(b1 + 2);
    uint32 a2 = (*b2 << 16) + (*(b2 + 1) << 8) + *(b2 + 2);

    return (a1 - a2);
}
