#include <3ds.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string.h>
#include <stdio.h>


#define _3DSGPU_CPP_
#include "snes9x.h"
#include "memmap.h"
#include "3dsgpu.h"
#include "3dsfiles.h"
#include "3dsimpl.h"
#include "3dssettings.h"

#ifndef M_PI
#define	M_PI		3.14159265358979323846
#endif

bool somethingWasDrawn = false;
bool somethingWasFlushed = false;

extern "C" u32 __ctru_linear_heap;
extern "C" u32 __ctru_linear_heap_size;

#define LINEARFREE_SAFE(x)  if (x) linearFree(x);


//------------------------------------------------------------------------
// Increased buffer size to 1MB for screens with heavy effects (multiple wavy backgrounds and line-by-line windows).
// Memory Usage = 2.00 MB   for GPU command buffer
#define COMMAND_BUFFER_SIZE             0x200000




u32 *gpuCommandBuffer1;
u32 *gpuCommandBuffer2;
int gpuCommandBufferSize = 0;
int gpuCurrentCommandBuffer = 0;
SGPU3DS GPU3DS;


static const u8 colorFmtSizes[] = {2,1,0,0,0}; // from citro3d framebuffer.c

size_t shader_count = (sizeof(GPU3DS.shaders)/sizeof(GPU3DS.shaders[0]));

//---------------------------------------------------------
// Enables / disables the parallax barrier
// Taken from RetroArch
//---------------------------------------------------------
void gpu3dsSetParallaxBarrier(bool enable)
{
   u32 reg_state = enable ? 0x00010001: 0x0;
   GSPGPU_WriteHWRegs(0x202000, &reg_state, 4);
}


//---------------------------------------------------------
// Sets the 2D screen mode based on the 3D slider.
// Taken from RetroArch.
//---------------------------------------------------------
float prevSliderVal = -1;
void gpu3dsCheckSlider()
{
    float sliderVal = *(float*)0x1FF81080;

    if (sliderVal != prevSliderVal)
    {
        if (sliderVal < 0.6)
        {
            gpu3dsSetParallaxBarrier(false);
        }
        else
        {
            gpu3dsSetParallaxBarrier(true);
        }

        gfxScreenSwapBuffers(GFX_TOP, false);
    }
    prevSliderVal = sliderVal;
}

void gpu3dsEnableDepthTest()
{
	GPU_SetDepthTestAndWriteMask(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

void gpu3dsDisableDepthTest()
{
	GPU_SetDepthTestAndWriteMask(false, GPU_ALWAYS, GPU_WRITE_ALL);
}


void gpu3dsEnableStencilTest(GPU_TESTFUNC func, u8 ref, u8 input_mask)
{
    GPU_SetStencilTest(true, func, ref, input_mask, 0);
}

void gpu3dsDisableStencilTest()
{
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0x00, 0x00);
}


void gpu3dsClearTextureEnv(u8 num)
{
	GPU_SetTexEnv(num,
		GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
		GPU_TEVSOURCES(GPU_PREVIOUS, 0, 0),
		GPU_TEVOPERANDS(0,0,0),
		GPU_TEVOPERANDS(0,0,0),
		GPU_REPLACE,
		GPU_REPLACE,
		0x80808080);
}

void gpu3dsSetTextureEnvironmentReplaceColor()
{
	GPU_SetTexEnv(
		0,
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR),
		GPU_TEVSOURCES(GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR),
		GPU_TEVOPERANDS(0, 0, 0),
		GPU_TEVOPERANDS(0, 0, 0),
		GPU_REPLACE, GPU_REPLACE,
		0x80808080
	);

	gpu3dsClearTextureEnv(1);
}

void gpu3dsSetTextureEnvironmentReplaceTexture0()
{
	GPU_SetTexEnv(
		0,
		GPU_TEVSOURCES(GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0),
		GPU_TEVSOURCES(GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0),
		GPU_TEVOPERANDS(0, 0, 0),
		GPU_TEVOPERANDS(0, 0, 0),
		GPU_REPLACE, GPU_REPLACE,
		0x80808080
	);

	gpu3dsClearTextureEnv(1);
}

void gpu3dsSetTextureEnvironmentReplaceTexture0WithColorAlpha()
{
	GPU_SetTexEnv(
		0,
		GPU_TEVSOURCES(GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0),
		GPU_TEVSOURCES(GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR),
		GPU_TEVOPERANDS(0, 0, 0),
		GPU_TEVOPERANDS(0, 0, 0),
		GPU_REPLACE, GPU_MODULATE,
		0x80808080
	);

	gpu3dsClearTextureEnv(1);
}


