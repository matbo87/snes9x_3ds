
#include "snes9x.h"
#include "ppu.h"

#include <3ds.h>
#include "3dsgpu.h"
#include "3dsimpl.h"
#include "3dsimpl_gpu.h"
#include "3dslog.h"
#include "3dssettings.h"

SGPU3DSExtended GPU3DSExt;

// Depth values from DRAW_* macros in S9xRenderScreenHardware() (gfxhw.cpp).
// Table: snesDepthTable[mode][bg] = {depth0, depth1}.
// These are the compositing priority values the emulator uses for draw ordering.
// Higher depth = drawn later = visually in front.
static const struct { int d0, d1; } snesDepthTable[8][4] = {
    // Mode 0: 4 BG layers
    {{8,11}, {7,10}, {2,5}, {1,4}},
    // Mode 1: 3 BG layers (BG2 d1 is runtime-dependent, handled below)
    {{8,11}, {7,10}, {2,5}, {0,0}},
    // Mode 2
    {{5,11}, {2,8},  {0,0}, {0,0}},
    // Mode 3
    {{5,11}, {2,8},  {0,0}, {0,0}},
    // Mode 4
    {{5,11}, {2,8},  {0,0}, {0,0}},
    // Mode 5
    {{5,11}, {2,8},  {0,0}, {0,0}},
    // Mode 6: 1 BG layer
    {{5,11}, {0,0},  {0,0}, {0,0}},
    // Mode 7: mono (geometry shader skips stereo offset)
    {{0,0},  {0,0},  {0,0}, {0,0}},
};

// Sprite compositing priorities interleave with BG layers at depths ~3,6,9,12.
// Average ~6 serves as the screen plane reference (zero parallax).
static const float STEREO_SCREEN_PLANE = 6.0f;

// OBJ priority depths from SNES hardware compositing order.
// Formula: (priority + 1) * 3, matching the emulator's priorityOffset calculation.
// OBJ.0=3, OBJ.1=6, OBJ.2=9, OBJ.3=12
static const float objPriorityDepth[4] = { 3.0f, 6.0f, 9.0f, 12.0f };

// Per-priority OBJ depth factor. Same formula as BG layers:
// (SCREEN_PLANE - depth) / SCREEN_PLANE
// OBJ.0: (6-3)/6 = +0.5  (behind screen — near far BGs)
// OBJ.1: (6-6)/6 =  0.0  (at screen plane)
// OBJ.2: (6-9)/6 = -0.5  (in front of screen — near player)
// OBJ.3: (6-12)/6 = -1.0 (pops forward — frontmost)
static float getStereoObjDepthFactor(int priority) {
    if (priority < 0 || priority > 3) return 0.0f;
    return (STEREO_SCREEN_PLANE - objPriorityDepth[priority]) / STEREO_SCREEN_PLANE;
}

// Map SNES layer to stereo depth factor using the emulator's own depth values.
// Positive = recedes into screen, negative = pops toward viewer.
static float getStereoDepthFactor(LAYER_ID id) {
    if (id == LAYER_OBJ)       return 0.0f;  // per-section override in draw loop
    if (id == LAYER_BACKDROP)  return 1.0f;   // behind everything
    if (id >= LAYER_COLOR_MATH) return 0.0f;  // full-screen effects, no offset

    int bg = (int)id;
    int mode = PPU.BGMode;
    if (mode > 7) return 0.0f;

    int d0 = snesDepthTable[mode][bg].d0;
    int d1 = snesDepthTable[mode][bg].d1;

    // Mode 1 BG2: depth1 depends on BG3Priority flag at runtime
    if (mode == 1 && bg == 2)
        d1 = PPU.BG3Priority ? 13 : 5;

    if (d0 == 0 && d1 == 0) return 0.0f;  // layer not used in this mode

    float avgDepth = (d0 + d1) / 2.0f;
    return (STEREO_SCREEN_PLANE - avgDepth) / STEREO_SCREEN_PLANE;
}

static float getStereoLayerScale(LAYER_ID id) {
    switch (id) {
        case LAYER_BG0:      return settings3DS.StereoBG0Scale / 20.0f;
        case LAYER_BG1:      return settings3DS.StereoBG1Scale / 20.0f;
        case LAYER_BG2:      return settings3DS.StereoBG2Scale / 20.0f;
        case LAYER_BG3:      return settings3DS.StereoBG3Scale / 20.0f;
        case LAYER_OBJ:      return settings3DS.StereoOBJScale / 20.0f;
        case LAYER_BACKDROP: return settings3DS.StereoBackdropScale / 20.0f;
        default:             return 1.0f;
    }
}

void gpu3dsDeallocLayers()
{
    SLayerList *list = &GPU3DSExt.layerList;

    linearFree(list->sections);
    list->sections = nullptr;
    linearFree(list->ibo);
    list->ibo = nullptr;
}

