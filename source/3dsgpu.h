
#ifndef _3DSGPU_H_
#define _3DSGPU_H_

#include <3ds.h>
#include <citro3d.h>
#include "gpulib.h"
#include "3dssnes9x.h"
#include "gfx.h"

#define BUFFER_BASE_PADDR 0x18000000

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

typedef enum {
    EMUSTATE_EMULATE = 1,
    EMUSTATE_PAUSEMENU = 2,
    EMUSTATE_END = 3
} EMUSTATE;

typedef enum { 
    SPROGRAM_SCREEN, 
    SPROGRAM_TILES, 
    SPROGRAM_MODE7, 
    SPROGRAM_UNSET,
} SGPU_SHADER_PROGRAM;

typedef enum { 
    ULOC_PROJECTION,
    ULOC_TEX_SCALE,
    ULOC_TEX_OFFSET,
    ULOC_UPDATE_FRAME
} SGPU_SHADER_ULOC;

typedef enum
{
    TARGET_SNES_MAIN,    
	TARGET_SNES_SUB,
	TARGET_SNES_DEPTH,
	TARGET_SNES_MODE7_FULL,
    TARGET_SNES_MODE7_TILE_0,
	TARGET_SCREEN,
    TARGET_UNSET,
} SGPU_TARGET_ID;

typedef enum
{
    SNES_MAIN,
    SNES_SUB,
	SNES_DEPTH,
	SNES_MODE7_FULL,
	SNES_MODE7_TILE_0,
    SCREEN_BEZEL,
	SNES_TILE_CACHE,
    SNES_MODE7_TILE_CACHE,
} SGPU_TEXTURE_ID;

typedef enum 
{
    TEX_ENV_REPLACE_COLOR,
    TEX_ENV_REPLACE_TEXTURE0,
    TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA,
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
    VBO_UNSET,
} SGPU_VBO_ID;

typedef struct
{
    SGPU_TEXTURE_ID         id;
    u16                     width, height;
    GPU_TEXCOLOR            format;
    u32                     param;
    bool                    onVram;
    bool                    hasTarget;  // always false for !onVram
    bool                    hasDepthBuf;  // always false for !hasTarget
} SGPUTextureConfig;

typedef struct
{
	C3D_Mtx                 projection;
    C3D_Tex                 tex;
    float                   scale[4];

    SGPU_TEXTURE_ID         id;
    bool                    texInitialized;
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
        SGPU_TEXTURE_ID textureBind : 8; 
        SGPU_TEX_ENV textureEnv : 3;
        SGPU_ALPHA_TEST alphaTest : 4;
        SGPU_ALPHA_BLENDINGMODE alphaBlending : 8;
        bool textureOffset : 1;
    };
    u64 packed;
} SGPURenderState;