bool gpu3dsAllocVertexList(SVertexListInfo *info)
{
    SVertexList *list = &GPU3DS.vertices[info->id];

    if (list == NULL)
        return false;

    list->id = info->id;
    
    AttrInfo_Init(&list->attrInfo);
    
    for (size_t i = 0; i < info->totalAttributes; i++) {
        AttrInfo_AddLoader(&list->attrInfo, i, info->attrFormat[i].format, info->attrFormat[i].count);
    }
    
    list->data_base = linearAlloc(info->sizeInBytes);
    list->data = list->data_base;
    list->vertexSize = info->vertexSize;
    list->sizeInBytes = info->sizeInBytes;
    list->count = 0;
    list->from = 0;
    list->flip = 1;

    return true;
}

void gpu3dsDeallocVertexList(SVertexList *list)
{
    if (list == nullptr || list->data_base == nullptr)
        return;

    linearFree(list->data_base);
}

void gpu3dsSwapVertexListForNextFrame(SVertexList *list)
{
    if (list->flip)
        list->data = (void *)((u32)(list->data_base) + list->sizeInBytes / 2);
    else
        list->data = list->data_base;
    
    list->flip = 1 - list->flip;
    list->count = 0;
    list->from = 0;
}

void gpu3dsSetFragmentOperations(SGPURenderState *state, u32 flags) {
    // stencil test
    //
    if (flags & FLAG_STENCIL_TEST) {
        switch (state->stencilTest)
        {
            case STENCIL_TEST_DISABLED:
                gpu3dsDisableStencilTest();
                break;
            case STENCIL_TEST_ENABLED_WINDOWING_DISABLED:
                gpu3dsEnableStencilTest(GPU_NEVER, 0, 0);
                break;
            default:
                GPU_TESTFUNC func = (GPU_TESTFUNC)((state->stencilTest >> 4) & 7);
                int ref = (state->stencilTest >> 16) & 0xFF;
                int inputMask = (state->stencilTest >> 24) & 0xFF;
                gpu3dsEnableStencilTest(func, ref, inputMask);
                break;
        }
    }

    // depth test
    //
    if (flags & FLAG_DEPTH_TEST) {
        if (GPU3DS.depthTestEnabled) 
            gpu3dsEnableDepthTest();
        else
            gpu3dsDisableDepthTest();
    }

    // alpha test
    //
    if (flags & FLAG_ALPHA_TEST) {
        switch (state->alphaTest)
        {
            case ALPHA_TEST_DISABLED:
                gpu3dsDisableAlphaTest();
                break;
            case ALPHA_TEST_NE_ZERO:
                gpu3dsEnableAlphaTestNotEqualsZero();
                break;
            default:
                gpu3dsEnableAlphaTestGreaterThanEquals(state->alphaTest == ALPHA_TEST_GTE_0_5 ? 0x7f : 0x0f);
                break;
        }
    }

    // alpha blending
    //
    if (flags & FLAG_ALPHA_BLENDING) {
        switch (state->alphaBlending)
        {
            case ALPHA_BLENDING_ENABLED:
                gpu3dsEnableAlphaBlending();
                break;
            case ALPHA_BLENDING_KEEP_DEST_ALPHA:
                gpu3dsDisableAlphaBlendingKeepDestAlpha();
                break;
            case ALPHA_BLENDING_ADD:
                gpu3dsEnableAdditiveBlending();
                break;
            case ALPHA_BLENDING_ADD_DIV2:
                gpu3dsEnableAdditiveDiv2Blending();
                break;
            case ALPHA_BLENDING_SUB:
                gpu3dsEnableSubtractiveBlending();
                break;
            case ALPHA_BLENDING_SUB_DIV2:
                gpu3dsEnableSubtractiveDiv2Blending();
                break;
            default:
                gpu3dsDisableAlphaBlending();
                break;
        }
    }
}

