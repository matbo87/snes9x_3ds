
#include "3dsimpl.h"
#include "3dsimpl_tilecache.h"


#define MODE7_TILE_COUNT         256
#define TILE_CACHE_TILE_BYTES    128     // one 8x8 character; a 16x16 BG tile uses four

// Second copy of each tile cache, so decoding can continue while the GPU
// still uses the submitted copy.
static void *snesTileCacheBanks[2] = { NULL, NULL };
static void *mode7TileCacheBanks[2] = { NULL, NULL };
static int   activeTileCacheBank = 0;

// One bit records each tile position that differs in the inactive bank.
// Rewriting the same tile keeps its bit set, so it adds no copy work.
#define TILE_CACHE_DIRTY_U32_COUNT(n)    (((n) + 31) / 32)
static u32 snesTileCacheDirty[TILE_CACHE_DIRTY_U32_COUNT(MAX_TEXTURE_HASH_POSITIONS)];
// Mode7CharDirtyFlag is cleared within a frame, so it cannot track bank differences.
static u32 mode7TileCacheDirty[TILE_CACHE_DIRTY_U32_COUNT(MODE7_TILE_COUNT)];

// ~0u makes the first tile-cache write check the current submission.
static u32 lastTileCacheCheckSubmission = ~0u;

// The texture stores rows bottom-up, so nearby tile positions are not always nearby in memory.
static u32 snesTileCacheOffsetForTile(u32 p)
{
    return ((127 - (p >> 7)) * 128 + (p & 127)) * TILE_CACHE_TILE_BYTES;
}

static u32 mode7TileCacheOffsetForTile(u32 p)
{
    return ((15 - ((p >> 4) & 15)) * 16 + (p & 15)) * TILE_CACHE_TILE_BYTES;
}

typedef u32 (*TileOffsetFn)(u32);

static void cache3dsCopyDirtyTiles(
    const u8 *src, u8 *dst, u32 *dirty, u32 tileCount, TileOffsetFn offsetForTile)
{
    u32 p = 0;
    while (p < tileCount) {
        if ((p & 31) == 0) {
            while (p < tileCount && dirty[p >> 5] == 0)
                p += 32;
            if (p >= tileCount)
                break;
        }
        if (!(dirty[p >> 5] & (1u << (p & 31)))) {
            p++;
            continue;
        }

        u32 start = offsetForTile(p);
        u32 len = TILE_CACHE_TILE_BYTES;
        u32 next = p + 1;
        while (next < tileCount
            && (dirty[next >> 5] & (1u << (next & 31)))
            && offsetForTile(next) == start + len) {
            len += TILE_CACHE_TILE_BYTES;
            next++;
        }

        memcpy(dst + start, src + start, len);
        p = next;
    }

    memset(dirty, 0, TILE_CACHE_DIRTY_U32_COUNT(tileCount) * sizeof(u32));
}

static void cache3dsFlipTileCacheBanks()
{
    int inactiveBank = activeTileCacheBank ^ 1;

    cache3dsCopyDirtyTiles((const u8 *)snesTileCacheBanks[activeTileCacheBank], (u8 *)snesTileCacheBanks[inactiveBank],
        snesTileCacheDirty, MAX_TEXTURE_HASH_POSITIONS, snesTileCacheOffsetForTile);
    cache3dsCopyDirtyTiles((const u8 *)mode7TileCacheBanks[activeTileCacheBank], (u8 *)mode7TileCacheBanks[inactiveBank],
        mode7TileCacheDirty, MODE7_TILE_COUNT, mode7TileCacheOffsetForTile);

    activeTileCacheBank = inactiveBank;
    GPU3DS.textures[SNES_TILE_CACHE].tex.data = snesTileCacheBanks[inactiveBank];
    GPU3DS.textures[SNES_MODE7_TILE_CACHE].tex.data = mode7TileCacheBanks[inactiveBank];

    gpu3dsInvalidateTextureBind();
}

