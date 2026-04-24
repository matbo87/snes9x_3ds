#include <3ds.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <string.h>
#include <stdio.h>

#include "snes9x.h"
#include "memmap.h"
#include "3dsgpu.h"
#include "3dsfiles.h"
#include "3dsimpl.h"
#include "3dssettings.h"
#include "3dslog.h"
#include "3dslcd.h"

SGPU3DS GPU3DS;

static const u8 colorFmtSizes[] = {2,1,0,0,0}; // from citro3d framebuffer.c

static bool isReal3DS() {
    // real 3DS returns success for PDC_VTOTAL_* reads
    // some emulators (e.g. citra nightly 1989 on macOS) fail these
    // Not guaranteed for all emulators, so treat this as best-effort
    u32 top = 0, bottom = 0;
    Result topRet = GSPGPU_ReadHWRegs(PDC_VTOTAL_TOP, &top, 4);
    Result bottomRet = GSPGPU_ReadHWRegs(PDC_VTOTAL_BOTTOM, &bottom, 4);

    bool topValid = R_SUCCEEDED(topRet) && top > 0;
    bool bottomValid = R_SUCCEEDED(bottomRet) && bottom > 0;

    return topValid || bottomValid;
}

//---------------------------------------------------------
// Returns the inter-ocular distance in pixels based on
// the 3D slider position. Returns 0 when slider is off.
//---------------------------------------------------------
float gpu3dsGetIOD()
{
    float sliderVal = osGet3DSliderState();
    return sliderVal * IOD_MAX_PIXELS;
}

bool gpu3dsIs3DAvailable()
{
    return GPU3DS.model != CFG_MODEL_2DS
        && GPU3DS.model != CFG_MODEL_N2DSXL;
}

bool gpu3dsIs3DEnabled()
{
    return
        !settings3DS.Disable3DSlider
        && settings3DS.GameScreen == GFX_TOP
        && gfxIs3D();
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

// render solid UI shapes and textured text in a single GPU draw call
void gpu3dsSetTextureEnvironmentModulateColor() {
    C3D_TexEnv* env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);

    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);

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
    
    for (int i = 0; i < info->totalAttributes; i++) {
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
    if (list == nullptr)
        return;

    linearFree(list->data_base);
}

void gpu3dsPrepareListForNextFrame(SVertexList *list, bool swap)
{
    if (swap) {
        if (list->flip)
            list->data = (void *)((u32)(list->data_base) + list->sizeInBytes / 2);
        else
            list->data = list->data_base;
            
        list->flip = 1 - list->flip;
    }

    list->count = 0;
    list->from = 0;
}

void gpu3dsSetDefaultRenderState(SGPU_SHADER_PROGRAM shader, bool isSecondScreen) {
	GPU3DS.currentRenderState.shader = shader;
	GPU3DS.currentRenderState.depthTest = SGPU_STATE_DISABLED;
	GPU3DS.currentRenderState.stencilTest = STENCIL_TEST_DISABLED;
	GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_DISABLED;
	GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;

	if (shader == SPROGRAM_TILES) {
		GPU3DS.currentRenderState.target = TARGET_SNES_MAIN;
		GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_COLOR;
	} else {
		GPU3DS.currentRenderState.target = isSecondScreen ? TARGET_SCREEN_SECOND : TARGET_SCREEN_GAME;
		GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0;
	}
}