void gpu3dsSetShaderAndUniforms(SGPURenderState *state, u32 flags, bool targetUpdated, bool textureUpdated) {
    if (flags & FLAG_SHADER) { 
        shaderProgramUse(&GPU3DS.shaders[GPU3DS.currentShader].shaderProgram);
    }

    // when render target has been updated, we need to update our projection uniforms as well
    if (targetUpdated) {
        if (GPU3DS.currentRenderTarget == TARGET_SCREEN) {
            u32 *projection = (screenSettings.GameScreen == GFX_TOP) ? (u32 *)GPU3DS.projectionTopScreen.m : (u32 *)GPU3DS.projectionBottomScreen.m;
            GPU_SetFloatUniform(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_PROJECTION], projection, 4);
        } else {
            SGPUTexture *targetFromTex = &GPU3DS.textures[(SGPU_TEXTURE_ID)GPU3DS.currentRenderTarget];

            if (targetFromTex) {
                // projection for mode7 targets is handled in mode7 geometry shader
                GPU_SHADER_TYPE shaderType = (GPU3DS.currentRenderTarget != TARGET_SNES_MODE7_TILE_0 && GPU3DS.currentRenderTarget != TARGET_SNES_MODE7_FULL) ? 
                GPU_VERTEX_SHADER : GPU_GEOMETRY_SHADER;

                GPU_SetFloatUniform(shaderType, GPU3DS.shaderULocs[ULOC_PROJECTION], (u32 *)targetFromTex->projection.m, 4);
            }
        }
    }

    if (textureUpdated)
    {
        SGPUTexture *texture = &GPU3DS.textures[state->textureBind];
        GPU_SHADER_TYPE shaderType = texture->id == SNES_MODE7_TILE_CACHE ? GPU_GEOMETRY_SHADER : GPU_VERTEX_SHADER;
        GPU_SetFloatUniform(shaderType, GPU3DS.shaderULocs[ULOC_TEX_SCALE], (u32 *)texture->scale, 1);
    }

    if (GPU3DS.currentShader == SPROGRAM_TILES && (flags & FLAG_TEXTURE_OFFSET)) {
        float textureOffset[4] = {0.0f, 0.0f, 0.0f, state->textureOffset ? 1.0f : 0.0f};
        GPU_SetFloatUniform(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_TEX_OFFSET], (u32 *)textureOffset, 1);  
    }
    
    if (GPU3DS.currentShader == SPROGRAM_MODE7 && (flags & FLAG_UPDATE_FRAME)) {
        float updateFrame[4] = {(float)GPU3DSExt.mode7FrameCount, 0.0f, 0.0f, 0.0f};
        GPU_SetFloatUniform(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_UPDATE_FRAME], (u32 *)updateFrame, 1);
    }
}