// Check once per submission. Waiting until the first write gives the GPU more
// time to finish.
static void cache3dsPrepareTileCacheWrite()
{
    u32 submission = gpu3dsSubmissionCount();
    if (lastTileCacheCheckSubmission == submission)
        return;

    lastTileCacheCheckSubmission = submission;

    bool submittedFrameUsesTileCaches = gpu3dsSubmissionUsesTexture(SNES_TILE_CACHE)
        || gpu3dsSubmissionUsesTexture(SNES_MODE7_TILE_CACHE);

    if (!submittedFrameUsesTileCaches || gpu3dsIsRenderQueueDone())
        return;

    // Unreachable in the current blocking frame flow. Keep this guard for future
    // C3D_FRAME_NONBLOCK use: a failed begin may leave the inactive bank in flight.
    if (!gpu3dsFrameBeginSucceeded()) {
        gpu3dsWaitForRenderQueue();
        return;
    }

    cache3dsFlipTileCacheBanks();
}

static inline void cache3dsMarkSnesTileDirty(u16 texturePosition)
{
    // HDMA variants are regenerated each frame and must not propagate.
    if (texturePosition >= MAX_TEXTURE_HASH_POSITIONS)
        return;

    snesTileCacheDirty[texturePosition >> 5] |= 1u << (texturePosition & 31);
}

static inline void cache3dsMarkMode7TileDirty(u16 texturePosition)
{
    mode7TileCacheDirty[texturePosition >> 5] |= 1u << (texturePosition & 31);
}

bool cache3dsAllocTileCacheBanks()
{
    SGPUTexture *tileTex = &GPU3DS.textures[SNES_TILE_CACHE];
    SGPUTexture *mode7Tex = &GPU3DS.textures[SNES_MODE7_TILE_CACHE];

    snesTileCacheBanks[0] = tileTex->tex.data;
    mode7TileCacheBanks[0] = mode7Tex->tex.data;
    snesTileCacheBanks[1] = linearAlloc(tileTex->tex.size);
    mode7TileCacheBanks[1] = linearAlloc(mode7Tex->tex.size);

    if (snesTileCacheBanks[1] == NULL || mode7TileCacheBanks[1] == NULL) {
        cache3dsDeallocTileCacheBanks();
        return false;
    }

    memset(snesTileCacheBanks[1], 0, tileTex->tex.size);
    memset(mode7TileCacheBanks[1], 0, mode7Tex->tex.size);
    memset(snesTileCacheDirty, 0, sizeof(snesTileCacheDirty));
    memset(mode7TileCacheDirty, 0, sizeof(mode7TileCacheDirty));
    activeTileCacheBank = 0;

    return true;
}

void cache3dsDeallocTileCacheBanks()
{
    // C3D_TexDelete owns the original allocation.
    // Restore it before freeing the extra bank.
    if (snesTileCacheBanks[0] != NULL)
        GPU3DS.textures[SNES_TILE_CACHE].tex.data = snesTileCacheBanks[0];
    if (mode7TileCacheBanks[0] != NULL)
        GPU3DS.textures[SNES_MODE7_TILE_CACHE].tex.data = mode7TileCacheBanks[0];

    if (snesTileCacheBanks[1] != NULL)
        linearFree(snesTileCacheBanks[1]);
    if (mode7TileCacheBanks[1] != NULL)
        linearFree(mode7TileCacheBanks[1]);

    snesTileCacheBanks[0] = snesTileCacheBanks[1] = NULL;
    mode7TileCacheBanks[0] = mode7TileCacheBanks[1] = NULL;
    activeTileCacheBank = 0;
}


