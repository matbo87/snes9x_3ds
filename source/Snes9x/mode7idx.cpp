#include "snes9x.h"
#include "memmap.h"
#include "mode7idx.h"

Mode7Idx sMode7Idx;

void Mode7IdxInvalidate()
{
    sMode7Idx.valid = false;
}

void Mode7IdxRebuild()
{
    for (int c = 0; c < 256; c++)
        sMode7Idx.head[c] = MODE7IDX_NIL;

    // Tilemap slot i lives at Memory.VRAM[i * 2] (Mode 7 region uses
    // even VRAM bytes for tile numbers; odd bytes hold chargen pixel
    // data). Order within each per-char list is unspecified.
    for (int i = 0; i < 16384; i++)
    {
        uint8 c = Memory.VRAM[i * 2];
        uint16 h = sMode7Idx.head[c];
        sMode7Idx.next[i] = h;
        sMode7Idx.prev[i] = MODE7IDX_NIL;
        if (h != MODE7IDX_NIL) sMode7Idx.prev[h] = (uint16)i;
        sMode7Idx.head[c] = (uint16)i;
    }
    sMode7Idx.valid = true;
}