void gpu3dsApplyRenderState(SGPURenderState *state)
{    
    u32 flags = GPU3DS.currentRenderStateFlags;

    if (!flags) {
        return;
    }
    
    bool targetUpdated = flags & FLAG_TARGET;

    // update viewport
    // ! order seems important here !
    // binding the shader before setting the viewport, may cause the 3ds to freeze (see SMW2 intro)
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

void gpu3dsDrawVertexList(SVertexList *list, int layer, GPU_Primitive_t primitive)
{
    int fromIndex = list->from;
    int currentVerticesCount = list->count;
    
    if (GPU3DS.currentRenderStateFlags) {
        gpu3dsApplyRenderState(&GPU3DS.currentRenderState);
    }
    
    gpu3dsSetAttributeBuffers(&list->attrInfo, list->data);
    GPU_DrawArray(primitive, fromIndex, currentVerticesCount);

    somethingWasDrawn = true;
    
    list->from += currentVerticesCount;
    list->count = 0;
}

void gpu3dsDrawMode7Vertices(int fromIndex, int tileCount)
{
    GPU_DrawArray(GPU_GEOMETRY_PRIM, fromIndex, tileCount);
    
    somethingWasDrawn = true;
}

// may give us false positives, but works at least for citra nightly 1989 (mac)
bool isReal3DS() {
    Result ret = 0;
    OS_VersionBin *nver = new OS_VersionBin[sizeof(OS_VersionBin)];
    OS_VersionBin *cver = new OS_VersionBin[sizeof(OS_VersionBin)];
    static char systemVersionString[128];
    
    if (R_FAILED(ret = osGetSystemVersionDataString(nver, cver, systemVersionString, sizeof(systemVersionString)))) {
        return false;
    }

    return true;
}

bool gpu3dsInitialize()
{

    // Initialize the 3DS screen
    //
    GPU3DS.screenFormat = GSP_RGBA8_OES;
    gfxInit(GPU3DS.screenFormat, GPU3DS.screenFormat, false);
	gfxSet3D(false);
    APT_CheckNew3DS(&GPU3DS.isNew3DS);

    // Create the frame and depth buffers for the main screen.
    //
    GPU3DS.frameBufferFormat = GPU_RGBA8;
	GPU3DS.frameBuffer = vramAlloc(SCREEN_TOP_WIDTH * SCREEN_HEIGHT * 8);
	GPU3DS.frameDepthBuffer = vramAlloc(SCREEN_TOP_WIDTH * SCREEN_HEIGHT * 8);
    if (GPU3DS.frameBuffer == NULL ||
        GPU3DS.frameDepthBuffer == NULL)
    {
        printf ("Unable to allocate frame/depth buffers\n");
        return false;
    }

    // Initialize the sub screen for console output.
    //
    consoleInit(screenSettings.SecondScreen, NULL);

    // Create the command buffers
    //
    gpuCommandBufferSize = COMMAND_BUFFER_SIZE;
    gpuCommandBuffer1 = (u32 *)linearAlloc(COMMAND_BUFFER_SIZE / 2);
    gpuCommandBuffer2 = (u32 *)linearAlloc(COMMAND_BUFFER_SIZE / 2);
    if (gpuCommandBuffer1 == NULL || gpuCommandBuffer2 == NULL)
        return false;
	GPUCMD_SetBuffer(gpuCommandBuffer1, gpuCommandBufferSize, 0);
    gpuCurrentCommandBuffer = 0;

#ifndef RELEASE
    printf ("Buffer: %8x\n", (u32) gpuCommandBuffer1);
#endif

    GPU3DS.isReal3DS = isReal3DS();
    GPU3DS.currentRenderState = {0};

    // Initialize the projection matrix for the top / bottom
    // screens
    //
    Mtx_OrthoTilt(&GPU3DS.projectionTopScreen, 0.0f, 400.0f, 240.0f, 0.0f, 0.0f, 1.0f, true);
    Mtx_OrthoTilt(&GPU3DS.projectionBottomScreen, 0.0f, 320.0f, 240.0f, 0.0f, 0.0f, 1.0f, true);

    // Initialize all shaders to empty
    //
    for (int i = 0; i < shader_count; i++)
    {
        GPU3DS.shaders[i].dvlb = NULL;
    }

#ifndef RELEASE
    printf ("gpu3dsInitialize - Allocate buffers\n");
#endif

#ifndef RELEASE
    printf ("gpu3dsInitialize - Set GPU statuses\n");
#endif

	GPU_DepthMap(-1.0f, 0.0f);
	GPUCMD_AddMaskedWrite(GPUREG_EARLYDEPTH_TEST1, 0x1, 0);
	GPUCMD_AddWrite(GPUREG_EARLYDEPTH_TEST2, 0);
	GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG, GPU_CULL_NONE&0x3);
	GPU_SetStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    GPUCMD_AddWrite(GPUREG_TEXUNIT0_BORDER_COLOR, 0);

    gpu3dsFlush();
    gpu3dsWaitForPreviousFlush();

    return true;
}


void gpu3dsFinalize()
{
    // Bug fix: Free up all shaders' DVLB
    //
    // Initialize all shaders to empty
    //
    for (int i = 0; i < shader_count; i++)
    {
        if (GPU3DS.shaders[i].dvlb)
            DVLB_Free(GPU3DS.shaders[i].dvlb);
    }

    // Bug fix: free the frame buffers!
    if (GPU3DS.frameBuffer) vramFree(GPU3DS.frameBuffer);
    if (GPU3DS.frameDepthBuffer) vramFree(GPU3DS.frameDepthBuffer);

    LINEARFREE_SAFE(gpuCommandBuffer1);
    LINEARFREE_SAFE(gpuCommandBuffer2);

#ifndef RELEASE
    printf("gfxExit:\n");
#endif

	gfxExit();
}

void gpu3dsEnableAlphaTestNotEqualsZero()
{
    GPU_SetAlphaTest(true, GPU_NOTEQUAL, 0x00);
}

void gpu3dsEnableAlphaTestGreaterThanEquals(uint8 alpha)
{
    GPU_SetAlphaTest(true, GPU_GEQUAL, alpha);
}

void gpu3dsDisableAlphaTest()
{
    GPU_SetAlphaTest(false, GPU_NOTEQUAL, 0x00);
}

u32 gpu3dsGetNextPowerOf2(u32 v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;

    v++;

    const u32 min = 8;

    return (!v || v >= min ? v : min);
}

