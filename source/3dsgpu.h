
#ifndef _3DSGPU_H_
#define _3DSGPU_H_

#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>
#include "gpulib.h"
#include "gfx.h"

#define FLAG_SHADER             BIT(0)
#define FLAG_TARGET             BIT(1)
#define FLAG_TEXTURE_BIND       BIT(2)
#define FLAG_TEXTURE_ENV        BIT(3)
#define FLAG_STENCIL_TEST       BIT(4)
#define FLAG_DEPTH_TEST         BIT(5)
#define FLAG_ALPHA_TEST         BIT(6)
#define FLAG_ALPHA_BLENDING     BIT(7)
#define FLAG_TEXTURE_OFFSET     BIT(8)
#define FLAG_UPDATE_FRAME       BIT(9)

#define DISPLAY_TRANSFER_FMT GX_TRANSFER_FMT_RGB8

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(DISPLAY_TRANSFER_FMT) | GX_TRANSFER_OUT_FORMAT(DISPLAY_TRANSFER_FMT) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define UPDATE_PROPERTY_IF_CHANGED(property, flag) \
    if (propertyFlags & flag) { \
        if (currentState->property != overrides->property) { \
            currentState->property = overrides->property; \
            GPU3DS.currentRenderStateFlags |= flag; \
        } \
    }\

#define PROPERTY_HAS_CHANGED(property, flag) \
    if ((propertyFlags & flag) && (currentState->property != overrides->property)) \
        return true;

// C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0) -> stencilMode = 16
// C3D_StencilTest(true, GPU_NEVER, 0, 0, 0) -> stencilMode = 1;
#define STENCIL_TEST_DISABLED 16
#define STENCIL_TEST_ENABLED_WINDOWING_DISABLED 1

#define SCREEN_TARGET_COUNT  3

typedef enum {
    SCREEN_TARGET_LEFT,
    SCREEN_TARGET_RIGHT,
    SCREEN_TARGET_BOTTOM
} SCREEN_TARGET;

typedef enum {
    EMUSTATE_EMULATE = 1,
    EMUSTATE_PAUSEMENU = 2,
    EMUSTATE_END = 3
} EMUSTATE;

typedef enum { 
    SPROGRAM_SCREEN, 
    SPROGRAM_TILES, 
    SPROGRAM_MODE7, 
    SPROGRAM_COUNT,
} SGPU_SHADER_PROGRAM;

typedef enum { 
    ULOC_PROJECTION,
    ULOC_TEX_SCALE,
    ULOC_TEX_OFFSET,
    ULOC_UPDATE_FRAME,
    ULOC_COUNT
} SGPU_SHADER_ULOC;

typedef enum
{
    TARGET_SNES_MAIN,    
	TARGET_SNES_SUB,
	TARGET_SNES_DEPTH,
	TARGET_SNES_MODE7_FULL,
    TARGET_SNES_MODE7_TILE_0,
	TARGET_SCREEN_PRIMARY,
    TARGET_SCREEN_SECONDARY,
    TARGET_COUNT,
} SGPU_TARGET_ID;

typedef enum
{
    // --- Ingame Textures ---
    SNES_MAIN,
    SNES_SUB,
	SNES_DEPTH,
	SNES_MODE7_FULL,
	SNES_MODE7_TILE_0,
	SNES_TILE_CACHE,
    SNES_MODE7_TILE_CACHE,
    
    // --- UI Textures ---
    UI_BORDER,
    UI_BEZEL,
    UI_COVER,
    UI_ATLAS,
    
    TEX_COUNT,
} SGPU_TEXTURE_ID;

// explicit is always better than implicit ツ
static const SGPU_TEXTURE_ID UI_TEXTURE_START = UI_BORDER;

typedef enum 
{
    TEX_ENV_REPLACE_COLOR,
    TEX_ENV_REPLACE_TEXTURE0,
    TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA,
    TEX_ENV_BLEND_COLOR_TEXTURE0,
    TEX_ENV_UNSET,
} SGPU_TEX_ENV;