void gpu3dsResetLayer(SLayer *layer) {
    layer->sectionsByTarget[TARGET_SNES_MAIN] = 0;
    layer->verticesByTarget[TARGET_SNES_MAIN] = 0;

    layer->sectionsByTarget[TARGET_SNES_SUB] = 0;
    layer->verticesByTarget[TARGET_SNES_SUB] = 0;
    
    layer->sectionsTotal = 0;
    layer->m7Tile0 = false;
}

void gpu3dsResetLayers(SLayerList *list) {
    list->verticesTotal = 0;
    list->anythingOnSub = false;
    list->layersTotalByTarget[TARGET_SNES_SUB] = 0;
    list->layersTotalByTarget[TARGET_SNES_MAIN] = 0;
            
    for (int i = 0; i < LAYERS_COUNT; i++) {
        gpu3dsResetLayer(&list->layers[i]);
    }
}

// reset to initial state when loading a game
void gpu3dsResetLayerSectionLimits(SLayerList *list) {
    list->sectionsMax = 0;

    for (int i = 0; i < LAYERS_COUNT; i++) 
    {
        SLayer *layer = &list->layers[i];

        layer->sectionsOffset = list->sectionsMax;

        switch (i) {
            case LAYER_WINDOW_LR:
            case LAYER_BRIGHTNESS:
                layer->sectionsMax = 1; // always 0-1 section
                break;
            case LAYER_BACKDROP:
                layer->sectionsMax = 2; // always 0-2 sections
                break;
            default:
                layer->sectionsMax = 128; 
                break;
        }

        list->sectionsMax += layer->sectionsMax;
    }
}

void gpu3dsAdjustLayerSectionLimits(SLayerList *list) {
    u16 threshold = 16;
    u16 allowedSectionsMax = list->sectionsSizeInBytes / sizeof(SLayerSection);
    u16 newSectionsMax = 0;

    for (int i = 0; i < LAYERS_COUNT; i++) {
        SLayer *layer = &list->layers[i];

        layer->sectionsOffset = newSectionsMax;

        if (layer->sectionsSkipped) {
            layer->sectionsMax += (layer->sectionsSkipped < threshold ? threshold : layer->sectionsSkipped);
        }
        
        newSectionsMax += layer->sectionsMax;
    }

    if (newSectionsMax <= allowedSectionsMax) {
        list->sectionsMax = newSectionsMax;

        return;
    }

    // if newSectionsMax > allowedSectionsMax, we have to reduce layer->sectionsMax for !layer->sectionsSkipped layers
    // so that newSectionsMax still fits into the allocated memory
    // (e.g. color math layer has sectionsMax = 128 but only 7 sections are needed for the current frame)
    //
    // for convinience we only do this for color math,obj and bg0-bg3 in prioritized order
    // because those layers will have the highest number of unused sections
    u16 sectionsToReduce = newSectionsMax - allowedSectionsMax;
    u16 reducedSections = 0;

    LAYER_ID order[6] = {
        LAYER_COLOR_MATH,
        LAYER_OBJ,
        LAYER_BG3,
        LAYER_BG2,
        LAYER_BG1,
        LAYER_BG0,
    };

    for (int i = 0; i < 6; i++) {
        SLayer *layer = &list->layers[order[i]];

        if (!layer->sectionsSkipped) {
            u16 newMax = layer->sectionsTotal < threshold ? threshold : layer->sectionsTotal;
            reducedSections += layer->sectionsMax - newMax;
            layer->sectionsMax = newMax;
        }

        if (sectionsToReduce <= reducedSections)
            break;
    }

    newSectionsMax = 0;
    for (int i = 0; i < LAYERS_COUNT; i++) {
        SLayer *layer = &list->layers[i];

        // just in case if we still exceed the max limit (should never happen)
        if (newSectionsMax + layer->sectionsMax > allowedSectionsMax) {
            layer->sectionsMax = 0;
        }

        layer->sectionsOffset = newSectionsMax;

        if (layer->sectionsSkipped) {
            layer->sectionsSkipped = false;
        }

        newSectionsMax += layer->sectionsMax;
    }

    list->sectionsMax = newSectionsMax;
}

u64 gpu3dsGetLayerPackedMask(LAYER_ID id, bool firstSection) {
    if (id == LAYER_OBJ)
    {
        return firstSection
            ? PACKED_MASK_TEX_BIND | PACKED_MASK_STENCIL | PACKED_MASK_ALPHA_TEST
            : PACKED_MASK_STENCIL;
    }

    if (id == LAYER_BG0 || id == LAYER_BG1)
    {
        return PACKED_MASK_TEX_BIND
            | PACKED_MASK_STENCIL
            | PACKED_MASK_ALPHA_TEST
            | PACKED_MASK_TEX_OFFSET;
    }

    if (id == LAYER_BG2 || id == LAYER_BG3)
    {
        return firstSection
            ? PACKED_MASK_TEX_BIND | PACKED_MASK_STENCIL | PACKED_MASK_ALPHA_TEST
            : PACKED_MASK_STENCIL;
    }

    return 0;
}