int gpu3dsGetPixelSize(GPU_TEXCOLOR pixelFormat)
{
    if (pixelFormat == GPU_RGBA8)
        return 4;
    if (pixelFormat == GPU_RGB8)
        return 3;
    if (pixelFormat == GPU_RGBA5551)
        return 2;
    if (pixelFormat == GPU_RGB565)
        return 2;
    if (pixelFormat == GPU_RGBA4)
        return 2;
    return 0;
}

bool gpu3dsInitTexture(SGPUTextureConfig *config)
{
    SGPUTexture *texture = &GPU3DS.textures[config->id];
    texture->id = config->id;
    u32 w_pow2 = gpu3dsGetNextPowerOf2(config->width);
    u32 h_pow2 = gpu3dsGetNextPowerOf2(config->height);

	if (config->onVram) {
		texture->texInitialized = C3D_TexInitVRAM(&texture->tex, w_pow2, h_pow2, config->format);
	} else {
		config->hasTarget = false; // Render targets must be in VRAM
		texture->texInitialized = C3D_TexInit(&texture->tex, w_pow2, h_pow2, config->format);	
	}

	if (!texture->texInitialized) {
		return false;
	}

    texture->tex.param = config->param;

    texture->scale[3] = 1.0f / config->width;  // x
    texture->scale[2] = 1.0f / config->height; // y
    texture->scale[1] = 0; // z
    texture->scale[0] = 0; // w


    void *texImage = C3D_Tex2DGetImagePtr(&texture->tex, 0, NULL);

	if (!config->onVram) {
        memset(texImage, 0, texture->tex.size);
        
        return true;
    }

    // (citra only?) fixes broken mode7 texture on f-zero start screen 
    // (and probably other games)
    gpu3dsClearTexture(texture, 0);

    // 3DS does not allow rendering to a viewport whose width > 512.
    int maxViewportWidth = 512;
    
    int vpWidth = w_pow2 > maxViewportWidth ? maxViewportWidth : w_pow2;
    int vpHeight = h_pow2 > maxViewportWidth ? maxViewportWidth : h_pow2;

	// bubble2k's orthographic implementation had some adjustments for 0xA and 0xB (see 3dsmatrix.cpp in older versions)
    // which seem required for the shader logic in shaderfast and shaderfastm7
	// We do this here as well to match the projection matrix, because pica shader debugging is way more challenging + time consuming
    float near = 0.0f;
    float far = 1.0f;
    Mtx_Ortho(&texture->projection, 0.0f, vpWidth, 0.0f, vpHeight, near, far, true);
    texture->projection.m[8] = near / (far - near);
    texture->projection.m[9] = 1 / (near - far);

    return true;
}

void gpu3dsClearTexture(SGPUTexture *texture, u32 color) {
    if (texture == nullptr)
        return;

    void *texImage = C3D_Tex2DGetImagePtr(&texture->tex, 0, NULL);

    C3D_SyncMemoryFill(
        (u32 *)texImage, color, (u32 *)((u8 *)texImage + texture->tex.size), 
        BIT(0) | (colorFmtSizes[texture->tex.fmt] << 8), NULL, 0, NULL, 0);
}

void gpu3dsDestroyTexture(SGPUTexture *texture)
{
    if (texture == nullptr) {
        return;
    }

    C3D_TexDelete(&texture->tex);
}

void gpu3dsStartNewFrame()
{
    //if (GPU3DS.enableDebug)
    //    printf("  gpu3dsStartNewFrame\n");

    gpuCurrentCommandBuffer = 1 - gpuCurrentCommandBuffer;

    impl3dsPrepareForNewFrame();

    if (gpuCurrentCommandBuffer == 0)
    {
	    GPUCMD_SetBuffer(gpuCommandBuffer1, gpuCommandBufferSize, 0);
    }
    else
    {
	    GPUCMD_SetBuffer(gpuCommandBuffer2, gpuCommandBufferSize, 0);
    }
}