void gpu3dsSetFragmentOperations(SGPURenderState *state, u64 diff) {
    // stencil test
    //
    if (diff & PACKED_MASK_STENCIL) {
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
    if (diff & PACKED_MASK_DEPTH_TEST) {
        if (state->depthTest)
            gpu3dsEnableDepthTest();
        else
            gpu3dsDisableDepthTest();
    }

    // alpha test
    //
    if (diff & PACKED_MASK_ALPHA_TEST) {
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
    if (diff & PACKED_MASK_ALPHA_BLEND) {
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

void gpu3dsSetShaderAndUniforms(SGPURenderState *state, u64 diff, bool targetUpdated, bool textureUpdated) {
    bool shaderUpdated = diff & PACKED_MASK_SHADER;

    if (shaderUpdated) {
        C3D_BindProgram(&GPU3DS.shaders[state->shader].shaderProgram);
        GPU3DS.currentRenderTargetDim = 0;
        GPU3DS.currentTextureDim = 0;
        GPU3DS.currentVboId = VBO_COUNT;
    }

    // set projection
    if (targetUpdated || shaderUpdated)
    {
        if (state->target == TARGET_SCREEN_GAME || state->target == TARGET_SCREEN_SECOND) {
            gfxScreen_t screen = state->target == TARGET_SCREEN_GAME ? settings3DS.GameScreen : settings3DS.SecondScreen;
            C3D_Mtx projection = (screen == GFX_TOP) ? GPU3DS.projectionTopScreen : GPU3DS.projectionBottomScreen;
            C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_PROJECTION], &projection);
        } else {
            SGPU_TEXTURE_ID targetTexId = (SGPU_TEXTURE_ID)state->target;

            // Mosaic redirect for uniform upload: main-screen projection
            // comes from the scratch texture (same 256x256 dim, so the
            // projection matches — this is really just to stay consistent
            // with the gpu3dsSetRenderTargetToTexture redirect).
            if (GPU3DS.mosaicScratchActive && state->target == TARGET_SNES_MAIN) {
                targetTexId = SNES_MOSAIC_SCRATCH;
            }

            SGPUTexture *targetFromTex = &GPU3DS.textures[targetTexId];
            GPU_SHADER_TYPE projShader = state->shader == SPROGRAM_SCREEN ? GPU_VERTEX_SHADER : GPU_GEOMETRY_SHADER;

            if (targetFromTex->tex.dim != GPU3DS.currentRenderTargetDim) {
                C3D_FVUnifMtx4x4(projShader, GPU3DS.shaderULocs[ULOC_PROJECTION], &targetFromTex->projection);
                GPU3DS.currentRenderTargetDim = targetFromTex->tex.dim;
            }
        }
    }

    // set textureScale
    if (textureUpdated)
    {
        SGPUTexture *texture = &GPU3DS.textures[state->textureBind];
        GPU_SHADER_TYPE shaderType = state->shader != SPROGRAM_SCREEN ? GPU_GEOMETRY_SHADER : GPU_VERTEX_SHADER;

        if (GPU3DS.currentTextureDim != texture->tex.dim) {
            C3D_FVUnifSet(shaderType, GPU3DS.shaderULocs[ULOC_TEX_SCALE], texture->scale[3], texture->scale[2], texture->scale[1], texture->scale[0]);
            GPU3DS.currentTextureDim = texture->tex.dim;
        }
    }

    if (state->shader == SPROGRAM_TILES && (diff & PACKED_MASK_TEX_OFFSET)) {
        float textureOffset[4] = {0.0f, 0.0f, 0.0f, state->textureOffset == SGPU_STATE_ENABLED ? 1.0f : 0.0f}; // wzyx
        C3D_FVUnifSet(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_TEX_OFFSET], textureOffset[3], textureOffset[2], textureOffset[1], textureOffset[0]);
    }

    if (shaderUpdated && state->shader == SPROGRAM_MODE7) {
        float updateFrame[4] = {(float)GPU3DSExt.mode7FrameCount, 0.0f, 0.0f, 0.0f}; // wzyx
        C3D_FVUnifSet(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_UPDATE_FRAME], updateFrame[3], updateFrame[2], updateFrame[1], updateFrame[0]);
    }
}


void gpu3dsDraw(SVertexList *list, const void* indices, int count, int from) {
    t3dsStartTimer(TIMER_DRAW);
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

    t3dsStopTimer(TIMER_DRAW);
}

bool gpu3dsFrameBegin(u8 flags, bool ingame, bool isSecondScreen)
{
    t3dsStartTimer(TIMER_GPU_WAIT);
    if (!C3D_FrameBegin(flags)) {
        t3dsStopTimer(TIMER_GPU_WAIT);
        return false;
    }
    t3dsStopTimer(TIMER_GPU_WAIT);

    // invalidate so next gpu3dsApplyRenderState re-applies the target
    GPU3DS.appliedRenderState.target = TARGET_UNSET;
    GPU3DS.currentVboId = VBO_COUNT;
    
	gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_SCREEN]);
	gpu3dsSetDefaultRenderState(ingame ? SPROGRAM_TILES : SPROGRAM_SCREEN, isSecondScreen);

    return true;
}

void gpu3dsFrameEnd(u8 flags)
{
    t3dsStartTimer(TIMER_FLUSH);
    C3D_FrameEnd(flags);
    t3dsStopTimer(TIMER_FLUSH);
}

bool gpu3dsClearScreen(gfxScreen_t screen, bool isTopStereo) {
	SCREEN_TARGET targetId = screen == GFX_TOP ? SCREEN_TARGET_LEFT : SCREEN_TARGET_BOTTOM;

	if (!C3D_FrameDrawOn(GPU3DS.screenTargets[targetId])) {
		return false;
	}

	C3D_RenderTargetClear(GPU3DS.screenTargets[targetId], C3D_CLEAR_COLOR, 0, 0);

	if (isTopStereo && screen != GFX_BOTTOM) {
		C3D_RenderTargetClear(GPU3DS.screenTargets[SCREEN_TARGET_RIGHT], C3D_CLEAR_COLOR, 0, 0);
		C3D_FrameDrawOn(GPU3DS.screenTargets[SCREEN_TARGET_RIGHT]); // sets target->used flag
	}

    // invalidate so next gpu3dsApplyRenderState re-applies the target
    GPU3DS.appliedRenderState.target = TARGET_UNSET;

	return true;
}