void gpu3dsInitLayers() {
    SLayerList *list = &GPU3DSExt.layerList;

    list->sizeInBytes = gpu3dsGetNextPowerOf2(MAX_VERTICES * sizeof(u16));
    list->ibo = linearAlloc(list->sizeInBytes);

    gpu3dsResetLayers(list);

    for (int i = 0; i < LAYERS_COUNT; i++) 
    {
        LAYER_ID id = LAYER_ID(i);
        SLayer *layer = &list->layers[id];

        layer->id = id;
    }

    list->sectionsSizeInBytes = gpu3dsGetNextPowerOf2(list->sectionsMax * sizeof(SLayerSection));
    list->sections = (SLayerSection *)linearAlloc(list->sectionsSizeInBytes);
			
    log3dsWrite("ibo size: %dkb, sections size: %dkb",
        list->sizeInBytes / 1024,
        list->sectionsSizeInBytes / 1024
    );
}

int compareSections(const SLayerSection *a, const SLayerSection *b, bool tile0) {
    // First, compare target: TARGET_SNES_SUB should come first
    if (a->onSub != b->onSub) {
        return a->onSub ? -1 : 1;
    }
    
    // If targets are equal, compare texture: SNES_MODE7_TILE_0 should come first
    if (tile0 && a->state.textureBind != b->state.textureBind) {
        return (a->state.textureBind == SNES_MODE7_TILE_0) ? -1 : 1;
    }

    return 0;
}

void sortSections(SLayerSection *sections, int n, bool tile0) {
    for (int i = 1; i < n; i++) {
        SLayerSection section = sections[i];
        int j = i - 1;
        while (j >= 0 && compareSections(&section, &sections[j], tile0) < 0) {
            sections[j + 1] = sections[j];
            j--;
        }
        sections[j + 1] = section;
    }
}

// window_lr, backdrop, color math, brightness
void gpu3dsDrawLayer(SLayer *layer, int from, int to) {
    SLayerList *list = &GPU3DSExt.layerList;

    u64 mask = PACKED_MASK_TEX_ENV
        | PACKED_MASK_STENCIL
        | PACKED_MASK_ALPHA_TEST
        | PACKED_MASK_ALPHA_BLEND;

    if (layer->id == LAYER_COLOR_MATH)
        mask |= PACKED_MASK_TEX_BIND;

    for (int i = from; i < to; i++) {
        SLayerSection *section = &list->sections[i];

        GPU3DS.currentRenderState.packed =
            (GPU3DS.currentRenderState.packed & ~mask) | (section->state.packed & mask);
        gpu3dsDraw(&GPU3DS.vertices[section->vboId], NULL, section->count, section->from);
    }
}

// obj, bg0-bg3
void gpu3dsDrawLayerByIndices(SLayer *layer, u16 *indices, int from, int to) {
    SLayerList *list = &GPU3DSExt.layerList;
    u16 *sectionIndices = indices;
    u16 batchFrom = 0;
    u16 batchCount = 0;

    // Those fields are set once before the loop and stay constant 
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA;
    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;
    
    // restrict the diff to only the properties that actually vary across sections for that layer type
    u64 layerMask[2];
    layerMask[0] = gpu3dsGetLayerPackedMask(layer->id, true);  // first section
    layerMask[1] = gpu3dsGetLayerPackedMask(layer->id, false); // subsequent sections

    // init from first section
    SLayerSection *first = &list->sections[from];
    SGPU_VBO_ID vboId = first->vboId;
    GPU3DS.currentRenderState.packed =
        (GPU3DS.currentRenderState.packed & ~layerMask[0]) | (first->state.packed & layerMask[0]);

    for (int idx = from; idx < to; idx++) {
        SLayerSection *section = &list->sections[idx];
        u16 sFrom = section->from;
        u16 sCount = section->count;

        // batch break on state change
        if (idx > from) {
            bool changed = (GPU3DS.currentRenderState.packed ^ section->state.packed) & layerMask[1];

            if (changed) {
                gpu3dsDraw(&GPU3DS.vertices[vboId], (void *)(indices + batchFrom), batchCount);
                vboId = section->vboId;
                GPU3DS.currentRenderState.packed =
                    (GPU3DS.currentRenderState.packed & ~layerMask[1]) | (section->state.packed & layerMask[1]);
                batchFrom += batchCount;
                batchCount = 0;
            }
        }

        // build sequential indices
        u16 *dst = indices + batchFrom + batchCount;
        int i = 0;
        for (; i <= sCount - 4; i += 4) {
            dst[i]     = sFrom + i;
            dst[i + 1] = sFrom + i + 1;
            dst[i + 2] = sFrom + i + 2;
            dst[i + 3] = sFrom + i + 3;
        }
        for (; i < sCount; i++) {
            dst[i] = sFrom + i;
        }

        batchCount += sCount;
    }

    // draw final batch
    gpu3dsDraw(&GPU3DS.vertices[vboId], (void *)(indices + batchFrom), batchCount);
}