bool gpu3dsInitializeShaderUniformLocations()
{
    GPU3DS.shaderULocs[ULOC_PROJECTION] = shaderInstanceGetUniformLocation(GPU3DS.shaders[SPROGRAM_TILES].shaderProgram.vertexShader, "projection");
    GPU3DS.shaderULocs[ULOC_TEX_SCALE] = shaderInstanceGetUniformLocation(GPU3DS.shaders[SPROGRAM_TILES].shaderProgram.vertexShader, "textureScale");
    
    // used by tiles vertex shader
    GPU3DS.shaderULocs[ULOC_TEX_OFFSET] = shaderInstanceGetUniformLocation(GPU3DS.shaders[SPROGRAM_TILES].shaderProgram.vertexShader, "textureOffset");
    
    // used by m7 vertex shader
    GPU3DS.shaderULocs[ULOC_UPDATE_FRAME] = shaderInstanceGetUniformLocation(GPU3DS.shaders[SPROGRAM_MODE7].shaderProgram.vertexShader, "updateFrame");

	bool uLocsInvalid = false;

    for (int i = 0; i < 4; i++) {
        if (GPU3DS.shaderULocs[i] == -1) {
            uLocsInvalid = true;
            break;
        }
    }
    
    return !uLocsInvalid;
}

void gpu3dsLoadShader(SGPU_SHADER_PROGRAM shaderIndex, u32 *shaderBinary,
    int size, int geometryShaderStride)
{
	GPU3DS.shaders[shaderIndex].dvlb = DVLB_ParseFile((u32 *)shaderBinary, size);

	shaderProgramInit(&GPU3DS.shaders[shaderIndex].shaderProgram);
	shaderProgramSetVsh(&GPU3DS.shaders[shaderIndex].shaderProgram,
        &GPU3DS.shaders[shaderIndex].dvlb->DVLE[0]);

	if (geometryShaderStride)
    {
		shaderProgramSetGsh(&GPU3DS.shaders[shaderIndex].shaderProgram,
			&GPU3DS.shaders[shaderIndex].dvlb->DVLE[1], geometryShaderStride);
    }
}

void gpu3dsEnableAlphaBlending()
{
	GPU_SetAlphaBlending(
		GPU_BLEND_ADD,
		GPU_BLEND_ADD,
		GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
		GPU_ONE, GPU_ZERO
	);
}

void gpu3dsDisableAlphaBlending()
{
	GPU_SetAlphaBlending(
		GPU_BLEND_ADD,
		GPU_BLEND_ADD,
		GPU_ONE, GPU_ZERO,
		GPU_ONE, GPU_ZERO
	);
}

void gpu3dsDisableAlphaBlendingKeepDestAlpha()
{
    GPU_SetAlphaBlending(
        GPU_BLEND_ADD,
        GPU_BLEND_ADD,
        GPU_ONE, GPU_ZERO,
        GPU_ZERO, GPU_ONE
    );
}

void gpu3dsEnableAdditiveBlending()
{
	GPU_SetAlphaBlending(
		GPU_BLEND_ADD,
		GPU_BLEND_ADD,
		GPU_DST_ALPHA, GPU_ONE,
		GPU_ONE, GPU_ZERO
	);
}

void gpu3dsEnableSubtractiveBlending()
{
	GPU_SetAlphaBlending(
		GPU_BLEND_REVERSE_SUBTRACT,
		GPU_BLEND_ADD,
		GPU_DST_ALPHA, GPU_ONE,
		GPU_ONE, GPU_ZERO
	);
}

void gpu3dsEnableAdditiveDiv2Blending()
{
    GPU_SetBlendingColor(0, 0, 0, 0xff);
	GPU_SetAlphaBlending(
		GPU_BLEND_ADD,
		GPU_BLEND_ADD,
		GPU_DST_ALPHA, GPU_ONE_MINUS_DST_ALPHA,
		GPU_ONE, GPU_ZERO
	);
}

void gpu3dsEnableSubtractiveDiv2Blending()
{
    GPU_SetBlendingColor(0, 0, 0, 0xff);
	GPU_SetAlphaBlending(
		GPU_BLEND_REVERSE_SUBTRACT,
		GPU_BLEND_ADD,
		GPU_DST_ALPHA, GPU_ONE_MINUS_DST_ALPHA,
		GPU_ONE, GPU_ZERO
	);
}

void gpu3dsResetState()
{
    gpu3dsResetLayerSectionLimits(&GPU3DSExt.layerList);

    GPU3DS.currentShader = SPROGRAM_UNSET;
    GPU3DS.currentRenderTarget = TARGET_UNSET;
    GPU3DS.depthTestEnabled = false;
    gpu3dsDisableDepthTest();
    
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_UNSET;
    GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_UNSET;
    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_UNSET;
    
    GPU3DS.currentRenderStateFlags = 0;

	gpu3dsClearTextureEnv(1);
	gpu3dsClearTextureEnv(2);
	gpu3dsClearTextureEnv(3);
	gpu3dsClearTextureEnv(4);
	gpu3dsClearTextureEnv(5);

    gpu3dsFlush();
    gpu3dsWaitForPreviousFlush();
}


