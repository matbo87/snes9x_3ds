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

u32 vertexListBufferOffsets[1] = { 0 };

static const u8 colorFmtSizes[] = {2,1,0,0,0}; // from citro3d framebuffer.c

size_t shader_count = (sizeof(GPU3DS.shaders)/sizeof(GPU3DS.shaders[0]));

inline void __attribute__((always_inline)) gpu3dsSetAttributeBuffers(SGPU_LIST_ID listId, C3D_AttrInfo *attrInfo, u32 *listAddress, int bufferSize)
{
    if (GPU3DS.currentAttributeBuffer != listAddress)
    {
        int totalAttributes = attrInfo->attrCount;
        u32 attributeFormats = attrInfo->flags[0];
        u32 *osAddress = (u32 *)osConvertVirtToPhys(listAddress);

        // Some very minor optimizations
        if (GPU3DS.currentTotalAttributes != totalAttributes ||
            GPU3DS.currentAttributeFormats != attributeFormats)
        {
            u64 vertexListAttribPermutations[1] = { attrInfo->permutation };
            u8 vertexListNumberOfAttribs[1] = { totalAttributes };

            GPU_SetAttributeBuffers(
                totalAttributes,
                osAddress,
                attributeFormats,
                0xFFFF,
                vertexListAttribPermutations[0],
                1,
                vertexListBufferOffsets,
                vertexListAttribPermutations,
                vertexListNumberOfAttribs
            );

            GPU3DS.currentTotalAttributes = totalAttributes;
            GPU3DS.currentAttributeFormats = attributeFormats;
        }
        else
        {
            GPUCMD_AddWrite(GPUREG_ATTRIBBUFFERS_LOC, ((u32)osAddress)>>3);

            // The real 3DS doesn't allow us to set the osAddress independently without
            // setting the additional register as below. If we don't do this, the
            // 3DS GPU will freeze up.
            //
            GPUCMD_AddMaskedWrite(GPUREG_VSH_INPUTBUFFER_CONFIG, 0xB, 0xA0000000|(totalAttributes-1));
        }

        GPU3DS.currentAttributeBuffer = listAddress;
    }
}

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
    list->Count = 0;
    list->FromIndex = 0;
    list->Flip = 1;

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
    if (list->Flip)
        list->data = (void *)((u32)(list->data_base) + list->sizeInBytes / 2);
    else
        list->data = list->data_base;
    
    list->Flip = 1 - list->Flip;
    list->Count = 0;
    list->FromIndex = 0;
}


void gpu3dsSetFragmentOperations(SGPURenderState *state) {
    // stencil test
    //
    if (state->updated & FLAG_STENCIL_TEST) {
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

        state->updated &= ~FLAG_STENCIL_TEST;
    }

    // depth test
    //
    if (state->updated & FLAG_DEPTH_TEST) {
        if (state->depthTest == DEPTH_TEST_ENABLED) 
            gpu3dsEnableDepthTest();
        else
            gpu3dsDisableDepthTest();

        state->updated &= ~FLAG_DEPTH_TEST;
    }

    // alpha test
    //
    if (state->updated & FLAG_ALPHA_TEST) {
        switch (state->alphaTest)
        {
            case ALPHA_TEST_DISABLED:
                gpu3dsDisableAlphaTest();
                break;
            case ALPHA_TEST_NE_ZERO:
                gpu3dsEnableAlphaTestNotEqualsZero();
                break;
            default:
                gpu3dsEnableAlphaTestGreaterThanEquals(state->alphaTest);
                break;
        }

        state->updated &= ~FLAG_ALPHA_TEST;
    }

    // alpha blending
    //
    if (state->updated & FLAG_ALPHA_BLENDING) {
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

        state->updated &= ~FLAG_ALPHA_BLENDING;
    }
}

