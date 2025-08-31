
#include "snes9x.h"
#include "ppu.h"

#include <3ds.h>
#include "3dsgpu.h"
#include "3dsimpl.h"
#include "3dsimpl_gpu.h"
#include "3dslog.h"

SGPU3DSExtended GPU3DSExt;

void gpu3dsDeallocLayers()
{   
    SLayerList *list = &GPU3DSExt.layerList;

    if (list == nullptr)
        return;

    if (list->sections != nullptr)
        linearFree(list->sections);

    if (list->ibo_base != nullptr)
        linearFree(list->ibo_base);
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

void gpu3dsPrepareLayersForNextFrame() {
    SLayerList *list = &GPU3DSExt.layerList;
    
    if (!list->hasSkippedSections) {
        list->flip = 1 - list->flip;

        if (list->flip)
            list->ibo = (void *)((u32)(list->ibo_base) + list->sizeInBytes);
        else
            list->ibo = list->ibo_base;
    }
    else {
        gpu3dsAdjustLayerSectionLimits(list);
        list->hasSkippedSections = false;
        gpu3dsResetLayers(list);
    }
}

u32 gpu3dsGetPropertyFlags(LAYER_ID id, bool firstSection) {
    if (id == LAYER_OBJ)
    {
        return firstSection ? FLAG_TEXTURE_BIND
            | FLAG_STENCIL_TEST
            | FLAG_ALPHA_TEST : FLAG_STENCIL_TEST;
    }

    if (id == LAYER_BG0 || id == LAYER_BG1)
    {
        return FLAG_TEXTURE_BIND
            | FLAG_STENCIL_TEST
            | FLAG_ALPHA_TEST
            | FLAG_TEXTURE_OFFSET;
    }

    if (id == LAYER_BG2 || id == LAYER_BG3)
    {
        return firstSection ? FLAG_TEXTURE_BIND
            | FLAG_STENCIL_TEST
            | FLAG_ALPHA_TEST : FLAG_STENCIL_TEST;
    }

    return 0;
}

void gpu3dsInitLayers() {
    SLayerList *list = &GPU3DSExt.layerList;

    list->sizeInBytes = gpu3dsGetNextPowerOf2(MAX_VERTICES * sizeof(u16));
    list->ibo_base = linearAlloc(list->sizeInBytes * 2); // allocate double the required size for double buffering
    list->ibo = list->ibo_base;
    list->flip = 1;

    gpu3dsResetLayers(list);

    for (int i = 0; i < LAYERS_COUNT; i++) 
    {
        LAYER_ID id = LAYER_ID(i);
        SLayer *layer = &list->layers[id];

        layer->id = id;
    }

    gpu3dsResetLayerSectionLimits(list);
    list->sectionsSizeInBytes = gpu3dsGetNextPowerOf2(list->sectionsMax * sizeof(SLayerSection));
    list->sections = (SLayerSection *)linearAlloc(list->sectionsSizeInBytes);
			
    log3dsWrite("ibo size:%dkb, sections size: %dkb",
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

    u32 flags = FLAG_TEXTURE_ENV
    | FLAG_STENCIL_TEST
    | FLAG_ALPHA_TEST
    | FLAG_ALPHA_BLENDING;

    if (layer->id == LAYER_COLOR_MATH)
        flags |= FLAG_TEXTURE_BIND;

    for (int i = from; i < to; i++) {
        SLayerSection *section = &list->sections[i];

        gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, flags, &section->state);
        gpu3dsDraw(&GPU3DS.vertices[section->vboId], NULL, section->count, section->from);
    }
}

// obj, bg0-bg3
void gpu3dsDrawLayerByIndices(SLayer *layer, u16 *indices, int from, int to) {
    SLayerList *list = &GPU3DSExt.layerList;
    u16 *sectionIndices = indices;
    u16 batchFrom = 0;
    u16 batchCount = 0;
    bool drawLater;
    
	SGPURenderState renderState = GPU3DS.currentRenderState;
    u32 layerFlags = FLAG_TEXTURE_ENV | FLAG_ALPHA_BLENDING;

    renderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA;
    renderState.alphaBlending = ALPHA_BLENDING_DISABLED;
    gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, layerFlags, &renderState);

    SGPU_VBO_ID vboId;
    
    u32 sectionFlags[2]; 
    sectionFlags[0] = gpu3dsGetPropertyFlags(layer->id, true); // first section
    sectionFlags[1] = gpu3dsGetPropertyFlags(layer->id, false); // upcoming section(s)

    for (int idx = from; idx < to; idx++) {
        SLayerSection *section = &list->sections[idx];
        u16 sFrom = section->from;
        u16 sCount = section->count;
        
        int i = 0;
        for (; i <= sCount - 4; i += 4) {
            sectionIndices[i] = sFrom + i;
            sectionIndices[i + 1] = sFrom + i + 1;
            sectionIndices[i + 2] = sFrom + i + 2;
            sectionIndices[i + 3] = sFrom + i + 3;
        }

        for (; i < sCount; i++) {
            sectionIndices[i] = sFrom + i;
        }

        // starting the first batch of sections
        if (idx == from)
        {
            vboId = section->vboId;
            gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, sectionFlags[0], &section->state);  
        }
        else
        {
            drawLater = !gpu3dsRenderStateHasChangedInLayer(&GPU3DS.currentRenderState, sectionFlags[1], &section->state); 
            
            if (!drawLater) {
                // draw the current batch of sections


                gpu3dsDraw(&GPU3DS.vertices[vboId], (void *)(indices + batchFrom), batchCount);
                vboId = section->vboId;
                
                // starting a new batch of sections
                gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, sectionFlags[1], &section->state);        
                batchFrom += batchCount;
                batchCount = 0;
            }
        }

        batchCount += sCount;
                
        if (idx < to - 1) 
            sectionIndices += sCount;
        else {
            // draw the last batch of sections
            gpu3dsDraw(&GPU3DS.vertices[vboId], (void *)(indices + batchFrom), batchCount);
        }
    }
}

