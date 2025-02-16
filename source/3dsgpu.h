
#ifndef _3DSGPU_H_
#define _3DSGPU_H_

#include <3ds.h>
#include <citro3d.h>
#include "gpulib.h"
#include "3dssnes9x.h"
#include "gfx.h"

#define FLAG_SHADER             BIT(0)
#define FLAG_TARGET             BIT(1)
#define FLAG_TEXTURE_BIND       BIT(2)
#define FLAG_TEXTURE_ENV        BIT(3)
#define FLAG_TEXTURE_OFFSET     BIT(4)
#define FLAG_STENCIL_TEST       BIT(5)
#define FLAG_DEPTH_TEST         BIT(6)
#define FLAG_ALPHA_TEST         BIT(7)
#define FLAG_ALPHA_BLENDING     BIT(8)
#define FLAG_UPDATE_FRAME       BIT(9)

// C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0) -> stencilMode = 16
// C3D_StencilTest(true, GPU_NEVER, 0, 0, 0) -> stencilMode = 1;
#define STENCIL_TEST_DISABLED 16
#define STENCIL_TEST_ENABLED_WINDOWING_DISABLED 1

typedef enum { 
    SPROGRAM_SCREEN, 
    SPROGRAM_TILES, 
    SPROGRAM_MODE7 
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
    SNES_MODE7_TILE_CACHE
} SGPU_TEXTURE_ID;

typedef enum 
{
    TEX_ENV_REPLACE_COLOR,
    TEX_ENV_REPLACE_TEXTURE0,
    TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA
} SGPU_TEX_ENV;

typedef enum 
{
    DEPTH_TEST_DISABLED,
    DEPTH_TEST_ENABLED
} SGPU_DEPTH_TEST;

typedef enum 
{
    ALPHA_TEST_DISABLED,
    ALPHA_TEST_NE_ZERO,
    ALPHA_TEST_GTE_HALF,
    ALPHA_TEST_GTE_FULL
} SGPU_ALPHA_TEST;

typedef enum 
{
    ALPHA_BLENDING_DISABLED,
    ALPHA_BLENDING_ENABLED,
    ALPHA_BLENDING_KEEP_DEST_ALPHA,
    ALPHA_BLENDING_ADD,
    ALPHA_BLENDING_ADD_DIV2,
    ALPHA_BLENDING_SUB,
    ALPHA_BLENDING_SUB_DIV2
} SGPU_ALPHA_BLENDINGMODE;

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
    SGPU_TEXTURE_ID         id;
    bool                    texInitialized;
    C3D_Tex                 tex;
	C3D_Mtx                 projection;
    float                   scale[4];
} SGPUTexture;

typedef struct {
	DVLB_s 				*dvlb;
	shaderProgram_s 	shaderProgram;
} SGPUShader;

typedef struct
{
    u8              TotalAttributes = 0;
    u64             AttributeFormats = 0;
    int             VertexSize = 0;
    int             SizeInBytes = 0;
    int             FirstIndex = 0;
    int             Total = 0;
    int             Count = 0;
    void            *ListOriginal;
    void            *List;
    void            *ListBase;
    int             Flip = 0;

    void            *PrevList;
    u32             *PrevListOSAddr;
    int             PrevFirstIndex = 0;
    int             PrevCount = 0;

} SVertexList;


typedef struct
{
    u8              TotalAttributes = 0;
    u64             AttributeFormats = 0;
    void            *List;
    int             Count = 0;
    GPU_Primitive_t Type;
} SStoredVertexList;


typedef struct
{
    SGPU_SHADER_PROGRAM         shader;
    SGPU_TARGET_ID              target;
    SGPU_TEXTURE_ID             textureBind;
    SGPU_TEX_ENV                textureEnv;
    u32                         stencilTest;
    SGPU_DEPTH_TEST             depthTest;
    SGPU_ALPHA_TEST             alphaTest;
	SGPU_ALPHA_BLENDINGMODE     alphaBlending;
    bool                        textureOffset; // false for sub-screen
    u32                         updateFrame;

    u32                         updated;
    u32                         initialized;
} SGPURenderState;

typedef struct
{
    GSPGPU_FramebufferFormat    screenFormat;
    GPU_TEXCOLOR                frameBufferFormat;

    void                *frameBuffer;
    void                *frameDepthBuffer;
    void                *textureDepthBuffer;

    C3D_Mtx             projectionTopScreen;
    C3D_Mtx             projectionBottomScreen;
    SStoredVertexList   vertexesStored[4][10];

    SGPUTexture         textures[8];

    u32                 *currentAttributeBuffer = 0;
    u8                  currentTotalAttributes = 0;
    u32                 currentAttributeFormats = 0;

    SGPUShader          shaders[3];
    s8                  shaderULocs[4];

    SGPURenderState     currentRenderState;

    bool                isReal3DS;
    bool                isNew3DS;
    bool                enableDebug = false;
    int                 emulatorState = 0;

} SGPU3DS;


extern SGPU3DS GPU3DS;

#define EMUSTATE_EMULATE        1
#define EMUSTATE_PAUSEMENU      2
#define EMUSTATE_END            3


bool gpu3dsInitialize();
void gpu3dsFinalize();

void gpu3dsAllocVertexList(SVertexList *list, int sizeInBytes, int vertexSize,
    u8 totalAttributes, u64 attributeFormats);
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
bool gpu3dsUseShader(SGPU_SHADER_PROGRAM shaderIndex);

void gpu3dsSetRenderTargetToFrameBuffer();
void gpu3dsSetRenderTargetToTexture(SGPU_TEXTURE_ID textureId);
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

void gpu3dsScissorTest(GPU_SCISSORMODE mode, uint32 x, uint32 y, uint32 w, uint32 h);

void gpu3dsEnableAlphaBlending();
void gpu3dsEnableAdditiveBlending();
void gpu3dsEnableSubtractiveBlending();
void gpu3dsEnableAdditiveDiv2Blending();
void gpu3dsEnableSubtractiveDiv2Blending();
void gpu3dsDisableAlphaBlending();
void gpu3dsDisableAlphaBlendingKeepDestAlpha();

bool gpu3dsUpdateRenderState(SGPURenderState* state, int propertyType, u32 newValue, u32 oldValue);

void gpu3dsDrawVertexList(SVertexList *list, GPU_Primitive_t type, bool repeatLastDraw, int storeVertexListIndex, int storeIndex);
void gpu3dsDrawVertexList(SVertexList *list, GPU_Primitive_t type, int fromIndex, int tileCount);

void gpu3dsCheckSlider();

#endif
