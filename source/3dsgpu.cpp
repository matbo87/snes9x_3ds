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
#include "3dslog.h"

SGPU3DS GPU3DS;

static const u8 colorFmtSizes[] = {2,1,0,0,0}; // from citro3d framebuffer.c

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
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
}

void gpu3dsDisableDepthTest()
{
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
}


void gpu3dsEnableStencilTest(GPU_TESTFUNC func, u8 ref, u8 input_mask)
{
    C3D_StencilTest(true, func, ref, input_mask, 0);
}

void gpu3dsDisableStencilTest()
{
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0, 0);
}


void gpu3dsClearTextureEnv(u8 num)
{
	C3D_TexEnvInit(C3D_GetTexEnv(num));
}

void gpu3dsSetTextureEnvironmentReplaceColor()
{
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

	gpu3dsClearTextureEnv(1);
}

void gpu3dsSetTextureEnvironmentBlendColorOnTexture() {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);

	gpu3dsClearTextureEnv(1);
}

void gpu3dsSetTextureEnvironmentReplaceTexture0()
{
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

	gpu3dsClearTextureEnv(1);
}

void gpu3dsSetTextureEnvironmentReplaceTexture0WithColorAlpha()
{
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);

	gpu3dsClearTextureEnv(1);
}


bool gpu3dsAllocVertexList(SVertexListInfo *info)
{
    SVertexList *list = &GPU3DS.vertices[info->id];

    if (list == NULL)
        return false;

    list->id = info->id;
    list->primitive = list->id != VBO_SCREEN ? GPU_GEOMETRY_PRIM : GPU_TRIANGLES;
    
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
    list->count = 0;
    list->from = 0;
}

void gpu3dsSetDefaultRenderState(SGPU_SHADER_PROGRAM shader, bool newFrame, bool isSecondaryScreen) {
	SGPU_TARGET_ID target;
	SGPU_TEX_ENV texEnv;

	if (shader == SPROGRAM_TILES) {
		target = TARGET_SNES_MAIN;
		texEnv = TEX_ENV_REPLACE_COLOR;
	} else {
		target = isSecondaryScreen ? TARGET_SCREEN_SECONDARY : TARGET_SCREEN_PRIMARY;
		texEnv = TEX_ENV_REPLACE_TEXTURE0;
	}

	if (GPU3DS.currentRenderTarget != target || newFrame) 
	{
		GPU3DS.currentRenderTarget = target;
		GPU3DS.currentRenderStateFlags |= FLAG_TARGET;
	}
	
	if (GPU3DS.currentShader != shader) 
	{
		GPU3DS.currentShader = shader;
		GPU3DS.currentRenderStateFlags |= FLAG_SHADER;
	}

	if (GPU3DS.depthTestEnabled) 
	{
		GPU3DS.depthTestEnabled = false;
		GPU3DS.currentRenderStateFlags |= FLAG_DEPTH_TEST;
	}

	SGPURenderState renderState = GPU3DS.currentRenderState;

	renderState.textureEnv = texEnv;
	renderState.stencilTest = STENCIL_TEST_DISABLED;
	renderState.alphaTest = ALPHA_TEST_DISABLED;
	renderState.alphaBlending = ALPHA_BLENDING_DISABLED;

	u32 flags = FLAG_TEXTURE_ENV
			| FLAG_STENCIL_TEST
			| FLAG_ALPHA_TEST
			| FLAG_ALPHA_BLENDING;

	gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, flags, &renderState);
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
    bool shaderUpdated = flags & FLAG_SHADER;

    if (shaderUpdated) { 
        C3D_BindProgram(&GPU3DS.shaders[GPU3DS.currentShader].shaderProgram);
        GPU3DS.currentRenderTargetDim = 0;
        GPU3DS.currentTextureDim = 0;
    }

    // set projection
    if (targetUpdated || shaderUpdated) 
    {
        C3D_Mtx projection;

        if (GPU3DS.currentRenderTarget == TARGET_SCREEN_PRIMARY || GPU3DS.currentRenderTarget == TARGET_SCREEN_SECONDARY) {
            gfxScreen_t screen = GPU3DS.currentRenderTarget == TARGET_SCREEN_PRIMARY ? screenSettings.GameScreen : screenSettings.SecondScreen;
            C3D_Mtx projection = (screen == GFX_TOP) ? GPU3DS.projectionTopScreen : GPU3DS.projectionBottomScreen;
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_PROJECTION], &projection);
        } else {
            SGPUTexture *targetFromTex = &GPU3DS.textures[(SGPU_TEXTURE_ID)GPU3DS.currentRenderTarget];

            if (targetFromTex->tex.dim != GPU3DS.currentRenderTargetDim) {
                C3D_FVUnifMtx4x4(GPU_GEOMETRY_SHADER, GPU3DS.shaderULocs[ULOC_PROJECTION], &targetFromTex->projection);
                
                GPU3DS.currentRenderTargetDim = targetFromTex->tex.dim;
            }
        }
    }

    // set textureScale
    if (textureUpdated) 
    {
        SGPUTexture *texture = &GPU3DS.textures[state->textureBind];
        GPU_SHADER_TYPE shaderType = GPU3DS.currentShader != SPROGRAM_SCREEN ? GPU_GEOMETRY_SHADER : GPU_VERTEX_SHADER;

        if (GPU3DS.currentTextureDim != texture->tex.dim) {
            C3D_FVUnifSet(shaderType, GPU3DS.shaderULocs[ULOC_TEX_SCALE], texture->scale[3], texture->scale[2], texture->scale[1], texture->scale[0]);
            
            GPU3DS.currentTextureDim = texture->tex.dim;
        }
    }

    if (GPU3DS.currentShader == SPROGRAM_TILES && (flags & FLAG_TEXTURE_OFFSET)) {
        float textureOffset[4] = {0.0f, 0.0f, 0.0f, state->textureOffset ? 1.0f : 0.0f}; // wzyx
        C3D_FVUnifSet(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_TEX_OFFSET], textureOffset[3], textureOffset[2], textureOffset[1], textureOffset[0]);
    }
    
    if (GPU3DS.currentShader == SPROGRAM_MODE7 && (flags & FLAG_UPDATE_FRAME)) {
        float updateFrame[4] = {(float)GPU3DSExt.mode7FrameCount, 0.0f, 0.0f, 0.0f}; // wzyx
        C3D_FVUnifSet(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_UPDATE_FRAME], updateFrame[3], updateFrame[2], updateFrame[1], updateFrame[0]);
    }
}