void gpu3dsDrawLayers(SLayerList *list) {
    bool stereoEnabled = gpu3dsIs3DEnabled();
    float iod = stereoEnabled ? gpu3dsGetIOD() : 0.0f;
    int eyeCount = stereoEnabled ? 2 : 1;

    // One-shot stereo diagnostics (deferred until PPU.BGMode is valid)
    // Reset on stereo off→on transition or when logging is toggled on
    static int stereoFrameCount = 0;
    static bool stereoLoggedOnce = false;
    static bool stereoWasEnabled = false;
    static bool loggingWasEnabled = false;
    static u32 lastRomCRC = 0;
    if ((Memory.ROMCRC32 != lastRomCRC) ||
        (stereoEnabled && !stereoWasEnabled) ||
        (settings3DS.LogFileEnabled && !loggingWasEnabled)) {
        stereoFrameCount = 0;
        stereoLoggedOnce = false;
    }
    lastRomCRC = Memory.ROMCRC32;
    stereoWasEnabled = stereoEnabled;
    loggingWasEnabled = settings3DS.LogFileEnabled;
    if (stereoEnabled && !stereoLoggedOnce) {
        stereoFrameCount++;
        // Wait a few frames for the PPU to commit the real BG mode
        if (stereoFrameCount >= 10) {
            stereoLoggedOnce = true;
            int effectiveWidth = (settings3DS.ScreenStretch == Setting::ScreenStretch::Fit_8_7
                && PPU.ScreenHeight >= SNES_HEIGHT_EXTENDED)
                ? SNES_WIDTH : settings3DS.StretchWidth;
            float stretchCompensation = 256.0f / effectiveWidth;
            log3dsWrite("[stereo] ENABLED iod=%.3f eyes=%d mode=%d stretchW=%d effectiveW=%d stretchComp=%.3f",
                iod, eyeCount, PPU.BGMode, settings3DS.StretchWidth, effectiveWidth, stretchCompensation);
            for (int l = LAYER_BG0; l <= LAYER_BACKDROP; l++) {
                LAYER_ID lid = (LAYER_ID)l;
                float ls = getStereoLayerScale(lid);
                if (lid < LAYER_OBJ) {
                    int bg = (int)lid;
                    int mode = PPU.BGMode;
                    int d0 = (mode <= 7) ? snesDepthTable[mode][bg].d0 : 0;
                    int d1 = (mode <= 7) ? snesDepthTable[mode][bg].d1 : 0;
                    if (mode == 1 && bg == 2)
                        d1 = PPU.BG3Priority ? 13 : 5;
                    float common = ls * iod * stretchCompensation * (2.0f / 256.0f);
                    float zScale = common * 16.0f / STEREO_SCREEN_PLANE;
                    log3dsWrite("[stereo]   BG%d: d0=%d d1=%d scale=%.3f base=%.6f zScale=%.6f",
                        bg, d0, d1, ls, common, zScale);
                } else {
                    float df = getStereoDepthFactor(lid);
                    float finalOffset = df * ls * iod * stretchCompensation * (2.0f / 256.0f);
                    log3dsWrite("[stereo]   layer %d: depth=%.3f scale=%.3f offset=%.6f", l, df, ls, finalOffset);
                }
            }
        }
    }

    // Draw window_lr into shared depth buffer once (both eyes use the same clip regions)
    gpu3dsSetStereoOffset(0.0f);
    SLayer *windowLayer = &list->layers[LAYER_WINDOW_LR];

    if (windowLayer->verticesByTarget[0]) {
        GPU3DS.currentRenderState.target = TARGET_SNES_DEPTH;
        gpu3dsDrawLayer(windowLayer,
            windowLayer->sectionsOffset,
            windowLayer->sectionsOffset + windowLayer->sectionsByTarget[TARGET_SNES_MAIN]);
    }

    // Draw sub-screen layers once (mono — used for transparency effects)
    if (list->anythingOnSub) {
        GPU3DS.currentRenderState.target = TARGET_SNES_SUB;

        for (int j = 0; j < list->layersTotalByTarget[TARGET_SNES_SUB]; j++) {
            LAYER_ID id = list->layersByTarget[TARGET_SNES_SUB][j];
            SLayer *layer = &list->layers[id];

            GPU3DS.currentRenderState.depthTest =
                id < LAYER_OBJ ? SGPU_STATE_ENABLED : SGPU_STATE_DISABLED;

            int from = layer->sectionsOffset;
            int to = from + layer->sectionsByTarget[TARGET_SNES_SUB];

            if (id < LAYER_BACKDROP) {
                u16 *indices = (u16 *)list->ibo + layer->bufferOffset;
                gpu3dsDrawLayerByIndices(layer, indices, from, to);
            } else {
                gpu3dsDrawLayer(layer, from, to);
            }
        }
    }

    // Stretch compensation: when the SNES texture is stretched wider on screen
    // (e.g. 400px full), the same clip-space offset produces more physical pixels
    // of parallax. Multiply by (256/stretchWidth) to keep perceived depth constant.
    // Fit_8_7 mode overrides sWidth to 256 when PPU.ScreenHeight >= 239,
    // but StretchWidth stays at 274. Use 256 in that case to match.
    int effectiveWidth = (settings3DS.ScreenStretch == Setting::ScreenStretch::Fit_8_7
        && PPU.ScreenHeight >= SNES_HEIGHT_EXTENDED)
        ? SNES_WIDTH : settings3DS.StretchWidth;
    float stretchCompensation = 256.0f / effectiveWidth;

    // IBO capacity for bounds checking in per-section OBJ draw
    u32 iboCapacity = list->sizeInBytes / sizeof(u16);

    // Draw main-screen layers per eye (stereo when enabled)
    for (int eye = 0; eye < eyeCount; eye++) {
        bool rightEye = (eye == 1);
        float eyeSign = rightEye ? -1.0f : 1.0f;

        GPU3DS.stereoRightEye = rightEye;

        if (rightEye) {
            // Force render target re-application for right-eye redirect
            GPU3DS.appliedRenderState.target = TARGET_COUNT;
            GPU3DS.currentRenderTargetDim = 0;

            // Clear the shared depth buffer between eye passes.
            // SNES_DEPTH serves as both the color target for window_lr clip regions
            // and the depth attachment for SNES_MAIN/SNES_MAIN_R. Left-eye BG layers
            // wrote depth values (GPU_WRITE_ALL) that would corrupt right-eye depth testing.
            // Clear both color and depth+stencil to prevent stencil leaking between eyes.
            C3D_RenderTargetClear(GPU3DS.textures[SNES_DEPTH].target, C3D_CLEAR_ALL, 0, 0);
            C3D_FrameDrawOn(GPU3DS.textures[SNES_DEPTH].target);

            // Re-render window_lr to restore clip regions in the depth buffer.
            if (windowLayer->verticesByTarget[0]) {
                GPU3DS.currentRenderState.target = TARGET_SNES_DEPTH;
                gpu3dsDrawLayer(windowLayer,
                    windowLayer->sectionsOffset,
                    windowLayer->sectionsOffset + windowLayer->sectionsByTarget[TARGET_SNES_MAIN]);
            }
        }

        GPU3DS.currentRenderState.target = TARGET_SNES_MAIN;

        for (int j = 0; j < list->layersTotalByTarget[TARGET_SNES_MAIN]; j++) {
            LAYER_ID id = list->layersByTarget[TARGET_SNES_MAIN][j];
            SLayer *layer = &list->layers[id];

            GPU3DS.currentRenderState.depthTest =
                id < LAYER_OBJ ? SGPU_STATE_ENABLED : SGPU_STATE_DISABLED;

            int from = layer->sectionsOffset + layer->sectionsByTarget[TARGET_SNES_SUB];
            int to = from + layer->sectionsByTarget[TARGET_SNES_MAIN];

            if (stereoEnabled && id == LAYER_OBJ) {
                // Per-section stereo offset: each OBJ section has a priority tag.
                // Draw each section individually through gpu3dsDrawLayerByIndices
                // so render state and IBO index management stay correct.
                float layerScale = getStereoLayerScale(LAYER_OBJ);
                u32 bufferOffset = layer->bufferOffset + layer->verticesByTarget[TARGET_SNES_SUB];
                u16 *indices = (u16 *)list->ibo + bufferOffset;

                static int objDrawLogCount = 0;
                static bool objLoggingWasEnabled = false;
                static u32 lastRomCRCObj = 0;
                if ((settings3DS.LogFileEnabled && !objLoggingWasEnabled) ||
                    (Memory.ROMCRC32 != lastRomCRCObj))
                    objDrawLogCount = 0;
                lastRomCRCObj = Memory.ROMCRC32;
                objLoggingWasEnabled = settings3DS.LogFileEnabled;
                bool doLog = (objDrawLogCount < 20);

                if (doLog) {
                    log3dsWrite("[OBJ-DRAW] eye=%d from=%d to=%d sections=%d bufOff=%u subVerts=%d mainVerts=%d layerScale=%.2f",
                        eye, from, to, to - from, bufferOffset,
                        layer->verticesByTarget[TARGET_SNES_SUB],
                        layer->verticesByTarget[TARGET_SNES_MAIN], layerScale);
                }

                for (int s = from; s < to; s++) {
                    SLayerSection *section = &list->sections[s];

                    if (doLog) {
                        log3dsWrite("[OBJ-DRAW]   s[%d] prio=%d count=%d from=%d vbo=%d packed=0x%llx",
                            s, section->objPriority, section->count, section->from,
                            section->vboId, (unsigned long long)section->state.packed);
                    }

                    if (!section->count) continue;

                    // Bounds check: ensure this section's indices fit in the IBO
                    u32 iboOffset = (u32)(indices - (u16 *)list->ibo);
                    if (iboOffset + section->count > iboCapacity)
                        break;

                    float depthFactor = getStereoObjDepthFactor(section->objPriority);
                    gpu3dsSetStereoOffset(depthFactor * layerScale * iod * eyeSign * stretchCompensation * (2.0f / 256.0f));

                    gpu3dsDrawLayerByIndices(layer, indices, s, s + 1);
                    // Advance IBO pointer past this section's indices so the next
                    // section writes to fresh memory. C3D_DrawElements references
                    // index data by physical address — the GPU reads it later during
                    // frame flush, so we must not overwrite it.
                    indices += section->count;
                }

                if (doLog) objDrawLogCount++;
            } else {
                if (stereoEnabled) {
                    if (PPU.BGMode == 7 && id <= LAYER_BG1) {
                        // Mode 7 BG: geometry shader applies per-scanline Y-scaled depth.
                        float mode7Scale = settings3DS.StereoMode7Scale / 20.0f;
                        gpu3dsSetStereoOffset(mode7Scale * iod * eyeSign * stretchCompensation * (1.0f / 256.0f));
                    } else if (id < LAYER_OBJ) {
                        // BG layers: per-tile depth from Z coordinate.
                        // Geometry shader computes: offset = base + zScale * projectedZ
                        // where projectedZ = -d/16 (d = compositing depth 0-16).
                        float layerScale = getStereoLayerScale(id);
                        float common = layerScale * iod * eyeSign * stretchCompensation * (2.0f / 256.0f);
                        float zScale = common * 16.0f / STEREO_SCREEN_PLANE;
                        gpu3dsSetStereoOffset(common, zScale);
                    } else {
                        // BACKDROP: uses averaged depth factor (constant offset, zScale=0).
                        float depthFactor = getStereoDepthFactor(id);
                        float layerScale = getStereoLayerScale(id);
                        gpu3dsSetStereoOffset(depthFactor * layerScale * iod * eyeSign * stretchCompensation * (2.0f / 256.0f));
                    }
                }

                if (id < LAYER_BACKDROP) {
                    u32 bufferOffset = layer->bufferOffset + layer->verticesByTarget[TARGET_SNES_SUB];
                    u16 *indices = (u16 *)list->ibo + bufferOffset;
                    gpu3dsDrawLayerByIndices(layer, indices, from, to);
                } else {
                    gpu3dsDrawLayer(layer, from, to);
                }
            }
        }
    }

    // Reset stereo state
    GPU3DS.stereoRightEye = false;
    gpu3dsSetStereoOffset(0.0f);
}