typedef enum 
{
    ALPHA_TEST_DISABLED,
    ALPHA_TEST_NE_ZERO,
    ALPHA_TEST_GTE_0_5,
    ALPHA_TEST_GTE_1_0,
    ALPHA_TEST_UNSET,
} SGPU_ALPHA_TEST;

typedef enum 
{
    ALPHA_BLENDING_DISABLED,
    ALPHA_BLENDING_ENABLED,
    ALPHA_BLENDING_KEEP_DEST_ALPHA,
    ALPHA_BLENDING_ADD,
    ALPHA_BLENDING_ADD_DIV2,
    ALPHA_BLENDING_SUB,
    ALPHA_BLENDING_SUB_DIV2,
    ALPHA_BLENDING_UNSET,
} SGPU_ALPHA_BLENDINGMODE;

typedef enum
{
    VBO_SCENE_RECT,
    VBO_SCENE_TILE,
    VBO_SCENE_MODE7_LINE,
    VBO_MODE7_TILE,
    VBO_SCREEN,
    VBO_COUNT,
} SGPU_VBO_ID;

typedef enum
{
    PROFILING_NONE,
    PROFILING_CUSTOM,
    PROFILING_FPS,
} SGPU_PROFILING_MODE;

typedef struct
{   
    u32                     param;
    SGPU_TEXTURE_ID         id;
    GPU_TEXCOLOR            fmt;
    u16                     width;
    u16                     height;
} SGPUTextureConfig;

typedef struct
{
	C3D_Mtx                 projection;
    C3D_Tex                 tex;
    C3D_RenderTarget        *target;
    SGPU_TEXTURE_ID         id;

    float                   scale[4];
} SGPUTexture;

typedef struct {
	DVLB_s 				*dvlb;
	shaderProgram_s 	shaderProgram;
} SGPUShader;

typedef struct
{
  GPU_FORMATS format;
  int count;
} AttrInfoFormat;

typedef struct
{
    SGPU_VBO_ID         id;
    int                 sizeInBytes;
    int                 vertexSize;
    int                 totalAttributes;
    AttrInfoFormat      attrFormat[3];
} SVertexListInfo;

typedef struct
{
    C3D_AttrInfo        attrInfo;

    void                *data;
    void                *data_base;
    
    u32                 sizeInBytes;
    u32                 vertexSize;
    int                 count;
    int                 from;
    
    GPU_Primitive_t     primitive;
    SGPU_VBO_ID         id;
    bool                flip;
} SVertexList;

typedef union {
    struct {
        u32 stencilTest;
        
        SGPU_TEXTURE_ID textureBind : 8; // max enum value = 127
        SGPU_ALPHA_BLENDINGMODE alphaBlending : 8;

        SGPU_TEX_ENV textureEnv : 4; // max enum value = 7
        SGPU_ALPHA_TEST alphaTest : 4;

        bool textureOffset : 1;
        
        u8 _padding : 7;
    };
    u64 packed;
} SGPURenderState;

typedef struct
{
    SGPUTexture                 textures[TEX_COUNT];
    SVertexList                 vertices[VBO_COUNT];
    SGPUShader                  shaders[SPROGRAM_COUNT];

    SGPURenderState             currentRenderState;

    C3D_Mtx                     projectionTopScreen;
    C3D_Mtx                     projectionBottomScreen;
    
    C3D_RenderTarget            *screenTargets[SCREEN_TARGET_COUNT];
    void                        *currentBuffer;

    u32                         currentRenderStateFlags;
    u32                         currentRenderTargetDim;
    u32                         currentTextureDim;

    u32                         vramTotal;
    u32                         linearMemTotal;
    
    s8                          shaderULocs[ULOC_COUNT];

    EMUSTATE                    emulatorState;
    SGPU_TARGET_ID              currentRenderTarget;
    SGPU_SHADER_PROGRAM         currentShader;
    SGPU_PROFILING_MODE         profilingMode;
    
    bool                        depthTestEnabled;
    bool                        isReal3DS;
    bool                        isNew3DS;
    bool                        gpuSwapPending;
} SGPU3DS;