bool gpu3dsInitialize()
{
    memset(&GPU3DS, 0, sizeof(GPU3DS)); // wipe everything to 0/NULL/false

	vramFree(vramAlloc(0)); // vramInit()
    GPU3DS.vramTotal = vramSpaceFree();
    GPU3DS.linearMemTotal = linearSpaceFree();
	log3dsWrite("linear memory total: %dkb vram total: %dkb,", GPU3DS.linearMemTotal / 1024, GPU3DS.vramTotal / 1024);


    // Increased buffer size to 1MB for screens with heavy effects (multiple wavy backgrounds and line-by-line windows).
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
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
    log3dsWrite("Real 3DS: %s", GPU3DS.isReal3DS ? "v" : "x");

    u8 model = 0;
    cfguInit();
    CFGU_GetSystemModel(&model);
    cfguExit();
    GPU3DS.model = (CFG_SystemModel)model;
    log3dsWrite("Model: %d", GPU3DS.model);

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

bool gpu3dsAllocVramTextureAndTarget(SGPUTexture *texture, const SGPUTextureConfig *config)
{
    texture->id = config->id;
    u32 w_pow2 = gpu3dsGetNextPowerOf2(config->width);
    u32 h_pow2 = gpu3dsGetNextPowerOf2(config->height);

    if (!C3D_TexInitVRAM(&texture->tex, w_pow2, h_pow2, config->fmt)) {
        return false;
    }

    texture->tex.param = config->param;

    texture->scale[3] = 1.0f / config->width;  // x
    texture->scale[2] = 1.0f / config->height; // y
    texture->scale[1] = 0; // z
    texture->scale[0] = 0; // w

    // (citra only?) fixes broken mode7 texture on f-zero start screen 
    // (and probably other games)
    gpu3dsClearTexture(texture, 0);

    // 3DS does not allow rendering to a viewport whose width > 512.
    const u32 maxViewportWidth = 512;
    
    u32 vpWidth = w_pow2 > maxViewportWidth ? maxViewportWidth : w_pow2;
    u32 vpHeight = h_pow2 > maxViewportWidth ? maxViewportWidth : h_pow2;

	// bubble2k's orthographic implementation had some adjustments for 0xA and 0xB (see 3dsmatrix.cpp in older versions)
    // which seem required for the shader logic in shader_tiles and shader_mode7
	// We do this here as well to match the projection matrix
    float near = 0.0f;
    float far = 1.0f;
    Mtx_Ortho(&texture->projection, 0.0f, vpWidth, 0.0f, vpHeight, near, far, true);
    texture->projection.m[8] = near / (far - near);
    texture->projection.m[9] = 1 / (near - far);

    bool createTargetFromTex = w_pow2 <= maxViewportWidth && h_pow2 <= maxViewportWidth;

    if (createTargetFromTex) {       
        texture->target = C3D_RenderTargetCreateFromTex(&texture->tex, GPU_TEXFACE_2D, 0, -1);
    }
    else {
        // for our 1024x1024 texture we need to temporarily shrink dimensions to force 512 stride configuration
        // we could also create a custom render target via C3D_RenderTargetCreate but we rather save vram here
        u16 textureWidth = texture->tex.width;
        u16 textureHeight = texture->tex.height;

        texture->tex.width = vpWidth;
        texture->tex.height = vpHeight;

        texture->target = C3D_RenderTargetCreateFromTex(&texture->tex, GPU_TEXFACE_2D, 0, -1);

        texture->tex.width = textureWidth;
        texture->tex.height = textureHeight;
    }

    return texture->target != NULL;
}

bool gpu3dsAllocLinearTexture(SGPUTexture *texture, const SGPUTextureConfig *config)
{
    texture->id = config->id;
    u32 w_pow2 = gpu3dsGetNextPowerOf2(config->width);
    u32 h_pow2 = gpu3dsGetNextPowerOf2(config->height);

    if (!C3D_TexInit(&texture->tex, w_pow2, h_pow2, config->fmt)) {
        return false;
    }

    texture->tex.param = config->param;

    texture->scale[3] = 1.0f / config->width;  // x
    texture->scale[2] = 1.0f / config->height; // y
    texture->scale[1] = 0; // z
    texture->scale[0] = 0; // w

    memset(C3D_Tex2DGetImagePtr(&texture->tex, 0, NULL), 0, texture->tex.size);

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

    GPU3DS.currentRenderTargetDim = 0;
    GPU3DS.currentTextureDim = 0;
    GPU3DS.currentVboId = VBO_COUNT;

    // Set current to known defaults
    GPU3DS.currentRenderState.packed = 0;
    GPU3DS.currentRenderState.textureBind = TEX_UNSET;
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_UNSET;
    GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_UNSET;
    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_UNSET;
    GPU3DS.currentRenderState.shader = SPROGRAM_UNSET;
    GPU3DS.currentRenderState.target = TARGET_UNSET;
    GPU3DS.currentRenderState.depthTest = SGPU_STATE_UNSET;
    GPU3DS.currentRenderState.textureOffset = SGPU_STATE_UNSET;
    
    // Set applied to same values so no spurious diffs occur before first frame
    GPU3DS.appliedRenderState = GPU3DS.currentRenderState;

	gpu3dsClearTextureEnv(1);
	gpu3dsClearTextureEnv(2);
	gpu3dsClearTextureEnv(3);
	gpu3dsClearTextureEnv(4);
	gpu3dsClearTextureEnv(5);
}

void gpu3dsSetRenderTargetToFrameBuffer(SGPU_TARGET_ID targetId)
{
    gfxScreen_t screen = targetId == TARGET_SCREEN_GAME ? settings3DS.GameScreen : settings3DS.SecondScreen;
    SCREEN_TARGET screenTarget;

    if (screen == GFX_TOP)
        screenTarget = GPU3DS.activeSide == GFX_RIGHT ? SCREEN_TARGET_RIGHT : SCREEN_TARGET_LEFT;
    else
        screenTarget = SCREEN_TARGET_BOTTOM;

    C3D_FrameDrawOn(GPU3DS.screenTargets[screenTarget]);
}

void gpu3dsSetRenderTargetToTexture(SGPU_TARGET_ID target)
{
    SGPUTexture *texture = &GPU3DS.textures[target];

    // Mosaic redirect: main-screen draws land in the scratch so the BG
    // can be rendered at reduced resolution and composited back. Scratch
    // is a texture id, not a SGPU_TARGET_ID — the BW_TARGET bitfield is
    // full.
    if (GPU3DS.mosaicScratchActive && target == TARGET_SNES_MAIN) {
        texture = &GPU3DS.textures[SNES_MOSAIC_SCRATCH];
    }

    C3D_FrameDrawOn(texture->target);
}

void gpu3dsSetMosaicViewport(int mosaicSize)
{
    if (mosaicSize <= 1) {
        C3D_SetViewport(0, 0, 256, 256);
        return;
    }

    u32 vpW = (256 + mosaicSize - 1) / mosaicSize;
    u32 vpH = (224 + mosaicSize - 1) / mosaicSize;

    C3D_SetViewport(0, 0, vpW, vpH);
}

void gpu3dsBeginMosaicPass(int mosaicSize)
{
    GPU3DS.mosaicScratchActive = true;

    // Route TARGET_SNES_MAIN to the scratch and clear it. The clear is
    // deliberate: each mosaic pass renders exactly one BG, and the
    // composite quad then samples the result — any leftover pixels
    // from a previous BG's mosaic pass would leak through transparency.
    GPU3DS.appliedRenderState.target = TARGET_UNSET;
    gpu3dsSetRenderTargetToTexture(TARGET_SNES_MAIN);
    C3D_RenderTargetClear(GPU3DS.textures[SNES_MOSAIC_SCRATCH].target, C3D_CLEAR_ALL, 0, 0);

    gpu3dsSetMosaicViewport(mosaicSize);
}

void gpu3dsEndMosaicPass()
{
    GPU3DS.mosaicScratchActive = false;
    GPU3DS.appliedRenderState.target = TARGET_UNSET;
}

void gpu3dsBindTexture(SGPU_TEXTURE_ID textureId)
{
    SGPUTexture *texture = &GPU3DS.textures[textureId];
    
    // texture params are dynamic for main and mode7 texture
    if (textureId == SNES_MAIN)
    {
        GPU_TEXTURE_FILTER_PARAM filter = settings3DS.ScreenStretch == Setting::ScreenStretch::None
            ? GPU_NEAREST
            : settings3DS.ScreenFilter;

	    C3D_TexSetFilter(&texture->tex, filter, filter);
    }
    else if  (textureId == SNES_MODE7_FULL)
    {
        GPU_TEXTURE_WRAP_PARAM wrap = PPU.Mode7Repeat == 0 ? GPU_REPEAT : GPU_CLAMP_TO_BORDER;
        C3D_TexSetWrap(&texture->tex, wrap, wrap);
    }

    C3D_TexBind(0, &texture->tex);
}