void gpu3dsDrawMode7Texture()
{
    if (!IPPU.Mode7Prepared || !GPU3DSExt.mode7TilesModified) return;

	t3dsStartTimer(TIMER_DRAW_M7_TEXTURE);
	gpu3dsSetMode7TexturesPixelFormat(IPPU.Mode7EXTBGFlag ? GPU_RGBA4 : GPU_RGBA5551);

	GPU3DS.currentRenderState.textureBind = SNES_MODE7_TILE_CACHE;
	GPU3DS.currentRenderState.shader = SPROGRAM_MODE7;
	GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0;
	GPU3DS.currentRenderState.stencilTest = STENCIL_TEST_DISABLED;
	GPU3DS.currentRenderState.alphaTest = ALPHA_TEST_DISABLED;
	GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;

	SVertexList *list = &GPU3DS.vertices[VBO_MODE7_TILE];
    SGPUTexture *texture = &GPU3DS.textures[SNES_MODE7_FULL];

    // 3DS does not allow rendering to a viewport whose width > 512
    // so our 1024x1024 texture is split into 4 512x512 parts
	GPU3DS.currentRenderState.target = TARGET_SNES_MODE7_FULL;

	for (int section = 0; section < 4; section++)
	{
		if (GPU3DSExt.mode7SectionsModified[section])
		{
			GPU3DSExt.mode7SectionsModified[section] = false;

    		// if we draw all 4 sections, mode7 texture is not visible on citra.
    		// This seems to be a bug in citra’s handling of rendering to multiple regions of a single render target?
    		// skipping one section will display at least a part of the texture
			if (!GPU3DS.isReal3DS && section == 0)
				continue;

			// Invalidate applied target to force re-apply (framebuf address changes per section)
			GPU3DS.appliedRenderState.target = TARGET_COUNT;
			int addressOffset = ((3 - section) * 0x40000) * gpu3dsGetPixelSize(texture->tex.fmt);
    		texture->target->frameBuf.colorBuf = (void *)((int)texture->tex.data + addressOffset);

			gpu3dsDraw(list, NULL, 4096, 4096 * section);
		}
	}

	GPU3DSExt.mode7TilesModified = false;

	GPU3DS.currentRenderState.target = TARGET_SNES_MODE7_TILE_0;
	gpu3dsDraw(list, NULL, 4, 16384);

	// re-bind our tile shader
	GPU3DS.currentRenderState.shader = SPROGRAM_TILES;

	t3dsStopTimer(TIMER_DRAW_M7_TEXTURE);

    gpu3dsIncrementMode7UpdateFrameCount();
}