void gpu3dsSetShaderAndUniforms(SGPURenderState *state, bool targetUpdated, bool textureUpdated) {
    if (state->updated & FLAG_SHADER) { 
        shaderProgramUse(&GPU3DS.shaders[state->shader].shaderProgram);

        state->updated &= ~FLAG_SHADER;
    }

    // when render target has been updated, we need to update our projection uniforms as well
    if (targetUpdated) {
        if (state->target == TARGET_SCREEN) {
            u32 *projection = (screenSettings.GameScreen == GFX_TOP) ? (u32 *)GPU3DS.projectionTopScreen.m : (u32 *)GPU3DS.projectionBottomScreen.m;
            GPU_SetFloatUniform(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_PROJECTION], projection, 4);
        } else {
            SGPUTexture *targetFromTex = &GPU3DS.textures[(SGPU_TEXTURE_ID)state->target];

            if (targetFromTex) {
                // projection for mode7 targets is handled in mode7 geometry shader
                GPU_SHADER_TYPE shaderType = (state->target != TARGET_SNES_MODE7_TILE_0 && state->target != TARGET_SNES_MODE7_FULL) ? 
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

    if (state->shader == SPROGRAM_TILES && (state->updated & FLAG_TEXTURE_OFFSET)) {
        float textureOffset[4] = {0.0f, 0.0f, 0.0f, state->textureOffset};
        GPU_SetFloatUniform(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_TEX_OFFSET], (u32 *)textureOffset, 1);  

        state->updated &= ~FLAG_TEXTURE_OFFSET;
    }
    
    if (state->shader == SPROGRAM_MODE7 && (state->updated & FLAG_UPDATE_FRAME)) {
        float updateFrame[4] = {state->updateFrame, 0.0f, 0.0f, 0.0f};
        GPU_SetFloatUniform(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_UPDATE_FRAME], (u32 *)updateFrame, 1);
        state->updated &= ~FLAG_UPDATE_FRAME;
    }
}

void gpu3dsApplyRenderState()
{
    SGPURenderState *state = &GPU3DS.currentRenderState;
    // ! order is important here !
    // binding the shader before setting the viewport, may cause the 3ds to freeze (see SMW2 intro)

    bool targetUpdated = state->updated & FLAG_TARGET;

    // update viewport
    //
    if (targetUpdated) {
        if (state->target == TARGET_SCREEN) {
            gpu3dsSetRenderTargetToFrameBuffer();
        } else if (state->target != TARGET_SNES_MODE7_FULL) {
            gpu3dsSetRenderTargetToTexture((SGPU_TEXTURE_ID)state->target);
        } else {
            // TODO:
            // gfxhw.cpp currently handles gpu3dsSetRenderTargetToMode7Texture,
            // we would rather do it here
        }
        
        state->updated &= ~FLAG_TARGET;
    }

    // update texture + environment
    //
    bool textureUpdated = state->updated & FLAG_TEXTURE_BIND;

    if (textureUpdated) {
        gpu3dsBindTexture(state->textureBind);
        state->updated &= ~FLAG_TEXTURE_BIND;
    }

    if (state->updated & FLAG_TEXTURE_ENV) {
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

        state->updated &= ~FLAG_TEXTURE_ENV;
    }

    gpu3dsSetFragmentOperations(state);
    gpu3dsSetShaderAndUniforms(state, targetUpdated, textureUpdated);
}


void gpu3dsRedrawVertexList(SStoredVertexList *list, C3D_AttrInfo *attrInfo, int vertexSize)
{        
    if (list->Count == 0)
        return;
    
    gpu3dsApplyRenderState();
    gpu3dsSetAttributeBuffers(list->id, attrInfo, (u32*)list->data, vertexSize);
    GPU_DrawArray(list->id == VBO_SCREEN ? GPU_TRIANGLES : GPU_GEOMETRY_PRIM, list->FromIndex, list->Count);

    somethingWasDrawn = true;
}