void gpu3dsDraw(SVertexList *list, const void* indices, int count, int from) {
    gpu3dsApplyRenderState(&GPU3DS.currentRenderState);
    gpu3dsSetAttributeBuffers(list);

    if (indices != NULL) {
        C3D_DrawElements(list->primitive, count, C3D_UNSIGNED_SHORT, indices);
    } else if (from >= 0) {
        C3D_DrawArrays(list->primitive, from, count);
    } else {
        C3D_DrawArrays(list->primitive, list->from, list->count);

        list->from += list->count;
        list->count = 0;
    }
}

bool gpu3dsFrameBegin(u8 flags, bool ingame, bool isSecondaryScreen)
{
    GPU3DS.gpuSwapPending = false;

    if (!C3D_FrameBegin(flags)) {
        return false;
    }

	impl3dsPrepareForNewFrame(ingame);
	gpu3dsSetDefaultRenderState(ingame ? SPROGRAM_TILES : SPROGRAM_SCREEN, true, isSecondaryScreen);

    return true;
}

void gpu3dsFrameEnd(u8 flags)
{
    C3D_FrameEnd(flags);

    GPU3DS.gpuSwapPending = true;
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

bool gpu3dsClearScreen(gfxScreen_t screen, bool isTopStereo) {
	SCREEN_TARGET targetId = screen == GFX_TOP ? SCREEN_TARGET_LEFT : SCREEN_TARGET_BOTTOM;

	if (!C3D_FrameDrawOn(GPU3DS.screenTargets[targetId])) {
		return false;
	}

	C3D_RenderTargetClear(GPU3DS.screenTargets[targetId], C3D_CLEAR_COLOR, 0, 0);

	if (isTopStereo && screen != GFX_BOTTOM) {
		C3D_RenderTargetClear(GPU3DS.screenTargets[SCREEN_TARGET_RIGHT], C3D_CLEAR_COLOR, 0, 0);
		C3D_FrameDrawOn(GPU3DS.screenTargets[SCREEN_TARGET_RIGHT]); 
	}

	return true;
}

bool gpu3dsInitialize()
{

    GSPGPU_FramebufferFormat gpuBufFmt = (GSPGPU_FramebufferFormat)DISPLAY_TRANSFER_FMT;
    gfxInit(gpuBufFmt, gpuBufFmt, false);
    log3dsWrite("gfxInit GSPGPU_FramebufferFormat: %d", gpuBufFmt);

    memset(&GPU3DS, 0, sizeof(GPU3DS)); // wipe everything to 0/NULL/false

	vramFree(vramAlloc(0)); // vramInit()
    GPU3DS.vramTotal = vramSpaceFree();
    GPU3DS.linearMemTotal = linearSpaceFree();
	log3dsWrite("linear memory total: %dkb vram total: %dkb,", GPU3DS.linearMemTotal / 1024, GPU3DS.vramTotal / 1024);

	gfxSet3D(false);
    APT_CheckNew3DS(&GPU3DS.isNew3DS);

    // Increased buffer size to 1MB for screens with heavy effects (multiple wavy backgrounds and line-by-line windows).
    // Memory Usage = 2.00 MB   for GPU command buffer
    C3D_Init(0x200000);
    C3D_CullFace(GPU_CULL_NONE);

    log3dsWrite("C3D_Init v");

    GPU_COLORBUF colorBufFmt = (GPU_COLORBUF)gpu3dsGetTransferFmt((GPU_TEXCOLOR)DISPLAY_TRANSFER_FMT);

    // no depth buffer needed for screen targets
    GPU3DS.screenTargets[SCREEN_TARGET_LEFT] = C3D_RenderTargetCreate(SCREEN_HEIGHT, SCREEN_TOP_WIDTH, colorBufFmt, -1);
    C3D_RenderTargetSetOutput(GPU3DS.screenTargets[SCREEN_TARGET_LEFT], GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    GPU3DS.screenTargets[SCREEN_TARGET_RIGHT] = C3D_RenderTargetCreate(SCREEN_HEIGHT, SCREEN_TOP_WIDTH, colorBufFmt, -1);
    C3D_RenderTargetSetOutput(GPU3DS.screenTargets[SCREEN_TARGET_RIGHT], GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);

    GPU3DS.screenTargets[SCREEN_TARGET_BOTTOM] = C3D_RenderTargetCreate(SCREEN_HEIGHT, SCREEN_BOTTOM_WIDTH, colorBufFmt, -1);
    C3D_RenderTargetSetOutput(GPU3DS.screenTargets[SCREEN_TARGET_BOTTOM], GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

    log3dsWrite("C3D_RenderTargetSetOutput v");
    
    GPU3DS.isReal3DS = isReal3DS();
    log3dsWrite("Real 3DS: %s, New 3DS: %s", GPU3DS.isReal3DS ? "v" : "x", GPU3DS.isNew3DS ? "v" : "x");

    // Initialize the projection matrix for the top / bottom
    // screens
    //
    Mtx_OrthoTilt(&GPU3DS.projectionTopScreen, 0.0f, 400.0f, 240.0f, 0.0f, 0.0f, 1.0f, true);
    Mtx_OrthoTilt(&GPU3DS.projectionBottomScreen, 0.0f, 320.0f, 240.0f, 0.0f, 0.0f, 1.0f, true);

    // Initialize all shaders to empty
    //
    for (int i = 0; i < SPROGRAM_COUNT; i++)
    {
        GPU3DS.shaders[i].dvlb = NULL;
    }


    return true;
}


void gpu3dsFinalize()
{
	log3dsWrite("Free up all shaders' DVLB");
    for (int i = 0; i < SPROGRAM_COUNT; i++)
    {
        if (GPU3DS.shaders[i].dvlb)
            DVLB_Free(GPU3DS.shaders[i].dvlb);
    }

	log3dsWrite("delete the render targets");
    
    for (int i = 0; i < SCREEN_TARGET_COUNT; i++)
    {
   	    C3D_RenderTargetDelete(GPU3DS.screenTargets[i]);
    }

	log3dsWrite("C3D_Fini");
	C3D_Fini();

	log3dsWrite("gfxExit");
	gfxExit();
}

void gpu3dsEnableAlphaTestNotEqualsZero()
{
    C3D_AlphaTest(true, GPU_NOTEQUAL, 0x00);
}

void gpu3dsEnableAlphaTestGreaterThanEquals(uint8 alpha)
{
    C3D_AlphaTest(true, GPU_GEQUAL, alpha);
}

void gpu3dsDisableAlphaTest()
{
    C3D_AlphaTest(false, GPU_NOTEQUAL, 0x00);
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

// Return bits per pixel
size_t gpu3dsGetFmtSize(GPU_TEXCOLOR fmt)
{
	switch (fmt)
	{
		case GPU_RGBA8:
			return 32;
		case GPU_RGB8:
			return 24;
		case GPU_RGBA5551:
		case GPU_RGB565:
		case GPU_RGBA4:
		case GPU_LA8:
		case GPU_HILO8:
			return 16;
		case GPU_L8:
		case GPU_A8:
		case GPU_LA4:
		case GPU_ETC1A4:
			return 8;
		case GPU_L4:
		case GPU_A4:
		case GPU_ETC1:
			return 4;
		default:
			return 0;
	}
}

s8 gpu3dsGetTransferFmt(GPU_TEXCOLOR fmt)
{
    switch (fmt)
    {
        case GPU_RGBA8:
            return GX_TRANSFER_FMT_RGBA8;

        case GPU_RGB8:
            return GX_TRANSFER_FMT_RGB8;

        case GPU_RGBA5551:
            return GX_TRANSFER_FMT_RGB5A1;

        case GPU_RGB565:
            return GX_TRANSFER_FMT_RGB565;

        case GPU_RGBA4:
            return GX_TRANSFER_FMT_RGBA4;

        // unsupported Formats
        // ETC1, L8, A8, etc. cannot be used in hardware transfers
        default:
            return -1;
    }
}

u8 gpu3dsGetFrameBufferFmt(GPU_TEXCOLOR fmt, bool isDepthBuffer)
{
    size_t bitsPerPixel = gpu3dsGetFmtSize(fmt);

    if (isDepthBuffer)
    {
        GPU_DEPTHBUF depthFmt;

        switch (bitsPerPixel)
        {
            case 16:
                depthFmt = GPU_RB_DEPTH16;
                break;
            case 24:
                depthFmt = GPU_RB_DEPTH24;
                break;
            default:
                depthFmt = GPU_RB_DEPTH24_STENCIL8;
        }

        return (u8)depthFmt;
    }

    GPU_COLORBUF colorFmt;

    if (bitsPerPixel == 16)
    {
        switch (fmt)
        {
            case GPU_RGBA5551:
                colorFmt = GPU_RB_RGBA5551;
                break;
            case GPU_RGB565:
                colorFmt = GPU_RB_RGB565;
                break;
            default:
                colorFmt = GPU_RB_RGBA4;
        }
    } 
    else if (bitsPerPixel == 24)
        colorFmt = GPU_RB_RGB8;
    else 
        colorFmt = GPU_RB_RGBA8;
    
    return (u8)colorFmt;
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

    if (!config->onVram) {
        memset(C3D_Tex2DGetImagePtr(&texture->tex, 0, NULL), 0, texture->tex.size);
        
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
    // which seem required for the shader logic in shader_tiles and shader_mode7
	// We do this here as well to match the projection matrix
    float near = 0.0f;
    float far = 1.0f;
    Mtx_Ortho(&texture->projection, 0.0f, vpWidth, 0.0f, vpHeight, near, far, true);
    texture->projection.m[8] = near / (far - near);
    texture->projection.m[9] = 1 / (near - far);

    bool createTargetFromTex = w_pow2 <= maxViewportWidth && h_pow2 <= maxViewportWidth;
    GPU_DEPTHBUF depthFmt = (GPU_DEPTHBUF)gpu3dsGetFrameBufferFmt(texture->tex.fmt, true);

    if (createTargetFromTex) {       
        texture->target = C3D_RenderTargetCreateFromTex(&texture->tex, GPU_TEXFACE_2D, 0, config->hasDepthBuf ? C3D_DEPTHTYPE(depthFmt) : -1);
    }
    else {
        // for SNES_MODE7_FULL we create a 512x512 render target
        GPU_COLORBUF colorFmt = (GPU_COLORBUF)gpu3dsGetFrameBufferFmt(texture->tex.fmt);
        texture->target = C3D_RenderTargetCreate(vpWidth, vpHeight, colorFmt, -1);
    }

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
    if (texture == NULL) {
        return;
    }

    if (texture->target != NULL) {
   	    C3D_RenderTargetDelete(texture->target);
    }
    
    if (texture->tex.data != NULL) {
        C3D_TexDelete(&texture->tex);
    }
}

bool gpu3dsInitializeShaderUniformLocations()
{

    // used by shader_screen (v), shader_tiles (g), shader_mode7 (g)
    GPU3DS.shaderULocs[ULOC_PROJECTION] = shaderInstanceGetUniformLocation(GPU3DS.shaders[SPROGRAM_SCREEN].shaderProgram.vertexShader, "projection");
    GPU3DS.shaderULocs[ULOC_TEX_SCALE] = shaderInstanceGetUniformLocation(GPU3DS.shaders[SPROGRAM_SCREEN].shaderProgram.vertexShader, "textureScale");
    
    // used by shader_tiles (v)
    GPU3DS.shaderULocs[ULOC_TEX_OFFSET] = shaderInstanceGetUniformLocation(GPU3DS.shaders[SPROGRAM_TILES].shaderProgram.vertexShader, "textureOffset");
    
    // used by shader_mode7 (v)
    GPU3DS.shaderULocs[ULOC_UPDATE_FRAME] = shaderInstanceGetUniformLocation(GPU3DS.shaders[SPROGRAM_MODE7].shaderProgram.vertexShader, "updateFrame");

	bool uLocsInvalid = false;

    for (int i = 0; i < ULOC_COUNT; i++) {
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
    C3D_AlphaBlend(
        GPU_BLEND_ADD,
        GPU_BLEND_ADD,
        GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
        GPU_ONE, GPU_ZERO
    );
}

void gpu3dsDisableAlphaBlending()
{
    C3D_AlphaBlend(
        GPU_BLEND_ADD,
        GPU_BLEND_ADD,
        GPU_ONE, GPU_ZERO,
        GPU_ONE, GPU_ZERO
    );
}

void gpu3dsDisableAlphaBlendingKeepDestAlpha()
{
    C3D_AlphaBlend(
        GPU_BLEND_ADD,
        GPU_BLEND_ADD,
        GPU_ONE, GPU_ZERO,
        GPU_ZERO, GPU_ONE
    );
}

void gpu3dsEnableAdditiveBlending()
{
    C3D_AlphaBlend(
        GPU_BLEND_ADD,
        GPU_BLEND_ADD,
        GPU_DST_ALPHA, GPU_ONE,
        GPU_ONE, GPU_ZERO
    );
}

void gpu3dsEnableSubtractiveBlending()
{
	C3D_AlphaBlend(
        GPU_BLEND_REVERSE_SUBTRACT,
        GPU_BLEND_ADD,
        GPU_DST_ALPHA, GPU_ONE,
        GPU_ONE, GPU_ZERO
    );
}

void gpu3dsEnableAdditiveDiv2Blending()
{
    C3D_BlendingColor(0xFF000000);
    C3D_AlphaBlend(
        GPU_BLEND_ADD,
        GPU_BLEND_ADD,
        GPU_DST_ALPHA, GPU_ONE_MINUS_DST_ALPHA,
        GPU_ONE, GPU_ZERO
    );
}

void gpu3dsEnableSubtractiveDiv2Blending()
{
    C3D_BlendingColor(0xFF000000);
    C3D_AlphaBlend(
        GPU_BLEND_REVERSE_SUBTRACT,
        GPU_BLEND_ADD,
        GPU_DST_ALPHA, GPU_ONE_MINUS_DST_ALPHA,
        GPU_ONE, GPU_ZERO
    );
}

void gpu3dsResetState()
{
    gpu3dsResetLayerSectionLimits(&GPU3DSExt.layerList);

    GPU3DS.currentShader = SPROGRAM_COUNT;
    GPU3DS.currentRenderTarget = TARGET_COUNT;
    GPU3DS.currentRenderTargetDim = 0;
    GPU3DS.currentTextureDim = 0;
    GPU3DS.depthTestEnabled = false;
    gpu3dsDisableDepthTest();
    
    GPU3DS.currentRenderState.textureBind = TEX_COUNT;
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_UNSET;
    GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_UNSET;
    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_UNSET;
    
    GPU3DS.currentRenderStateFlags = 0;

	gpu3dsClearTextureEnv(1);
	gpu3dsClearTextureEnv(2);
	gpu3dsClearTextureEnv(3);
	gpu3dsClearTextureEnv(4);
	gpu3dsClearTextureEnv(5);

}

void gpu3dsSetRenderTargetToFrameBuffer()
{
    gfxScreen_t screen = GPU3DS.currentRenderTarget == TARGET_SCREEN_PRIMARY ? screenSettings.GameScreen : screenSettings.SecondScreen;
    C3D_RenderTarget *target = (screen == GFX_TOP) ? GPU3DS.screenTargets[SCREEN_TARGET_LEFT] : GPU3DS.screenTargets[SCREEN_TARGET_BOTTOM];
    
    C3D_FrameDrawOn(target);
}

void gpu3dsSetRenderTargetToTexture(SGPU_TARGET_ID target)
{
    SGPUTexture *texture = &GPU3DS.textures[target];

    C3D_FrameDrawOn(texture->target);
}

void gpu3dsBindTexture(SGPU_TEXTURE_ID textureId)
{
    SGPUTexture *texture = &GPU3DS.textures[textureId];
    
    // texture params are dynamic for main and mode7 texture
    if (textureId == SNES_MAIN)
    {
        GPU_TEXTURE_FILTER_PARAM filter = GPU_TEXTURE_FILTER_PARAM(settings3DS.ScreenFilter);
	    C3D_TexSetFilter(&texture->tex, filter, filter);
    } 
    else if  (textureId == SNES_MODE7_FULL)
    {
        GPU_TEXTURE_WRAP_PARAM wrap = PPU.Mode7Repeat == 0 ? GPU_REPEAT : GPU_CLAMP_TO_BORDER;
        C3D_TexSetWrap(&texture->tex, wrap, wrap);
    }

    C3D_TexBind(0, &texture->tex);
}

const char* SGPUTextureIDToString(SGPU_TEXTURE_ID id) {
    switch (id) {
        case SNES_MAIN:                 return "main";
        case SNES_SUB:                  return "sub";
        case SNES_DEPTH:                return "depth";
        case SNES_MODE7_FULL:           return "m7 full";
        case SNES_MODE7_TILE_0:         return "m7 zero";
        case SNES_TILE_CACHE:           return "tile cache";
        case SNES_MODE7_TILE_CACHE:     return "m7 tile cache";
        case UI_BORDER:                 return "ui border";
        case UI_BEZEL:                  return "ui bezel";
        case UI_COVER:                  return "ui cover";
        case UI_ATLAS:                  return "ui atlas";
        default:                        return "invalid";
    }
}

const char* SGPUTexColorToString(GPU_TEXCOLOR color) {
    switch (color) {
        case GPU_RGBA8:     return "GPU_RGBA8";
        case GPU_RGB8:      return "GPU_RGB8";
        case GPU_RGBA5551:  return "GPU_RGBA5551";
        case GPU_RGB565:    return "GPU_RGB565";
        case GPU_RGBA4:     return "GPU_RGBA4";
        default:            return "invalid";
    }
}

const char* SGPUVboIDToString(SGPU_VBO_ID id) {
    switch (id) {
        case VBO_SCENE_RECT:      return "vbo rect";
        case VBO_SCENE_TILE:      return "vbo tile";
        case VBO_SCENE_MODE7_LINE:return "vbo m7 line";
        case VBO_MODE7_TILE:      return "vbo m7 tile";
        case VBO_SCREEN:          return "vbo screen";
        default:                  return "invalid";
    }
}