void gpu3dsPrepareSnesScreenForNextFrame() {
    SLayerList *list = &GPU3DSExt.layerList;
    
    if (list->hasSkippedSections) {
        gpu3dsAdjustLayerSectionLimits(list);
        gpu3dsResetLayers(list);
        list->hasSkippedSections = false;
    }

    // flip snes VBOs to the alternate half of the buffer
    // make sure this is called BEFORE S9xMainLoop so that vertex writes go to different memory
	gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_SCENE_RECT], true);
	gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_SCENE_TILE], true);
	gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_SCENE_MODE7_LINE], true);
}

void gpu3dsDrawSnesScreen() {
    SLayerList *list = &GPU3DSExt.layerList;

    if (!list->verticesTotal || list->hasSkippedSections)
        return;

    LAYER_ID drawOrder[8] = {
        LAYER_BACKDROP,
        LAYER_OBJ,
        LAYER_BG0,
        LAYER_BG1,
        LAYER_BG2,
        LAYER_BG3,
        LAYER_COLOR_MATH,
        LAYER_BRIGHTNESS,
    };

    u32 bufferOffset = 0;
    
    for (int i = 0; i < 8; i++) {
        LAYER_ID id = drawOrder[i];

        SLayer *layer = &list->layers[id];
        
        u16 verticesOnSub = layer->verticesByTarget[TARGET_SNES_SUB];
        u16 verticesOnMain = layer->verticesByTarget[TARGET_SNES_MAIN];

        int verticesTotal = verticesOnSub + verticesOnMain;

        if (!verticesTotal) {
            continue;
        }

        if (verticesOnMain) {
            list->layersByTarget[TARGET_SNES_MAIN][list->layersTotalByTarget[TARGET_SNES_MAIN]++] = id;
        }

        if (verticesOnSub) {
            list->layersByTarget[TARGET_SNES_SUB][list->layersTotalByTarget[TARGET_SNES_SUB]++] = id;
            list->anythingOnSub = true;
        }


        if ((verticesOnMain && verticesOnSub) || layer->m7Tile0) {
            sortSections(list->sections + layer->sectionsOffset, layer->sectionsTotal, layer->m7Tile0);
        }
        
        if (id < LAYER_BACKDROP)
        {
            layer->bufferOffset = bufferOffset;
            bufferOffset += verticesTotal;
        }
    }

    gpu3dsDrawMode7Texture();
    gpu3dsDrawLayers(list);
    gpu3dsResetLayers(list);
}