void gpu3dsDrawLayers(SLayerList *list) {
    // set render state to default
    
    // draw window_lr into depth buffer first
    SLayer *layer = &list->layers[LAYER_WINDOW_LR];

    if (layer->verticesByTarget[0]) {
        GPU3DS.currentRenderTarget = TARGET_SNES_DEPTH;
        GPU3DS.currentRenderStateFlags |= FLAG_TARGET;
        
        gpu3dsDrawLayer(layer, layer->sectionsOffset, layer->sectionsOffset + layer->sectionsByTarget[TARGET_SNES_MAIN]);
    }

    u8 i0 = list->anythingOnSub ? 1 : 0;

    for (int i = i0; i >= 0; i--) {
        GPU3DS.currentRenderTarget = (SGPU_TARGET_ID)i;
        GPU3DS.currentRenderStateFlags |= FLAG_TARGET;

        bool sub = i == TARGET_SNES_SUB;

        for (int j = 0; j < list->layersTotalByTarget[i]; j++) {
            LAYER_ID id = list->layersByTarget[i][j];
            SLayer *layer = &list->layers[id];

            bool depthTestEnabled = id < LAYER_OBJ;
            if (GPU3DS.depthTestEnabled != depthTestEnabled) {
                GPU3DS.depthTestEnabled = depthTestEnabled;
                GPU3DS.currentRenderStateFlags |= FLAG_DEPTH_TEST;
            }

            int from = layer->sectionsOffset + (sub ? 0 : layer->sectionsByTarget[TARGET_SNES_SUB]);
            int to = from + layer->sectionsByTarget[i];

            if (id < LAYER_BACKDROP) {
                u32 bufferOffset = layer->bufferOffset + (sub ? 0 : layer->verticesByTarget[TARGET_SNES_SUB]);
                u16 *indices = (u16 *)list->ibo + bufferOffset;
    
                gpu3dsDrawLayerByIndices(layer, indices, from, to);
            }
            else {
                gpu3dsDrawLayer(layer, from, to);
            }
        }
    }
}

void gpu3dsPrepareAndDrawLayers() {
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

    gpu3dsDrawLayers(list);
    gpu3dsResetLayers(list);
}

void gpu3dsCommitLayerSection(SGPU_VBO_ID vboId, LAYER_ID id, SGPURenderState *state, bool sub, bool reuseVertices) {
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
        if (list->hasSkippedSections || !currentVerticesCount) return;

        SLayerSection *section = &list->sections[sectionIdx];

        section->state = *state;
        section->from = currentIdx;
        section->count = currentVerticesCount;
        section->vboId = vboId;
        section->onSub = sub;

        if (state->textureBind == SNES_MODE7_TILE_0) {
            layer->m7Tile0 = true;
        }
        
        layer->verticesByTarget[sub] += currentVerticesCount;
        layer->sectionsByTarget[sub]++;
        layer->sectionsTotal++;
        
        list->verticesTotal += currentVerticesCount;
    
        return;
    }

    int prevSectionIndex = sectionIdx - 1;

    if (prevSectionIndex >= 0 && !list->hasSkippedSections)
    {
        SLayerSection *section = &list->sections[sectionIdx];
        *section = *(&list->sections[prevSectionIndex]); // reuse last section properties
        
        u16 currentVerticesCount = gpu3dsGetValueWithinLimit(section->count, list->verticesTotal, MAX_VERTICES);

        if (!currentVerticesCount) return;

        section->state = *state;
        section->onSub = false;
        
        layer->sectionsByTarget[TARGET_SNES_MAIN]++;
        layer->verticesByTarget[TARGET_SNES_MAIN] += currentVerticesCount;
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
    
    for (int f = 0; f < 2; f++)
    {
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

        gpu3dsSwapVertexListForNextFrame(&GPU3DS.vertices[VBO_MODE7_TILE]);
    }
    
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
    gpu3dsSwapVertexListForNextFrame(&GPU3DS.vertices[VBO_MODE7_TILE]);
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