void gpu3dsDrawVertexList(SVertexList *list, bool repeatLastDraw, int layer, int fromIndex, int tileCount)
{
    bool storeVertexList = layer >= 0;

    if (repeatLastDraw)
    {
        if (storeVertexList)
        {
            gpu3dsRedrawVertexList(&GPU3DS.verticesStored[list->id][layer], &list->attrInfo, list->vertexSize);
        }        
            
        
        return;
    }
    
    if (tileCount == -1)
        tileCount = list->Count;

    if (tileCount == 0)
    {
        if (storeVertexList)
        {
            GPU3DS.verticesStored[list->id][layer].Count = 0;
        }

        return;
    }

    if (fromIndex == -1)
        fromIndex = list->FromIndex;

    gpu3dsApplyRenderState();
    gpu3dsSetAttributeBuffers(list->id, &list->attrInfo, (u32*)list->data, list->vertexSize);
    GPU_DrawArray(list->id == VBO_SCREEN ? GPU_TRIANGLES : GPU_GEOMETRY_PRIM, fromIndex, tileCount);

    somethingWasDrawn = true;

    if (list->id == VBO_MODE7_TILE)
    {
        return;
    }
        
    if (storeVertexList)
    {
        GPU3DS.verticesStored[list->id][layer].id = list->id;
        GPU3DS.verticesStored[list->id][layer].data = list->data;
        GPU3DS.verticesStored[list->id][layer].Count = tileCount;
        GPU3DS.verticesStored[list->id][layer].FromIndex = fromIndex;
    }
    
    list->FromIndex += tileCount;
    list->Count = 0;
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
    if (GPU3DS.textureDepthBuffer) vramFree(GPU3DS.textureDepthBuffer);

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
	gpu3dsClearTextureEnv(1);
	gpu3dsClearTextureEnv(2);
	gpu3dsClearTextureEnv(3);
	gpu3dsClearTextureEnv(4);
	gpu3dsClearTextureEnv(5);

	GPU_SetBlendingColor(0,0,0,0);

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

void gpu3dsSetRenderTargetToTexture(SGPU_TEXTURE_ID textureId)
{
    SGPUTexture *texture = &GPU3DS.textures[textureId];

    void *texColorImagePtr = C3D_Tex2DGetImagePtr(&texture->tex, 0, NULL);
    void *texDepthImagePtr = (textureId == SNES_MAIN || textureId == SNES_SUB) ? C3D_Tex2DGetImagePtr(&GPU3DS.textures[SNES_DEPTH].tex, 0, NULL) : GPU3DS.textureDepthBuffer;
    
    u32 *colorBuf = (u32 *)osConvertVirtToPhys(texColorImagePtr);
    u32 *depthBuf = (u32 *)osConvertVirtToPhys(texDepthImagePtr);

    // 3DS does not allow rendering to a viewport whose width > 512.
    int vpWidth = texture->tex.width > 512 ? 512 : texture->tex.width;
    int vpHeight = texture->tex.height > 512 ? 512 : texture->tex.height;
    
    GPU_SetViewport(depthBuf, colorBuf, 0, 0, vpWidth, vpHeight);
    GPUCMD_AddSingleParam(0x000F0117, GPUREG_COLORBUFFER_FORMAT_VALUES[texture->tex.fmt]);
}

void gpu3dsSetRenderTargetToMode7Texture(u32 pixelOffset)
{
    // mode7 shader on citra seems to behave differently than on real device.
    // if we draw all 4 mode7 sections,it's not visible.
    // we can still test it somehow by rendering only a part of the sections.
    // e.g. only render sectionIndex 2 and/or 3 for Yoshi's Island to see mode7 graphics on title screen
    if (GPU3DS.isReal3DS || (pixelOffset == 2 * 0x40000  || pixelOffset == 3 * 0x40000)) 
    {
        SGPUTexture *texture = &GPU3DS.textures[SNES_MODE7_FULL];
        void *texColorImagePtr = C3D_Tex2DGetImagePtr(&texture->tex, 0, NULL);
        void *texDepthImagePtr = GPU3DS.textureDepthBuffer;
        int addressOffset = pixelOffset * gpu3dsGetPixelSize(texture->tex.fmt);
        
        u32 *colorBuf = (u32 *)osConvertVirtToPhys((void *)((int)texColorImagePtr + addressOffset));
        u32 *depthBuf = (u32 *)osConvertVirtToPhys(GPU3DS.textureDepthBuffer);

        // 3DS does not allow rendering to a viewport whose width > 512.
        int vpWidth = texture->tex.width > 512 ? 512 : texture->tex.width;
        int vpHeight = texture->tex.height > 512 ? 512 : texture->tex.height;

        GPU_SetViewport(depthBuf, colorBuf, 0, 0, 512, 512);
        GPUCMD_AddSingleParam(0x000F0117, GPUREG_COLORBUFFER_FORMAT_VALUES[texture->tex.fmt]); //color buffer format
    }
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