typedef struct
{
    SGPUTexture                 textures[8];
    SVertexList                 vertices[5];
    SGPUShader                  shaders[3];
    
    C3D_Mtx                     projectionTopScreen;
    C3D_Mtx                     projectionBottomScreen;

    SGPURenderState             currentRenderState;

    void                        *frameBuffer;
    void                        *frameDepthBuffer;

    u32                         currentRenderStateFlags;
    u32                         currentRenderTargetDim;
    u32                         currentTextureDim;
    s8                          shaderULocs[4];

    EMUSTATE                    emulatorState;
    GSPGPU_FramebufferFormat    screenFormat;
    GPU_TEXCOLOR                frameBufferFormat;

    SGPU_VBO_ID                 currentVbo;
    SGPU_TARGET_ID              currentRenderTarget;
    SGPU_SHADER_PROGRAM         currentShader;
    
    bool                        depthTestEnabled;
    bool                        isReal3DS;
    bool                        isNew3DS;
    bool                        enableDebug;
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

bool gpu3dsInitTexture(SGPUTextureConfig *config);
void gpu3dsClearTexture(SGPUTexture *texture, u32 color = 0);
void gpu3dsDestroyTexture(SGPUTexture *texture);

int gpu3dsGetPixelSize(GPU_TEXCOLOR pixelFormat);
size_t gpu3dsGetFmtSize(GPU_TEXCOLOR pixelFormat); 
u8 gpu3dsGetFrameBufferFmt(GPU_TEXCOLOR pixelFormat, bool isDepthBuffer = false);
u32 gpu3dsGetNextPowerOf2(u32 v);

void gpu3dsStartNewFrame();

void gpu3dsCopyVRAMTilesIntoMode7TileVertexes(uint8 *VRAM);
void gpu3dsIncrementMode7UpdateFrameCount();

void gpu3dsResetState();

bool gpu3dsInitializeShaderUniformLocations();

void gpu3dsLoadShader(SGPU_SHADER_PROGRAM shaderIndex, u32 *shaderBinary, int size, int geometryShaderStride);

void gpu3dsSetRenderTargetToFrameBuffer();
void gpu3dsSetRenderTargetToTexture(SGPU_TARGET_ID textureId);
void gpu3dsSetRenderTargetToMode7Texture(u32 pixelOffset);

void gpu3dsFlush();
void gpu3dsWaitForPreviousFlush();
void gpu3dsFrameEnd();

void gpu3dsTransferToScreenBuffer(gfxScreen_t screen);
void gpu3dsSwapVertexListForNextFrame(SVertexList *list);
void gpu3dsSwapScreenBuffers();

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

void gpu3dsBindTexture(SGPU_TEXTURE_ID textureId);

void gpu3dsEnableAlphaBlending();
void gpu3dsEnableAdditiveBlending();
void gpu3dsEnableSubtractiveBlending();
void gpu3dsEnableAdditiveDiv2Blending();
void gpu3dsEnableSubtractiveDiv2Blending();
void gpu3dsDisableAlphaBlending();
void gpu3dsDisableAlphaBlendingKeepDestAlpha();


void gpu3dsSetFragmentOperations(SGPURenderState *state, u32 flags);
void gpu3dsSetShaderAndUniforms(SGPURenderState *state, u32 flags, bool targetUpdated, bool textureUpdated);

static inline void gpu3dsApplyRenderState(SGPURenderState *state)
{    
    u32 flags = GPU3DS.currentRenderStateFlags;

    if (!flags) {
        return;
    }
    
    bool targetUpdated = flags & FLAG_TARGET;

    // update viewport
    // ! order seems important here !
    // binding the shader before setting the viewport may cause the 3ds to freeze (see SMW2 intro)
    if (targetUpdated) {
        if (GPU3DS.currentRenderTarget == TARGET_SCREEN) {
            gpu3dsSetRenderTargetToFrameBuffer();
        } else if (GPU3DS.currentRenderTarget != TARGET_SNES_MODE7_FULL) {
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
    if (GPU3DS.currentVbo != list->id)
    {
        //printf("(%d) update buffer\n", list->id);
        int totalAttributes = list->attrInfo.attrCount;
        u32 attributeFormats = list->attrInfo.flags[0];

        u64 vertexListAttribPermutations[1] = { list->attrInfo.permutation };
        u8 vertexListNumberOfAttribs[1] = { totalAttributes };
        u32 vertexListBufferOffsets[1] = { osConvertVirtToPhys(list->data) - BUFFER_BASE_PADDR };

        GPU_SetAttributeBuffers(
            totalAttributes,
            (u32*)BUFFER_BASE_PADDR,
            attributeFormats,
            0xFFFF,
            vertexListAttribPermutations[0],
            1,
            vertexListBufferOffsets,
            vertexListAttribPermutations,
            vertexListNumberOfAttribs
        );

        GPU3DS.currentVbo = list->id;
    }
}

void gpu3dsDraw(SVertexList *list, const void* indices, int count, int from = -1);

void gpu3dsCheckSlider();

#endif
