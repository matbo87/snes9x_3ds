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
	STexCoord2f  TexCoord;
    u32			 Color;
} SVertex;

typedef struct {
    SVector4i	Position;
	STexCoord2i	TexCoord;
} SMode7TileVertex;

#define MAX_TEXTURE_POSITIONS		16383
#define MAX_HASH					(65536 * 16 / 8)


typedef struct
{
    int                 mode7FrameCount = 0;
    GPU_TEXCOLOR        mode7TextureFormat;

    // Memory Usage = 0.25 MB (for hashing of the texture position)
    uint16  vramCacheHashToTexturePosition[MAX_HASH + 1];

    // Memory Usage = 0.06 MB
    int     vramCacheTexturePositionToHash[MAX_TEXTURE_POSITIONS];

    int     newCacheTexturePosition = 2;
} SGPU3DSExtended;

extern SGPU3DSExtended GPU3DSExt;

void gpu3dsSetMode7TexturesPixelFormat(GPU_TEXCOLOR fmt);

void gpu3dsInitializeMode7Vertexes();

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

    vertices[0].TexCoord = (STexCoord2f){tx0, ty0};
    vertices[1].TexCoord = (STexCoord2f){tx1, ty1};

    list->Count += 2;
}

inline void __attribute__((always_inline)) gpu3dsAddMode7LineVertexes(
    int x0, int y0, int x1, int y1,
    float tx0, float ty0, float tx1, float ty1)
{
    SVertexList *list = &GPU3DS.vertices[VBO_SCENE];
    SVertex *vertices = (SVertex *) list->data + list->FromIndex + list->Count;

    vertices[0].Position = (SVector4i){x0, y0, 0, 1};
    vertices[1].Position = (SVector4i){x1, y1, 0, 1};

    vertices[0].TexCoord = (STexCoord2f){tx0, ty0};
    vertices[1].TexCoord = (STexCoord2f){tx1, ty1};

    list->Count += 2;
}


inline void __attribute__((always_inline)) gpu3dsSetMode7TileTexturePos(int idx, int data)
{
    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];

    m7vertices[0].Position.z = data;
}


inline void __attribute__((always_inline)) gpu3dsSetMode7TileModifiedFlag(int idx)
{
    int updateFrame = GPU3DSExt.mode7FrameCount;
    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];

    m7vertices[0].Position.w = updateFrame;
}


inline void __attribute__((always_inline)) gpu3dsSetMode7TileModifiedFlag(int idx, int updateFrame)
{
    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];

    m7vertices[0].Position.w = updateFrame;
}

#endif