//---------------------------------------------------------
// Initializes the Hash to Texture Position look-up (and
// the reverse look-up table as well)
//---------------------------------------------------------
void cache3dsInit()
{
    //printf ("Cache %8x\n", &vramCacheFrameNumber);
    //memset(&vramCacheFrameNumber, 0, MAX_HASH * 4);
    memset(&GPU3DSExt.vramCacheHashToTexturePosition, 0, (MAX_HASH + 1) * 2);

    // Fixes issue in Thunder Spirits where the tileaddr + pal
    // gives the texturePos of 0, but gets overwritten by other tiles
    //
    GPU3DSExt.vramCacheTexturePositionToHash[0] = 0;
    for (int i = 1; i < MAX_TEXTURE_HASH_POSITIONS; i++)
        GPU3DSExt.vramCacheTexturePositionToHash[i] = MAX_HASH;
    GPU3DSExt.newCacheTexturePosition = 2;
}


//---------------------------------------------------------
// Converts the tile in SNES bitplane format into 
// it's 5551 16-bit representation in 3DS texture format.
//---------------------------------------------------------
void cache3dsCacheSnesTileToTexturePosition(
    uint8 *snesTilePixels,
	uint16 *snesPalette,
    uint16 texturePosition)
{
    cache3dsPrepareTileCacheWrite();
    cache3dsMarkSnesTileDirty(texturePosition);

    uint16_t *tileTexture = (uint16_t *)GPU3DS.textures[SNES_TILE_CACHE].tex.data;
    uint32 base = snesTileCacheOffsetForTile(texturePosition) / sizeof(*tileTexture);

    #define GET_TILE_PIXEL(x)   (snesTilePixels[x] == 0 ? 0 : snesPalette[snesTilePixels[x]])
    tileTexture [base + 0] = GET_TILE_PIXEL(56);
    tileTexture [base + 1] = GET_TILE_PIXEL(57);
    tileTexture [base + 4] = GET_TILE_PIXEL(58);
    tileTexture [base + 5] = GET_TILE_PIXEL(59);
    tileTexture [base + 16] = GET_TILE_PIXEL(60);
    tileTexture [base + 17] = GET_TILE_PIXEL(61);
    tileTexture [base + 20] = GET_TILE_PIXEL(62);
    tileTexture [base + 21] = GET_TILE_PIXEL(63);

    tileTexture [base + 2] = GET_TILE_PIXEL(48);
    tileTexture [base + 3] = GET_TILE_PIXEL(49);
    tileTexture [base + 6] = GET_TILE_PIXEL(50);
    tileTexture [base + 7] = GET_TILE_PIXEL(51);
    tileTexture [base + 18] = GET_TILE_PIXEL(52);
    tileTexture [base + 19] = GET_TILE_PIXEL(53);
    tileTexture [base + 22] = GET_TILE_PIXEL(54);
    tileTexture [base + 23] = GET_TILE_PIXEL(55);

    tileTexture [base + 8] = GET_TILE_PIXEL(40);
    tileTexture [base + 9] = GET_TILE_PIXEL(41);
    tileTexture [base + 12] = GET_TILE_PIXEL(42);
    tileTexture [base + 13] = GET_TILE_PIXEL(43);
    tileTexture [base + 24] = GET_TILE_PIXEL(44);
    tileTexture [base + 25] = GET_TILE_PIXEL(45);
    tileTexture [base + 28] = GET_TILE_PIXEL(46);
    tileTexture [base + 29] = GET_TILE_PIXEL(47);

    tileTexture [base + 10] = GET_TILE_PIXEL(32);
    tileTexture [base + 11] = GET_TILE_PIXEL(33);
    tileTexture [base + 14] = GET_TILE_PIXEL(34);
    tileTexture [base + 15] = GET_TILE_PIXEL(35);
    tileTexture [base + 26] = GET_TILE_PIXEL(36);
    tileTexture [base + 27] = GET_TILE_PIXEL(37);
    tileTexture [base + 30] = GET_TILE_PIXEL(38);
    tileTexture [base + 31] = GET_TILE_PIXEL(39);

    tileTexture [base + 32] = GET_TILE_PIXEL(24);
    tileTexture [base + 33] = GET_TILE_PIXEL(25);
    tileTexture [base + 36] = GET_TILE_PIXEL(26);
    tileTexture [base + 37] = GET_TILE_PIXEL(27);
    tileTexture [base + 48] = GET_TILE_PIXEL(28);
    tileTexture [base + 49] = GET_TILE_PIXEL(29);
    tileTexture [base + 52] = GET_TILE_PIXEL(30);
    tileTexture [base + 53] = GET_TILE_PIXEL(31);

    tileTexture [base + 34] = GET_TILE_PIXEL(16);
    tileTexture [base + 35] = GET_TILE_PIXEL(17);
    tileTexture [base + 38] = GET_TILE_PIXEL(18);
    tileTexture [base + 39] = GET_TILE_PIXEL(19);
    tileTexture [base + 50] = GET_TILE_PIXEL(20);
    tileTexture [base + 51] = GET_TILE_PIXEL(21);
    tileTexture [base + 54] = GET_TILE_PIXEL(22);
    tileTexture [base + 55] = GET_TILE_PIXEL(23);

    tileTexture [base + 40] = GET_TILE_PIXEL(8);
    tileTexture [base + 41] = GET_TILE_PIXEL(9);
    tileTexture [base + 44] = GET_TILE_PIXEL(10);
    tileTexture [base + 45] = GET_TILE_PIXEL(11);
    tileTexture [base + 56] = GET_TILE_PIXEL(12);
    tileTexture [base + 57] = GET_TILE_PIXEL(13);
    tileTexture [base + 60] = GET_TILE_PIXEL(14);
    tileTexture [base + 61] = GET_TILE_PIXEL(15);

    tileTexture [base + 42] = GET_TILE_PIXEL(0);
    tileTexture [base + 43] = GET_TILE_PIXEL(1);
    tileTexture [base + 46] = GET_TILE_PIXEL(2);
    tileTexture [base + 47] = GET_TILE_PIXEL(3);
    tileTexture [base + 58] = GET_TILE_PIXEL(4);
    tileTexture [base + 59] = GET_TILE_PIXEL(5);
    tileTexture [base + 62] = GET_TILE_PIXEL(6);
    tileTexture [base + 63] = GET_TILE_PIXEL(7);

}