extern SGPU3DS GPU3DS;


inline void __attribute__((always_inline)) gpu3dsUpdateRenderStateIfChanged(
    SGPURenderState* currentState,
    u32 propertyFlags,
    const SGPURenderState* overrides) {
    if (currentState->packed == overrides->packed) return;

    UPDATE_PROPERTY_IF_CHANGED(textureBind, FLAG_TEXTURE_BIND);
    UPDATE_PROPERTY_IF_CHANGED(textureEnv, FLAG_TEXTURE_ENV);
    UPDATE_PROPERTY_IF_CHANGED(stencilTest, FLAG_STENCIL_TEST);
    UPDATE_PROPERTY_IF_CHANGED(alphaTest, FLAG_ALPHA_TEST);
    UPDATE_PROPERTY_IF_CHANGED(alphaBlending, FLAG_ALPHA_BLENDING);
    UPDATE_PROPERTY_IF_CHANGED(textureOffset, FLAG_TEXTURE_OFFSET);
}

inline bool __attribute__((always_inline)) gpu3dsRenderStateHasChangedInLayer(
    const SGPURenderState* currentState,
    u32 propertyFlags,
    const SGPURenderState* overrides)
{
    if (currentState->packed == overrides->packed) return false;
    
    PROPERTY_HAS_CHANGED(stencilTest, FLAG_STENCIL_TEST);
    PROPERTY_HAS_CHANGED(alphaTest, FLAG_ALPHA_TEST);
    PROPERTY_HAS_CHANGED(textureBind, FLAG_TEXTURE_BIND);
    PROPERTY_HAS_CHANGED(textureOffset, FLAG_TEXTURE_OFFSET);

    return false;
}

bool gpu3dsInitialize();
void gpu3dsFinalize();

bool gpu3dsAllocVertexList(SVertexListInfo *info);
void gpu3dsDeallocVertexList(SVertexList *list);

bool gpu3dsAllocVramTextureAndTarget(SGPUTexture *texture, const SGPUTextureConfig *config);
bool gpu3dsAllocLinearTexture(SGPUTexture *texture, const SGPUTextureConfig *config);

void gpu3dsClearTexture(SGPUTexture *texture, u32 color = 0);
void gpu3dsDestroyTexture(SGPUTexture *texture);

int gpu3dsGetPixelSize(GPU_TEXCOLOR pixelFormat);
size_t gpu3dsGetFmtSize(GPU_TEXCOLOR pixelFormat); 
u8 gpu3dsGetFrameBufferFmt(GPU_TEXCOLOR pixelFormat, bool isDepthBuffer = false);
s8 gpu3dsGetTransferFmt(GPU_TEXCOLOR pixelFormat);

u32 gpu3dsGetNextPowerOf2(u32 v);

void gpu3dsCopyVRAMTilesIntoMode7TileVertexes(uint8 *VRAM);
void gpu3dsIncrementMode7UpdateFrameCount();

void gpu3dsResetState();

bool gpu3dsInitializeShaderUniformLocations();

void gpu3dsLoadShader(SGPU_SHADER_PROGRAM shaderIndex, u32 *shaderBinary, int size, int geometryShaderStride);

void gpu3dsSetRenderTargetToFrameBuffer();
void gpu3dsSetRenderTargetToTexture(SGPU_TARGET_ID textureId);

void gpu3dsSwapVertexListForNextFrame(SVertexList *list);

void gpu3dsEnableAlphaTestNotEqualsZero();
void gpu3dsEnableAlphaTestGreaterThanEquals(uint8 alpha);
void gpu3dsDisableAlphaTest();

void gpu3dsEnableDepthTest();
void gpu3dsDisableDepthTest();

void gpu3dsEnableStencilTest(GPU_TESTFUNC func, u8 ref, u8 input_mask);
void gpu3dsDisableStencilTest();