void gpu3dsCommitLayerSection(SGPU_VBO_ID vboId, LAYER_ID id, SGPURenderState *state, bool sub, bool reuseVertices, u8 objPriority) {
    SLayerList *list = &GPU3DSExt.layerList;
    SLayer *layer = &list->layers[id];

    if (layer->sectionsTotal >= layer->sectionsMax) {
        // skip current frame + count all the skipped sections 
        // to handle layer section limits for the next frame later on (gpu3dsAdjustLayerSectionLimits())
        //
        // This case should rarely happen (and never for LAYER_WINDOW_LR, LAYER_BRIGHTNESS, LAYER_BACKDROP)
        // If at all, it occurs when "In-Frame Pallete Changes" setting is set to "Enabled"
        layer->sectionsSkipped++;
        list->hasSkippedSections = true;
    }   

    int sectionIdx = layer->sectionsOffset + layer->sectionsTotal;

    if (!reuseVertices)
    {
        SVertexList *vbo = &GPU3DS.vertices[vboId];
        u16 currentIdx = vbo->from;
        u16 currentVerticesCount = gpu3dsGetValueWithinLimit(vbo->count, list->verticesTotal, MAX_VERTICES);

        vbo->from += vbo->count;
        vbo->count = 0;

        // max sections/vertices overflow
        if (list->hasSkippedSections || !currentVerticesCount) {
            return;
        }

        SLayerSection *section = &list->sections[sectionIdx];

        section->state = *state;
        section->from = currentIdx;
        section->count = currentVerticesCount;
        section->vboId = vboId;
        section->onSub = sub;
        section->objPriority = objPriority;

        if (state->textureBind == SNES_MODE7_TILE_0) {
            layer->m7Tile0 = true;
        }
        
        layer->verticesByTarget[sub] += currentVerticesCount;
        layer->sectionsByTarget[sub]++;
        layer->sectionsTotal++;
        
        list->verticesTotal += currentVerticesCount;
    
        return;
    }

    // For OBJ per-priority reuse, copy from the matching sub-screen section
    // (indexed by objPriority), not from the previous section.
    int prevSectionIndex = (id == LAYER_OBJ)
        ? layer->sectionsOffset + objPriority
        : sectionIdx - 1;

    if (prevSectionIndex >= 0 && !list->hasSkippedSections)
    {
        SLayerSection *section = &list->sections[sectionIdx]; // reuse last section properties

        *section = list->sections[prevSectionIndex];

        if (!section->count) {
            return;
        }

        section->state = *state;
        section->onSub = false; // reuse only happens on main
        section->objPriority = objPriority;

        layer->sectionsByTarget[TARGET_SNES_MAIN]++;
        layer->verticesByTarget[TARGET_SNES_MAIN] += section->count;
        layer->sectionsTotal++;
    }
}

