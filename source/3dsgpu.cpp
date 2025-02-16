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
u64 vertexListAttribPermutations[1] = { 0x3210 };
u8 vertexListNumberOfAttribs[1] = { 2 };

static const u8 colorFmtSizes[] = {2,1,0,0,0}; // from citro3d framebuffer.c

size_t shader_count = (sizeof(GPU3DS.shaders)/sizeof(GPU3DS.shaders[0]));

bool gpu3dsUpdateRenderState(SGPURenderState* state, int propertyType, u32 newValue, u32 oldValue) {
    if (newValue == oldValue && (state->initialized & propertyType))
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
        case FLAG_TEXTURE_PARAMS:
            state->textureParams = newValue;
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

    state->updated |= propertyType;
    state->initialized |= propertyType;

    return true;
}

inline void gpu3dsSetAttributeBuffers(
    u8 totalAttributes,
    u32 *listAddress, u64 attributeFormats)
{
    if (GPU3DS.currentAttributeBuffer != listAddress)
    {
        u32 *osAddress = (u32 *)osConvertVirtToPhys(listAddress);

        // Some very minor optimizations
        if (GPU3DS.currentTotalAttributes != totalAttributes ||
            GPU3DS.currentAttributeFormats != attributeFormats)
        {
            vertexListNumberOfAttribs[0] = totalAttributes;
            GPU_SetAttributeBuffers(
                totalAttributes, // number of attributes
                osAddress,
                attributeFormats,
                0xFFFF, //0b1100
                0x3210,
                1, //number of buffers
                vertexListBufferOffsets,        // buffer offsets (placeholders)
                vertexListAttribPermutations,   // attribute permutations for each buffer
                vertexListNumberOfAttribs       // number of attributes for each buffer
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

void *gpu3dsAlignTo0x80 (void *addr)
{
    if ((u32)addr & 0x7f)
        return (void *)(((u32)addr & ~0x7f) + 0x80);
    return addr;
}


void gpu3dsAllocVertexList(SVertexList *list, int sizeInBytes, int vertexSize,
    u8 totalAttributes, u64 attributeFormats)
{
    list->TotalAttributes = totalAttributes;
    list->AttributeFormats = attributeFormats;
    list->VertexSize = vertexSize;
    list->SizeInBytes = sizeInBytes;
    list->ListBase = (STileVertex *) linearAlloc(sizeInBytes);
    list->List = list->ListBase;
    list->ListOriginal = list->List;
    list->Total = 0;
    list->Count = 0;
    list->Flip = 1;
}

void gpu3dsDeallocVertexList(SVertexList *list)
{
    LINEARFREE_SAFE(list->ListBase);
}

void gpu3dsSwapVertexListForNextFrame(SVertexList *list)
{
    if (list->Flip)
        list->List = (void *)((uint32)(list->ListBase) + list->SizeInBytes / 2);
    else
        list->List = list->ListBase;
    list->ListOriginal = list->List;
    list->Flip = 1 - list->Flip;
    list->Total = 0;
    list->Count = 0;
    list->FirstIndex = 0;
    list->PrevCount = 0;
    list->PrevFirstIndex = 0;
}



void updateUniforms(SGPURenderState *state)
{
    // update projection based on current render target (screen or texture)
    if (state->updated & FLAG_TARGET) {
        if (state->target == TARGET_SCREEN) {
            u32 *projection = (screenSettings.GameScreen == GFX_TOP) ? (u32 *)GPU3DS.projectionTopScreen.m : (u32 *)GPU3DS.projectionBottomScreen.m;
            GPU_SetFloatUniform(GPU_VERTEX_SHADER, GPU3DS.shaderULocs[ULOC_PROJECTION], projection, 4);
        } else {
            SGPUTexture *texture = &GPU3DS.textures[(SGPU_TEXTURE_ID)state->target];

            if (texture != NULL) {
                GPU_SHADER_TYPE shaderType = state->shader != SPROGRAM_MODE7 ? GPU_VERTEX_SHADER : GPU_GEOMETRY_SHADER;
                GPU_SetFloatUniform(shaderType, GPU3DS.shaderULocs[ULOC_PROJECTION], (u32 *)texture->projection.m, 4);
            }
        }
        
        state->updated &= ~FLAG_TARGET;
    }

    if (state->updated & FLAG_TEXTURE_BIND) {
        SGPUTexture *texture = &GPU3DS.textures[state->textureBind];

        if (texture != NULL) {
            GPU_SHADER_TYPE shaderType2 = texture->id == SNES_MODE7_TILE_CACHE ? GPU_GEOMETRY_SHADER : GPU_VERTEX_SHADER;
            GPU_SetFloatUniform(shaderType2, GPU3DS.shaderULocs[ULOC_TEX_SCALE], (u32 *)texture->scale, 1);
        }
        
        state->updated &= ~FLAG_TEXTURE_BIND;
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


void applyRenderState()
{
    SGPURenderState *state = &GPU3DS.currentRenderState;

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

    updateUniforms(state);
}

void gpu3dsDrawVertexList(SVertexList *list, GPU_Primitive_t type, bool repeatLastDraw, int storeVertexListIndex, int storeIndex)
{
    if (!repeatLastDraw)
    {
        if (list->Count > 0)
        {
            //printf ("  DVL         : %8x count=%d\n", list->List, list->Count);
            gpu3dsSetAttributeBuffers(
                list->TotalAttributes,          // number of attributes
                (u32*)list->List,
                list->AttributeFormats
            );

            applyRenderState();
            GPU_DrawArray(type, 0, list->Count);

            // Save the parameters passed to the gpu3dsSetAttributeBuffers and GPU_DrawArray
            //
            if (storeVertexListIndex >= 0 && storeIndex >= 0)
            {
                GPU3DS.vertexesStored[storeVertexListIndex][storeIndex].TotalAttributes = list->TotalAttributes;
                GPU3DS.vertexesStored[storeVertexListIndex][storeIndex].List = list->List;
                GPU3DS.vertexesStored[storeVertexListIndex][storeIndex].AttributeFormats = list->AttributeFormats;
                GPU3DS.vertexesStored[storeVertexListIndex][storeIndex].Count = list->Count;
            }

            // Saves this just in case it can be re-used for windowing
            // or HDMA effects.
            //
            list->PrevCount = list->Count;
            list->PrevFirstIndex = list->FirstIndex;
            list->PrevList = list->List;

            u8 *p = (u8 *)list->List;
            list->List = (STileVertex *) gpu3dsAlignTo0x80(p + (list->Count * list->VertexSize));

            list->FirstIndex += list->Count;
            list->Total += list->Count;
            list->Count = 0;

            somethingWasDrawn = true;
        }
        else
        {
            // Save the parameters passed to the gpu3dsSetAttributeBuffers and GPU_DrawArray
            //
            if (storeVertexListIndex >= 0 && storeIndex >= 0)
            {
                GPU3DS.vertexesStored[storeVertexListIndex][storeIndex].Count = list->Count;
            }

        }
    }
    else
    {
        SStoredVertexList *list = &GPU3DS.vertexesStored[storeVertexListIndex][storeIndex];
        if (list->Count > 0)
        {
            //printf ("  DVL (repeat): %8x count=%d\n", list->List, list->Count);
            gpu3dsSetAttributeBuffers(
                list->TotalAttributes,          // number of attributes
                (u32*)list->List,
                list->AttributeFormats
            );

            applyRenderState();
	        GPU_DrawArray(type, 0, list->Count);

            somethingWasDrawn = true;
        }
    }
}


void gpu3dsDrawVertexList(SVertexList *list, GPU_Primitive_t type, int fromIndex, int tileCount)
{
    if (tileCount > 0)
    {
        gpu3dsSetAttributeBuffers(
            list->TotalAttributes,          // number of attributes
            (u32 *)list->List,
            list->AttributeFormats
        );


        applyRenderState();
        GPU_DrawArray(type, fromIndex, tileCount);

        somethingWasDrawn = true;
    }
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
	GPU_SetDepthTestAndWriteMask(false, GPU_GEQUAL, GPU_WRITE_ALL);

	GPUCMD_AddMaskedWrite(GPUREG_EARLYDEPTH_TEST1, 0x1, 0);
	GPUCMD_AddWrite(GPUREG_EARLYDEPTH_TEST2, 0);
	GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG, GPU_CULL_NONE&0x3);
    
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
	GPU_SetStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);

	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetAlphaBlending(
		GPU_BLEND_ADD,
		GPU_BLEND_ADD,
		GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
		GPU_ONE, GPU_ZERO
	);
	gpu3dsEnableAlphaTestNotEqualsZero();
    GPUCMD_AddWrite(GPUREG_TEXUNIT0_BORDER_COLOR, 0);
    gpu3dsSetTextureEnvironmentReplaceTexture0();
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


bool gpu3dsUseShader(SGPU_SHADER_PROGRAM shaderIndex)
{
    if (gpu3dsUpdateRenderState(&GPU3DS.currentRenderState, FLAG_SHADER, (u32)shaderIndex, (u32)GPU3DS.currentRenderState.shader))
    {
        shaderProgramUse(&GPU3DS.shaders[shaderIndex].shaderProgram);

        return true; 
    }

    return false;
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
	GPU_DepthMap(-1.0f, 0.0f);
	GPUCMD_AddWrite(GPUREG_FACECULLING_CONFIG, GPU_CULL_NONE&0x3);
	GPU_SetStencilTest(false, GPU_ALWAYS, 0x00, 0xFF, 0x00);
	GPU_SetStencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
	GPU_SetBlendingColor(0,0,0,0);
	GPU_SetDepthTestAndWriteMask(false, GPU_GEQUAL, GPU_WRITE_ALL);
	GPUCMD_AddMaskedWrite(GPUREG_EARLYDEPTH_TEST1, 0x1, 0);
	GPUCMD_AddWrite(GPUREG_EARLYDEPTH_TEST2, 0);

	GPU_SetAlphaBlending(
		GPU_BLEND_ADD,
		GPU_BLEND_ADD,
		GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA,
		GPU_ONE, GPU_ZERO
	);

	GPU_SetAlphaTest(true, GPU_NOTEQUAL, 0x00);

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

void gpu3dsSetRenderTargetToFrameBuffer(gfxScreen_t screenId)
{
    if (gpu3dsUpdateRenderState(&GPU3DS.currentRenderState, FLAG_TARGET, (u32)TARGET_SCREEN, (u32)GPU3DS.currentRenderState.target))
    {
        GPU_SetViewport(
            (u32 *)osConvertVirtToPhys(GPU3DS.frameDepthBuffer),
            (u32 *)osConvertVirtToPhys(GPU3DS.frameBuffer),
            0, 0, SCREEN_HEIGHT, (screenId == GFX_TOP) ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH);

        GPUCMD_AddSingleParam(0x000F0117, GPUREG_COLORBUFFER_FORMAT_VALUES[GPU3DS.frameBufferFormat]); //color buffer format
    }
}

void gpu3dsSetRenderTargetToTexture(SGPU_TEXTURE_ID textureId)
{
    if (gpu3dsUpdateRenderState(&GPU3DS.currentRenderState, FLAG_TARGET, (u32)textureId, (u32)GPU3DS.currentRenderState.target))
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
        GPUCMD_AddSingleParam(0x000F0117, GPUREG_COLORBUFFER_FORMAT_VALUES[texture->tex.fmt]); //color buffer format
    }
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

        gpu3dsUpdateRenderState(&GPU3DS.currentRenderState, FLAG_TARGET, (u32)SNES_MODE7_FULL, (u32)GPU3DS.currentRenderState.target);
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


void gpu3dsBindTexture(SGPU_TEXTURE_ID textureId, u32 param)
{
    SGPUTexture *texture = &GPU3DS.textures[textureId];

    // if param is not set use last applied param
    // for SNES_MAIN texture and SNES_MODE7_FULL texture params may change dynamically
    // (e.g. switch from pixel perfect to stretched view and vice versa)
    if (!param)
        param = texture->tex.param;

    if (GPU3DS.currentRenderState.textureBind != textureId || GPU3DS.currentRenderState.textureParams != param)
    {
        texture->tex.param = param;
        GPU_SetTextureEnable(GPU_TEXUNIT0);

        GPU_SetTexture(
            GPU_TEXUNIT0,
            (u32 *)osConvertVirtToPhys(texture->tex.data),
            texture->tex.width,
            texture->tex.height,
            texture->tex.param,
            texture->tex.fmt
        );
        
        gpu3dsUpdateRenderState(&GPU3DS.currentRenderState, FLAG_TEXTURE_BIND, (u32)textureId, (u32)GPU3DS.currentRenderState.textureBind);
        gpu3dsUpdateRenderState(&GPU3DS.currentRenderState, FLAG_TEXTURE_PARAMS, (u32)param, (u32)GPU3DS.currentRenderState.textureParams);
    }
}