/*
The following array is based on
    https://www.3dbrew.org/wiki/GPU/Internal_Registers#GPUREG_COLORBUFFER_FORMAT and
supports only the following frame buffer format types:

  GPU_RGBA8 = 0x0,
  GPU_RGB8 = 0x1,
  GPU_RGBA5551 = 0x2,
  GPU_RGB565 = 0x3,
  GPU_RGBA4 = 0x4
*/
const uint32 GPUREG_COLORBUFFER_FORMAT_VALUES[5] = { 0x0002, 0x00010001, 0x00020000, 0x00030000, 0x00040000 };

void gpu3dsSetRenderTargetToFrameBuffer()
{
    GPU_SetViewport(    
        (u32 *)osConvertVirtToPhys(GPU3DS.frameDepthBuffer),
        (u32 *)osConvertVirtToPhys(GPU3DS.frameBuffer),
        0, 0, SCREEN_HEIGHT, (screenSettings.GameScreen == GFX_TOP) ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH);

    GPUCMD_AddSingleParam(0x000F0117, GPUREG_COLORBUFFER_FORMAT_VALUES[GPU3DS.frameBufferFormat]);
}

void gpu3dsSetRenderTargetToTexture(SGPU_TARGET_ID target)
{
    SGPUTexture *texture = &GPU3DS.textures[target];

    void *texColorImagePtr = C3D_Tex2DGetImagePtr(&texture->tex, 0, NULL);
    void *texDepthImagePtr = (target == TARGET_SNES_MAIN || target == TARGET_SNES_SUB) ? C3D_Tex2DGetImagePtr(&GPU3DS.textures[SNES_DEPTH].tex, 0, NULL) : NULL;
    
    u32 *colorBuf = (u32 *)osConvertVirtToPhys(texColorImagePtr);
    u32 *depthBuf = (u32 *)osConvertVirtToPhys(texDepthImagePtr);
    
    GPU_SetViewport(depthBuf, colorBuf, 0, 0, texture->tex.width, texture->tex.height);
    GPUCMD_AddSingleParam(0x000F0117, GPUREG_COLORBUFFER_FORMAT_VALUES[texture->tex.fmt]);
}

void gpu3dsSetRenderTargetToMode7Texture(u32 pixelOffset)
{
    SGPUTexture *texture = &GPU3DS.textures[SNES_MODE7_FULL];
    void *texColorImagePtr = C3D_Tex2DGetImagePtr(&texture->tex, 0, NULL);

    // mode7 shader on citra seems to behave differently than on real device.
    // if we draw all 4 sections, mode7 texture is not visible.
    // we can still test it somehow by rendering only a part of the sections.
    // e.g. skip sectionIndex 1 for Yoshi's Island to see mode7 graphics on title screen
    if (!GPU3DS.isReal3DS && pixelOffset == 1 * 0x40000)
    {
        pixelOffset = 0;
    }

    int addressOffset = pixelOffset * gpu3dsGetPixelSize(texture->tex.fmt);

    u32 *colorBuf = (u32 *)osConvertVirtToPhys((void *)((int)texColorImagePtr + addressOffset));
    
    // 3DS does not allow rendering to a viewport whose width > 512
    // so our 1024x1024 texture is split into 4 512x512 parts
    GPU_SetViewport(NULL, colorBuf, 0, 0, 512, 512);
    GPUCMD_AddSingleParam(0x000F0117, GPUREG_COLORBUFFER_FORMAT_VALUES[texture->tex.fmt]); //color buffer format
}


void gpu3dsFlush()
{
	u32* commandBuffer;
	u32  commandBuffer_size;
    
	if(somethingWasDrawn) {
	    GPUCMD_AddMaskedWrite(GPUREG_PRIMITIVE_CONFIG, 0x8, 0x00000000);
	    GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_FLUSH, 0x00000001);
	    GPUCMD_AddWrite(GPUREG_FRAMEBUFFER_INVALIDATE, 0x00000001);
    }
    
	GPUCMD_Split(&commandBuffer, &commandBuffer_size);
	GX_FlushCacheRegions (commandBuffer, commandBuffer_size * 4, (u32 *) __ctru_linear_heap, __ctru_linear_heap_size, NULL, 0);
	GX_ProcessCommandList(commandBuffer, commandBuffer_size * 4, 0x00);
    
    somethingWasFlushed = true;
    somethingWasDrawn = false;
}