void gpu3dsInitializeMode7Vertex(int idx, int x, int y)
{
    int x0 = 0;
    int y0 = 0;

    if (x < 64)
    {
        x0 = x * 8;
        y0 = (y * 2 + 1) * 8;
    }
    else
    {
        x0 = (x - 64) * 8;
        y0 = (y * 2) * 8;
    }

    int x1 = x0 + 8;
    int y1 = y0 + 8;

    
    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];

    m7vertices[0].Position = (SVector4i){x0, y0, 0, -1};
}

void gpu3dsInitializeMode7VertexForTile0(int idx, int x, int y)
{
    int x0 = x;
    int y0 = y;

    int x1 = x0 + 8;
    int y1 = y0 + 8;

    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];
    
    m7vertices[0].Position = (SVector4i){x0, y0, 0, 0x3fff};
}

void gpu3dsInitializeMode7Vertexes()
{
    GPU3DSExt.mode7FrameCount = 3;
    
    int idx = 0;
    for (int section = 0; section < 4; section++)
    {
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 128; x++)
                gpu3dsInitializeMode7Vertex(idx++, x, y);
    }

    gpu3dsInitializeMode7VertexForTile0(16384, 0, 0);
    gpu3dsInitializeMode7VertexForTile0(16385, 0, 8);
    gpu3dsInitializeMode7VertexForTile0(16386, 8, 0);
    gpu3dsInitializeMode7VertexForTile0(16387, 8, 8);

    // copy first half to second half for double buffering
    SVertexList *list = &GPU3DS.vertices[VBO_MODE7_TILE];
    memcpy((void *)((u32)list->data_base + list->sizeInBytes / 2), list->data_base, list->sizeInBytes / 2);

	gpu3dsCopyVRAMTilesIntoMode7TileVertexes(Memory.VRAM);
}

// Changes the texture pixel format (but it must be the same 
// size as the original pixel format). No errors will be thrown
// if the format is incorrect.
//

void gpu3dsSetMode7TexturesPixelFormat(GPU_TEXCOLOR fmt)
{
    if (GPU3DSExt.mode7TextureFormat == fmt)
        return;

    GPU3DSExt.mode7TextureFormat = fmt;

    GPU3DS.textures[SNES_MODE7_FULL].tex.fmt = fmt;
    GPU3DS.textures[SNES_MODE7_TILE_0].tex.fmt = fmt;
    GPU3DS.textures[SNES_MODE7_TILE_CACHE].tex.fmt = fmt;
}

void gpu3dsCopyVRAMTilesIntoMode7TileVertexes(uint8 *VRAM)
{
    for (int i = 0; i < 16384; i++)
    {
        gpu3dsSetMode7TileModified(i, VRAM[i * 2]);
    }
    IPPU.Mode7CharDirtyFlagCount = 1;
    for (int i = 0; i < 256; i++)
    {
        IPPU.Mode7CharDirtyFlag[i] = 2;
    }
}

void gpu3dsIncrementMode7UpdateFrameCount()
{
    gpu3dsPrepareListForNextFrame(&GPU3DS.vertices[VBO_MODE7_TILE], true);
    GPU3DSExt.mode7FrameCount ++;

    if (GPU3DSExt.mode7FrameCount == 0x3fff)
    {
        GPU3DSExt.mode7FrameCount = 1;
    }

    // Bug fix: Clears the updateFrameCount of both sets
    // of mode7TileVertexes!
    //
    if (GPU3DSExt.mode7FrameCount <= 2)
    {
        SMode7TileVertex* vertices = (SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data;

        for (int i = 0; i < 16384; )
        {
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;

            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
            vertices[i++].Position.w = -1;
        }
    }
}

void gpu3dsAddQuadRect(u16 x0, u16 y0, u16 x1, u16 y1, u16 wx, u16 wy, int z, u32 fillColor, u32 borderColor, u8 borderSize) 
{
    if (borderSize > 0) {
        u16 cx0 = x0 + borderSize;
        u16 cy0 = y0 + borderSize;
        u16 cx1 = x1 - borderSize;
        u16 cy1 = y1 - borderSize;
        
        // top, bottom left, right
        gpu3dsAddSimpleQuadVertexes(x0, y0, x1, cy0, wx, wy, wx, wy, z, borderColor);
        gpu3dsAddSimpleQuadVertexes(x0, cy1, x1, y1, wx, wy, wx, wy, z, borderColor);
        gpu3dsAddSimpleQuadVertexes(x0, cy0, cx0, cy1, wx, wy, wx, wy, z, borderColor);
        gpu3dsAddSimpleQuadVertexes(cx1, cy0, x1, cy1, wx, wy, wx, wy, z, borderColor);

        gpu3dsAddSimpleQuadVertexes(cx0, cy0, cx1, cy1, wx, wy, wx, wy, z, fillColor);
    } else {
        gpu3dsAddSimpleQuadVertexes(x0, y0, x1, y1, wx, wy, wx, wy, z, fillColor);
    }
}