void gpu3dsClearTextureEnv(u8 num);
void gpu3dsSetTextureEnvironmentReplaceColor();
void gpu3dsSetTextureEnvironmentReplaceTexture0();
void gpu3dsSetTextureEnvironmentReplaceTexture0WithColorAlpha();
void gpu3dsSetTextureEnvironmentBlendColorOnTexture();

void gpu3dsBindTexture(SGPU_TEXTURE_ID textureId);

void gpu3dsEnableAlphaBlending();
void gpu3dsEnableAdditiveBlending();
void gpu3dsEnableSubtractiveBlending();
void gpu3dsEnableAdditiveDiv2Blending();
void gpu3dsEnableSubtractiveDiv2Blending();
void gpu3dsDisableAlphaBlending();
void gpu3dsDisableAlphaBlendingKeepDestAlpha();

void gpu3dsSetDefaultRenderState(SGPU_SHADER_PROGRAM shader, bool newFrame, bool isSecondaryScreen = false);
void gpu3dsSetFragmentOperations(SGPURenderState *state, u32 flags);
void gpu3dsSetShaderAndUniforms(SGPURenderState *state, u32 flags, bool targetUpdated, bool textureUpdated);

static inline void gpu3dsApplyRenderState(SGPURenderState *state)
{    
    u32 flags = GPU3DS.currentRenderStateFlags;

    if (!flags) {
        return;
    }
    
    bool targetUpdated = flags & FLAG_TARGET;

    if (targetUpdated) {
        C3D_RenderTarget *target;

        if (GPU3DS.currentRenderTarget == TARGET_SCREEN_PRIMARY || GPU3DS.currentRenderTarget == TARGET_SCREEN_SECONDARY) {
            gpu3dsSetRenderTargetToFrameBuffer();
        } else {
            gpu3dsSetRenderTargetToTexture(GPU3DS.currentRenderTarget);
        }
    }

    // update texture + environment
    bool textureUpdated = flags & FLAG_TEXTURE_BIND;

    if (textureUpdated) {
        gpu3dsBindTexture(state->textureBind);
    }

    if (flags & FLAG_TEXTURE_ENV) {
        switch (state->textureEnv)
        {
            case TEX_ENV_REPLACE_TEXTURE0:
                gpu3dsSetTextureEnvironmentReplaceTexture0();
                break;
            case TEX_ENV_BLEND_COLOR_TEXTURE0:
                gpu3dsSetTextureEnvironmentBlendColorOnTexture();
                break;
            case TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA:
                gpu3dsSetTextureEnvironmentReplaceTexture0WithColorAlpha();
                break;
            default:
                gpu3dsSetTextureEnvironmentReplaceColor();
                break;
        }
    }

    gpu3dsSetFragmentOperations(state, flags);
    gpu3dsSetShaderAndUniforms(state, flags, targetUpdated, textureUpdated);

    GPU3DS.currentRenderStateFlags = 0;
}

static inline void gpu3dsSetAttributeBuffers(SVertexList *list)
{
    if (GPU3DS.currentBuffer != list->data)
    {
	    C3D_SetAttrInfo(&list->attrInfo);
	
	    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
	    BufInfo_Init(bufInfo);
	    BufInfo_Add(bufInfo, list->data, list->vertexSize, list->attrInfo.attrCount, list->attrInfo.permutation);

        GPU3DS.currentBuffer = list->data;
    }
}

void gpu3dsDraw(SVertexList *list, const void* indices, int count, int from = -1);
bool gpu3dsFrameBegin(u8 flags = 0, bool ingame = false, bool isSecondaryScreen = false);
void gpu3dsFrameEnd(u8 flags = 0);
bool gpu3dsClearScreen(gfxScreen_t screen, bool isTopStereo = false);

void gpu3dsCheckSlider();

// for debugging
const char* SGPUTextureIDToString(SGPU_TEXTURE_ID id);
const char* SGPUTexColorToString(GPU_TEXCOLOR color);
const char* SGPUVboIDToString(SGPU_VBO_ID color);
void printSGPURenderState(SGPURenderState *state, bool paused);


#endif
