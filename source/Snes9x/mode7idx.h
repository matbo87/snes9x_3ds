#ifndef _MODE7IDX_H_
#define _MODE7IDX_H_

#include "snes9x.h"

// Reverse index: char number -> intrusive linked list of tilemap-entry
// indices that reference it. Lets the per-frame Mode 7 dirty-char processing
// in S9xPrepareMode7CheckAndUpdateCharTiles iterate only the slots that
// actually use a dirty char, instead of scanning all 16384 tilemap entries.
//
// Invariant when sMode7Idx.valid is true: for every i in [0, 16384),
// the tilemap slot i is a member of the linked list rooted at
// sMode7Idx.head[Memory.VRAM[i * 2]].

#define MODE7IDX_NIL 0xFFFFu

struct Mode7Idx
{
    uint16 head[256];
    uint16 next[16384];
    uint16 prev[16384];
    bool   valid;
};

extern Mode7Idx sMode7Idx;

// Mark the index stale. Call from any path that bulk-replaces VRAM
// (cpu reset, state load, ROM load). Next read rebuilds from VRAM.
void Mode7IdxInvalidate();

// Build the index from the current Memory.VRAM tilemap. Idempotent.
void Mode7IdxRebuild();

// Per-write maintenance, O(1) each. Safe to call when sMode7Idx.valid
// is false (no-op): the next rebuild reads Memory.VRAM directly.

static inline void Mode7IdxRemove(uint16 i, uint8 c)
{
    if (!sMode7Idx.valid) return;
    uint16 p = sMode7Idx.prev[i];
    uint16 n = sMode7Idx.next[i];
    if (p == MODE7IDX_NIL) sMode7Idx.head[c] = n;
    else                   sMode7Idx.next[p] = n;
    if (n != MODE7IDX_NIL) sMode7Idx.prev[n] = p;
}

static inline void Mode7IdxInsertHead(uint16 i, uint8 c)
{
    if (!sMode7Idx.valid) return;
    uint16 h = sMode7Idx.head[c];
    sMode7Idx.next[i] = h;
    sMode7Idx.prev[i] = MODE7IDX_NIL;
    if (h != MODE7IDX_NIL) sMode7Idx.prev[h] = i;
    sMode7Idx.head[c] = i;
}

#endif
