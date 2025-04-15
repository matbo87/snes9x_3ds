#include "3dssnes9x.h"
#include "3dsgpu.h"

#ifndef _3DSIMPL_GPU_H_
#define _3DSIMPL_GPU_H_

#define COMPOSE_HASH(vramAddr, pal)   ((vramAddr) << 4) + ((pal) & 0xf)

typedef struct {
    s16 x, y, z;
} SVector3i;

typedef struct {
    s16 x, y, z, w;
} SVector4i;

typedef struct {
	s16 u, v;
} STexCoord2i;

typedef struct {
    float u, v;
} STexCoord2f;

typedef struct {
    SVector3i    Position;
	STexCoord2i  TexCoord;
} SQuadVertex;

typedef struct {
    SVector4i    Position;
	STexCoord2i  TexCoord;
    u32			 Color;
} SVertex;

typedef struct {
    SVector4i	Position;
	STexCoord2i	TexCoord;
} SMode7TileVertex;

#define MAX_TEXTURE_POSITIONS		16383
#define MAX_HASH					(65536 * 16 / 8)
#define MAX_MODE7_VERTICES          16388
typedef enum 
{
    LAYER_BG0,
    LAYER_BG1,
    LAYER_BG2,
    LAYER_BG3,
    LAYER_OBJ,
    LAYER_BACKDROP,
    LAYER_COLOR_MATH,
} LAYER_ID;


typedef struct
{
    int                 mode7FrameCount = 0;
    GPU_TEXCOLOR        mode7TextureFormat;
    bool    mode7TilesModified;
    bool    mode7SectionsModified[4];

    // Memory Usage = 0.25 MB (for hashing of the texture position)
    uint16  vramCacheHashToTexturePosition[MAX_HASH + 1];

    // Memory Usage = 0.06 MB
    int     vramCacheTexturePositionToHash[MAX_TEXTURE_POSITIONS];

    int     newCacheTexturePosition = 2;
} SGPU3DSExtended;

extern SGPU3DSExtended GPU3DSExt;

void gpu3dsSetMode7TexturesPixelFormat(GPU_TEXCOLOR fmt);

void gpu3dsInitializeMode7Vertexes();

inline bool __attribute__((always_inline)) gpu3dsUpdateRenderState(SGPURenderState* state, u32 propertyType, u32 newValue, u32 oldValue) {    
    bool valueChanged;

    if (GPU3DS.initializedRenderStateFlags & propertyType) {
        valueChanged = newValue != oldValue;
    } else {
        // always update if property hasn't been set yet
        GPU3DS.initializedRenderStateFlags |= propertyType;
        valueChanged = true;
    }

    if (!valueChanged)
        return false;

    switch (propertyType) {
        case FLAG_SHADER:
            state->shader = (SGPU_SHADER_PROGRAM)newValue;
            break;
        case FLAG_TARGET:
            state->target = (SGPU_TARGET_ID)newValue;
            break;
        case FLAG_TEXTURE_BIND:
            state->textureBind = (SGPU_TEXTURE_ID)newValue;
            break;
        case FLAG_TEXTURE_ENV:
            state->textureEnv = (SGPU_TEX_ENV)newValue;
            break;
        case FLAG_STENCIL_TEST:
            state->stencilTest = newValue;
            break;
        case FLAG_DEPTH_TEST:
            state->depthTest = (SGPU_DEPTH_TEST)newValue;
            break;
        case FLAG_ALPHA_TEST:
            state->alphaTest = (SGPU_ALPHA_TEST)newValue;
            break;
        case FLAG_ALPHA_BLENDING:
            state->alphaBlending = (SGPU_ALPHA_BLENDINGMODE)newValue;
            break;
        case FLAG_TEXTURE_OFFSET:
            state->textureOffset = (bool)newValue;
            break;
        case FLAG_UPDATE_FRAME:
            state->updateFrame = newValue;
            break;
    }

    GPU3DS.currentRenderStateFlags |= propertyType;

    return true;
}

inline void __attribute__((always_inline)) gpu3dsAddQuadVertexes(
    int x0, int y0, int x1, int y1,
    int tx0, int ty0, int tx1, int ty1,
    int data)
{
    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    SQuadVertex *vertices = &((SQuadVertex *) list->data)[list->FromIndex + list->Count];

	vertices[0].Position = (SVector3i){x0, y0, data};
	vertices[1].Position = (SVector3i){x1, y0, data};
	vertices[2].Position = (SVector3i){x0, y1, data};

	vertices[3].Position = (SVector3i){x1, y1, data};
	vertices[4].Position = (SVector3i){x0, y1, data};
	vertices[5].Position = (SVector3i){x1, y0, data};

	vertices[0].TexCoord = (STexCoord2i){tx0, ty0};
	vertices[1].TexCoord = (STexCoord2i){tx1, ty0};
	vertices[2].TexCoord = (STexCoord2i){tx0, ty1};

	vertices[3].TexCoord = (STexCoord2i){tx1, ty1};
	vertices[4].TexCoord = (STexCoord2i){tx0, ty1};
	vertices[5].TexCoord = (STexCoord2i){tx1, ty0};

    list->Count += 6;
}


inline void __attribute__((always_inline)) gpu3dsAddRectangleVertexes(int x0, int y0, int x1, int y1, u32 color)
{
    SVertexList *list = &GPU3DS.vertices[VBO_SCENE];
    SVertex *vertices = &((SVertex *) list->data)[list->FromIndex + list->Count];

    // using -1 for non-tile detection in shader
    vertices[0].Position = (SVector4i){x0, y0, 0, -1};
    vertices[1].Position = (SVector4i){x1, y1, 0, -1};

    u32 swappedColor = __builtin_bswap32(color);
    vertices[0].Color = swappedColor;
    vertices[1].Color = swappedColor;

    list->Count += 2;
}


inline void __attribute__((always_inline)) gpu3dsAddTileVertexes(
    int x0, int y0, int x1, int y1,
    int tx0, int ty0, int tx1, int ty1,
    int z)
{
    SVertexList *list = &GPU3DS.vertices[VBO_SCENE];
    SVertex *vertices = &((SVertex *) list->data)[list->FromIndex + list->Count];

    vertices[0].Position = (SVector4i){x0, y0, z, 1};
    vertices[1].Position = (SVector4i){x1, y1, z, 1};

    vertices[0].TexCoord = (STexCoord2i){tx0, ty0};
    vertices[1].TexCoord = (STexCoord2i){tx1, ty1};

    list->Count += 2;
}

inline void __attribute__((always_inline)) gpu3dsAddMode7LineVertexes(
    int x0, int y0, int x1, int y1,
    int tx0, int ty0, int tx1, int ty1)
{
    SVertexList *list = &GPU3DS.vertices[VBO_SCENE];
    SVertex *vertices = (SVertex *) list->data + list->FromIndex + list->Count;

    vertices[0].Position = (SVector4i){x0, y0, 0, 1};
    vertices[1].Position = (SVector4i){x1, y1, 0, 1};

    vertices[0].TexCoord = (STexCoord2i){tx0, ty0};
    vertices[1].TexCoord = (STexCoord2i){tx1, ty1};

    list->Count += 2;
}


inline void __attribute__((always_inline)) gpu3dsSetMode7TileModified(int idx, u8 data)
{
    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];

    m7vertices[0].Position.w = GPU3DSExt.mode7FrameCount;
    m7vertices[0].Position.z = data;

    if (!GPU3DSExt.mode7TilesModified)
        GPU3DSExt.mode7TilesModified = true;

    GPU3DSExt.mode7SectionsModified[idx >> 12] = true;
}

#endif