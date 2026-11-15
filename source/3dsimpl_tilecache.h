#ifndef _3DSIMPL_TILECACHE_H_
#define _3DSIMPL_TILECACHE_H_

#include "3dsgpu.h"
#include "3dsimpl_gpu.h"


//---------------------------------------------------------
// Initializes the Hash to Texture Position look-up (and
// the reverse look-up table as well)
//---------------------------------------------------------
void cache3dsInit();


//---------------------------------------------------------
// Takes a texture pair for a new tile; skips pairs this frame still draws from.
//---------------------------------------------------------
int cache3dsAllocTexturePosition(int tileAddr, int pal);


//---------------------------------------------------------
// Computes and returns the texture position (Non-Mode 7) 
// given the tile number and 
//---------------------------------------------------------
inline int cache3dsGetTexturePositionFast(int tileAddr, int pal)
{
    int hash = COMPOSE_HASH(tileAddr, pal);
    int pos = GPU3DSExt.vramCacheHashToTexturePosition[hash];

    if (pos == 0)
        pos = cache3dsAllocTexturePosition(tileAddr, pal);

    // The GPU reads this frame's vertices only after the frame ends.
    // The allocator must not hand this pair to another tile before then.
    GPU3DSExt.vramCachePairSubmission[pos >> 1] = gpu3dsSubmissionCount();

    return pos;
}


//---------------------------------------------------------
// Swaps the texture position for alternate frames. This
// is required to fix sprite flickering problems in games
// that updates the tile bitmaps mid-frame (for eg. in 
// DKC2)
//
// Two slots cover two decodes per frame; a third reuses texels
// that can still be referenced by the GPU (not seen in practice).
//---------------------------------------------------------
inline int cacheGetSwapTexturePositionForAltFrameFast(int tileAddr, int pal)
{
    int hash = COMPOSE_HASH(tileAddr, pal);
    int pos = GPU3DSExt.vramCacheHashToTexturePosition[hash] ^ 1;
    GPU3DSExt.vramCacheHashToTexturePosition[hash] = pos;
    return pos;
}


//---------------------------------------------------------
// Second bank for each tile cache, so decoding can continue
// while the GPU still samples the submitted one.
//---------------------------------------------------------
bool cache3dsAllocTileCacheBanks();
void cache3dsDeallocTileCacheBanks();


//---------------------------------------------------------
// Converts the tile in SNES bitplane format into 
// it's 5551 16-bit representation in 3DS texture format.
//---------------------------------------------------------
void cache3dsCacheSnesTileToTexturePosition(
    uint8 *snesTilePixels,
	uint16 *snesPalette,
    uint16 texturePosition);


//---------------------------------------------------------
// Converts the tile in SNES bitplane format into 
// it's 5551 16-bit representation in 3DS texture format.
//
// This is meant for the mode 7 tiles.
//---------------------------------------------------------
void cache3dsCacheSnesTileToMode7TexturePosition(
    uint8 *snesTilePixels,
	uint16 *snesPalette,
    uint16 texturePosition,
    uint32 *paletteMask);



#endif