void gpu3dsWaitForPreviousFlush()
{
    if (somethingWasFlushed)
    {
        gspWaitForP3D();
        somethingWasFlushed = false;
    }

}

/*
Translate from the following GPU_TEXCOLOR to their respective GX_TRANSFER_FMT values.
  GPU_RGBA8 = 0x0,
  GPU_RGB8 = 0x1,
  GPU_RGBA5551 = 0x2,
  GPU_RGB565 = 0x3,
  GPU_RGBA4 = 0x4
*/
const uint32 GX_TRANSFER_FRAMEBUFFER_FORMAT_VALUES[5] = {
    GX_TRANSFER_FMT_RGBA8, GX_TRANSFER_FMT_RGB8, GX_TRANSFER_FMT_RGB5A1, GX_TRANSFER_FMT_RGB565, GX_TRANSFER_FMT_RGBA4 };

/*
Translate from the following GSPGPU_FramebufferFormat to their respective GX_TRANSFER_FMT values:
  GSP_RGBA8_OES =0,
  GSP_BGR8_OES =1,
  GSP_RGB565_OES =2,
  GSP_RGB5_A1_OES =3,
  GSP_RGBA4_OES =4
*/
const uint32 GX_TRANSFER_SCREEN_FORMAT_VALUES[5]= {
    GX_TRANSFER_FMT_RGBA8, GX_TRANSFER_FMT_RGB8, GX_TRANSFER_FMT_RGB565, GX_TRANSFER_FMT_RGB5A1, GX_TRANSFER_FMT_RGBA4 };


void gpu3dsTransferToScreenBuffer(gfxScreen_t screen)
{
    int screenWidth = (screen == screenSettings.GameScreen) ? screenSettings.GameScreenWidth : screenSettings.SecondScreenWidth;
    gpu3dsWaitForPreviousFlush();
    GX_DisplayTransfer((u32 *)GPU3DS.frameBuffer, GX_BUFFER_DIM(SCREEN_HEIGHT, screenWidth),
        (u32 *)gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL),
        GX_BUFFER_DIM(SCREEN_HEIGHT, screenWidth),
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FRAMEBUFFER_FORMAT_VALUES[GPU3DS.frameBufferFormat]) |
        GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_SCREEN_FORMAT_VALUES[GPU3DS.screenFormat]));
}

void gpu3dsSwapScreenBuffers()
{
    gfxScreenSwapBuffers(GFX_TOP, false);
    gfxScreenSwapBuffers(GFX_BOTTOM, false);
}


void gpu3dsBindTexture(SGPU_TEXTURE_ID textureId)
{
    SGPUTexture *texture = &GPU3DS.textures[textureId];
    u32 param;
    GPU_TEXTURE_FILTER_PARAM filter;
    GPU_TEXTURE_WRAP_PARAM wrap;
    
    // texture params are dynamic for main and mode7 texture
    switch (textureId)
    {
        case SNES_MAIN:
            filter = (settings3DS.ScreenStretch == 0 || settings3DS.StretchHeight == -1) ? GPU_NEAREST : GPU_TEXTURE_FILTER_PARAM(settings3DS.ScreenFilter);
	        param = GPU_TEXTURE_MAG_FILTER(filter) | GPU_TEXTURE_MIN_FILTER(filter) | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_BORDER) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_BORDER);
            break;
        case SNES_MODE7_FULL:
            wrap = PPU.Mode7Repeat == 0 ? GPU_REPEAT : GPU_CLAMP_TO_BORDER;
            param = GPU_TEXTURE_MAG_FILTER(GPU_NEAREST) | GPU_TEXTURE_MIN_FILTER(GPU_NEAREST) | GPU_TEXTURE_WRAP_S(wrap) | GPU_TEXTURE_WRAP_T(wrap);
            break;
        default:
            param = texture->tex.param;
            break;
    }

    GPU_SetTextureEnable(GPU_TEXUNIT0);

    GPU_SetTexture(
        GPU_TEXUNIT0,
        (u32 *)osConvertVirtToPhys(texture->tex.data),
        texture->tex.width,
        texture->tex.height,
        param,
        texture->tex.fmt
    );
}