#undef GET_TILE_PIXEL


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
    uint32 *paletteMask)
{
    cache3dsPrepareTileCacheWrite();
    cache3dsMarkMode7TileDirty(texturePosition);

	uint16_t *tileTexture = (uint16_t *)GPU3DS.textures[SNES_MODE7_TILE_CACHE].tex.data;
    uint32 base = mode7TileCacheOffsetForTile(texturePosition) / sizeof(*tileTexture);
	uint32 charPaletteMask = 0;

    #define GET_TILE_PIXEL(x)   (snesTilePixels[x * 2] == 0 ? 0 : snesPalette[snesTilePixels[x * 2]]); charPaletteMask |= (1 << (snesTilePixels[x * 2] >> 3));
    tileTexture [base + 0] = GET_TILE_PIXEL(56);
    tileTexture [base + 1] = GET_TILE_PIXEL(57);
    tileTexture [base + 4] = GET_TILE_PIXEL(58);
    tileTexture [base + 5] = GET_TILE_PIXEL(59);
    tileTexture [base + 16] = GET_TILE_PIXEL(60);
    tileTexture [base + 17] = GET_TILE_PIXEL(61);
    tileTexture [base + 20] = GET_TILE_PIXEL(62);
    tileTexture [base + 21] = GET_TILE_PIXEL(63);

    tileTexture [base + 2] = GET_TILE_PIXEL(48);
    tileTexture [base + 3] = GET_TILE_PIXEL(49);
    tileTexture [base + 6] = GET_TILE_PIXEL(50);
    tileTexture [base + 7] = GET_TILE_PIXEL(51);
    tileTexture [base + 18] = GET_TILE_PIXEL(52);
    tileTexture [base + 19] = GET_TILE_PIXEL(53);
    tileTexture [base + 22] = GET_TILE_PIXEL(54);
    tileTexture [base + 23] = GET_TILE_PIXEL(55);

    tileTexture [base + 8] = GET_TILE_PIXEL(40);
    tileTexture [base + 9] = GET_TILE_PIXEL(41);
    tileTexture [base + 12] = GET_TILE_PIXEL(42);
    tileTexture [base + 13] = GET_TILE_PIXEL(43);
    tileTexture [base + 24] = GET_TILE_PIXEL(44);
    tileTexture [base + 25] = GET_TILE_PIXEL(45);
    tileTexture [base + 28] = GET_TILE_PIXEL(46);
    tileTexture [base + 29] = GET_TILE_PIXEL(47);

    tileTexture [base + 10] = GET_TILE_PIXEL(32);
    tileTexture [base + 11] = GET_TILE_PIXEL(33);
    tileTexture [base + 14] = GET_TILE_PIXEL(34);
    tileTexture [base + 15] = GET_TILE_PIXEL(35);
    tileTexture [base + 26] = GET_TILE_PIXEL(36);
    tileTexture [base + 27] = GET_TILE_PIXEL(37);
    tileTexture [base + 30] = GET_TILE_PIXEL(38);
    tileTexture [base + 31] = GET_TILE_PIXEL(39);

    tileTexture [base + 32] = GET_TILE_PIXEL(24);
    tileTexture [base + 33] = GET_TILE_PIXEL(25);
    tileTexture [base + 36] = GET_TILE_PIXEL(26);
    tileTexture [base + 37] = GET_TILE_PIXEL(27);
    tileTexture [base + 48] = GET_TILE_PIXEL(28);
    tileTexture [base + 49] = GET_TILE_PIXEL(29);
    tileTexture [base + 52] = GET_TILE_PIXEL(30);
    tileTexture [base + 53] = GET_TILE_PIXEL(31);

    tileTexture [base + 34] = GET_TILE_PIXEL(16);
    tileTexture [base + 35] = GET_TILE_PIXEL(17);
    tileTexture [base + 38] = GET_TILE_PIXEL(18);
    tileTexture [base + 39] = GET_TILE_PIXEL(19);
    tileTexture [base + 50] = GET_TILE_PIXEL(20);
    tileTexture [base + 51] = GET_TILE_PIXEL(21);
    tileTexture [base + 54] = GET_TILE_PIXEL(22);
    tileTexture [base + 55] = GET_TILE_PIXEL(23);

    tileTexture [base + 40] = GET_TILE_PIXEL(8);
    tileTexture [base + 41] = GET_TILE_PIXEL(9);
    tileTexture [base + 44] = GET_TILE_PIXEL(10);
    tileTexture [base + 45] = GET_TILE_PIXEL(11);
    tileTexture [base + 56] = GET_TILE_PIXEL(12);
    tileTexture [base + 57] = GET_TILE_PIXEL(13);
    tileTexture [base + 60] = GET_TILE_PIXEL(14);
    tileTexture [base + 61] = GET_TILE_PIXEL(15);

    tileTexture [base + 42] = GET_TILE_PIXEL(0);
    tileTexture [base + 43] = GET_TILE_PIXEL(1);
    tileTexture [base + 46] = GET_TILE_PIXEL(2);
    tileTexture [base + 47] = GET_TILE_PIXEL(3);
    tileTexture [base + 58] = GET_TILE_PIXEL(4);
    tileTexture [base + 59] = GET_TILE_PIXEL(5);
    tileTexture [base + 62] = GET_TILE_PIXEL(6);
    tileTexture [base + 63] = GET_TILE_PIXEL(7);

    *paletteMask = charPaletteMask;
}

#undef GET_TILE_PIXEL
