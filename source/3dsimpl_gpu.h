#include "3dssnes9x.h"
#include "3dsgpu.h"

#ifndef _3DSIMPL_GPU_H_
#define _3DSIMPL_GPU_H_

#define COMPOSE_HASH(vramAddr, pal)   ((vramAddr) << 4) + ((pal) & 0xf)

#define MAX_VERTICES 32768
#define LAYERS_COUNT 9

#define MAX_TEXTURE_POSITIONS		16383
#define MAX_HASH					(65536 * 16 / 8)
#define MAX_MODE7_VERTICES          16388

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

typedef enum 
{
    LAYER_BG0,
    LAYER_BG1,
    LAYER_BG2,
    LAYER_BG3,
    LAYER_OBJ,
    LAYER_BACKDROP,
    LAYER_COLOR_MATH, // main target
    LAYER_BRIGHTNESS, // main target
    LAYER_WINDOW_LR, // depth target
} LAYER_ID; // keep this order!

typedef enum
{
    VS_BACKDROP_SUB,    
	VS_BACKDROP_MAIN,
	VS_CLIP_TO_BLACK,
	VS_COLOR_MATH,
} VERTICAL_SECTION_ID;

typedef union {
    u64 packed;
    struct {
        u32 color;
        u32 v2; // depth for backdrop, stencil for color math
    };
} DrawableSectionValue;

typedef union {
    u16 packed;
    struct {
        u8 alphaBlending;
        u8 textureEnv;
    };
} DrawableSectionRenderState;

typedef struct 
{
	DrawableSectionValue value;
	DrawableSectionRenderState state;

    u16 	startY;
    u16 	endY;
} DrawableVerticalSection;


typedef struct 
{
    SGPURenderState     state;

    u16                 from;
    u16                 count;
} SLayerSection;

typedef struct 
{
    u32             propertyFlags[2];

    u32             bufferOffset;

    u16             sectionsByTarget[2];
    u16             verticesByTarget[2];
    u16             sectionsTotal;
    u16             sectionsMax;
    u16             sectionsOffset;
    u16             sectionsSkipped;

    LAYER_ID        id;
    bool            m7Tile0;
} SLayer;

typedef struct
{
    SLayer          layers[LAYERS_COUNT];
    LAYER_ID        layersByTarget[2][LAYERS_COUNT];

    SLayerSection   *sections;
    void            *ibo;
    void            *ibo_base;

    u32             sizeInBytes;

    u16             verticesTotal;    
    u16             sectionsSizeInBytes;
    u16             sectionsMax;

    u8              layersTotalByTarget[2];
    
    bool            anythingOnSub;
    bool            flip;
    bool            hasSkippedSections;
} SLayerList;

typedef struct
{
    u16             vramCacheHashToTexturePosition[MAX_HASH + 1]; // 262146 bytes
    int             vramCacheTexturePositionToHash[MAX_TEXTURE_POSITIONS]; // 65532 bytes
    
    SLayerList      layerList;

    u32             mode7FrameCount;
    u32             newCacheTexturePosition;

    GPU_TEXCOLOR    mode7TextureFormat;
    bool            mode7TilesModified;
} SGPU3DSExtended;

extern SGPU3DSExtended GPU3DSExt;

void gpu3dsDeallocLayers();
void gpu3dsResetLayerSectionLimits(SLayerList *list);
void gpu3dsPrepareLayersForNextFrame();
void gpu3dsInitLayers();
void gpu3dsPrepareAndDrawLayers();
void gpu3dsCommitLayerSection(LAYER_ID id, SGPURenderState *state, bool reuseVertices = false);

void gpu3dsSetMode7TexturesPixelFormat(GPU_TEXCOLOR fmt);

void gpu3dsInitializeMode7Vertexes();

inline u16 __attribute__((always_inline)) gpu3dsGetValueWithinLimit(u16 value, u32 from, u32 max) {
    return (from + value > max) ? (max - from) : value;
}

inline void __attribute__((always_inline)) gpu3dsAddQuadVertexes(
    int x0, int y0, int x1, int y1,
    int tx0, int ty0, int tx1, int ty1,
    int data)
{
    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    SQuadVertex *vertices = &((SQuadVertex *) list->data)[list->from + list->count];

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

    list->count += 6;
}


inline void __attribute__((always_inline)) gpu3dsAddRectangleVertexes(int x0, int y0, int x1, int y1, u32 color)
{
    SVertexList *list = &GPU3DS.vertices[VBO_SCENE];
    SVertex *vertices = &((SVertex *) list->data)[list->from + list->count];

    // using -1 for non-tile detection in shader
    vertices[0].Position = (SVector4i){x0, y0, 0, -1};
    vertices[1].Position = (SVector4i){x1, y1, 0, -1};

    u32 swappedColor = __builtin_bswap32(color);
    vertices[0].Color = swappedColor;
    vertices[1].Color = swappedColor;

    list->count += 2;
}


inline void __attribute__((always_inline)) gpu3dsAddTileVertexes(
    int x0, int y0, int x1, int y1,
    int tx0, int ty0, int tx1, int ty1,
    int z)
{
    SVertexList *list = &GPU3DS.vertices[VBO_SCENE];
    SVertex *vertices = &((SVertex *) list->data)[list->from + list->count];

    vertices[0].Position = (SVector4i){x0, y0, z, 1};
    vertices[1].Position = (SVector4i){x1, y1, z, 1};

    vertices[0].TexCoord = (STexCoord2i){tx0, ty0};
    vertices[1].TexCoord = (STexCoord2i){tx1, ty1};

    list->count += 2;
}

inline void __attribute__((always_inline)) gpu3dsAddMode7LineVertexes(
    int x0, int y0, int x1, int y1,
    int tx0, int ty0, int tx1, int ty1)
{
    SVertexList *list = &GPU3DS.vertices[VBO_SCENE];
    SVertex *vertices = (SVertex *) list->data + list->from + list->count;

    vertices[0].Position = (SVector4i){x0, y0, 0, 1};
    vertices[1].Position = (SVector4i){x1, y1, 0, 1};

    vertices[0].TexCoord = (STexCoord2i){tx0, ty0};
    vertices[1].TexCoord = (STexCoord2i){tx1, ty1};

    list->count += 2;
}


inline void __attribute__((always_inline)) gpu3dsSetMode7TileModified(int idx, u8 data)
{
    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];

    m7vertices[0].Position.w = GPU3DSExt.mode7FrameCount;
    m7vertices[0].Position.z = data;

    if (!GPU3DSExt.mode7TilesModified)
        GPU3DSExt.mode7TilesModified = true;
}

#endif