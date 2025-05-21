
#include "snes9x.h"
#include "ppu.h"

#include <3ds.h>
#include "3dsgpu.h"
#include "3dsimpl.h"
#include "3dsimpl_gpu.h"

SGPU3DSExtended GPU3DSExt;

void gpu3dsDeallocLayerSections() {
    for (int i = 0; i < LAYERS_COUNT; i++) 
    {
        SLayer *layer = &GPU3DSExt.layers[i];

        if (layer->sectionsExpanded == nullptr) 
            continue;

        linearFree(layer->sectionsExpanded);
        layer->sectionsExpanded = NULL;
        layer->sectionsMax = LAYER_SECTIONS_COUNT;
    }
}

void gpu3dsDeallocLayers()
{
    SLayerList *list = &GPU3DSExt.layerList;
    
    if (list == nullptr || list->ibo_base == nullptr)
        return;

    linearFree(list->ibo_base);

    gpu3dsDeallocLayerSections();
}

void gpu3dsResetLayer(SLayer *layer, int activeIndex) {
    layer->sectionsByTarget[TARGET_SNES_MAIN] = 0;
    layer->verticesByTarget[TARGET_SNES_MAIN] = 0;

    layer->sectionsByTarget[TARGET_SNES_SUB] = 0;
    layer->verticesByTarget[TARGET_SNES_SUB] = 0;
    
    layer->sectionsTotal = 0;
    layer->m7Tile0 = false;
}

void gpu3dsResetLayers() {
    SLayerList *list = &GPU3DSExt.layerList;

    if (list->flip)
        list->ibo = (void *)((u32)(list->ibo_base) + list->sizeInBytes);
    else
        list->ibo = list->ibo_base;
    
    list->flip = 1 - list->flip;
    list->verticesTotal = 0;
    list->anythingOnSub = false;
    list->layersTotalByTarget[TARGET_SNES_SUB] = 0;
    list->layersTotalByTarget[TARGET_SNES_MAIN] = 0;

    for (int i = 0; i < LAYERS_COUNT; i++) {
        gpu3dsResetLayer(&GPU3DSExt.layers[i], list->flip);
    }
}

u32 gpu3dsGetPropertyFlags(LAYER_ID id, bool initialState) {
    if (id == LAYER_WINDOW_LR || id == LAYER_BACKDROP)
        return initialState ? FLAG_TARGET : 0;

    if (id == LAYER_OBJ)
    {
        return initialState ? FLAG_TEXTURE_BIND 
            | FLAG_TEXTURE_ENV
            | FLAG_STENCIL_TEST
            | FLAG_ALPHA_TEST : FLAG_STENCIL_TEST;
    }

    if (id == LAYER_BG0 || id == LAYER_BG1)
    {
        return initialState ? FLAG_TEXTURE_BIND
            | FLAG_TEXTURE_ENV
            | FLAG_STENCIL_TEST 
            | FLAG_DEPTH_TEST 
            | FLAG_ALPHA_TEST
            | FLAG_TEXTURE_OFFSET : FLAG_TEXTURE_BIND 
            | FLAG_STENCIL_TEST
            | FLAG_ALPHA_TEST;
    }

    if (id == LAYER_BG2 || id == LAYER_BG3)
    {
        return initialState ? FLAG_TEXTURE_BIND
            | FLAG_TEXTURE_ENV
            | FLAG_STENCIL_TEST 
            | FLAG_DEPTH_TEST 
            | FLAG_ALPHA_TEST : FLAG_STENCIL_TEST;
    }

    // LAYER_COLOR_MATH
    return FLAG_TEXTURE_BIND
        | FLAG_TEXTURE_ENV 
        | FLAG_STENCIL_TEST
        | FLAG_DEPTH_TEST 
        | FLAG_ALPHA_TEST
        | FLAG_ALPHA_BLENDING;
}

void gpu3dsInitLayers() {
    SLayerList *list = &GPU3DSExt.layerList;

    list->verticesTotal = 0;
    list->verticesMax = MAX_VERTICES;
    list->sizeInBytes = gpu3dsGetNextPowerOf2(MAX_VERTICES * sizeof(u16));
    list->ibo_base = linearAlloc(list->sizeInBytes * 2); // allocate double the required size for double buffering
    list->ibo = list->ibo_base;
    list->anythingOnSub = false;
    list->flip = 1;

    for (int i = 0; i < LAYERS_COUNT; i++) 
    {
        SLayer *layer = &GPU3DSExt.layers[i];
        layer->id = (LAYER_ID)i;
        layer->sectionsMax = LAYER_SECTIONS_COUNT;
        layer->propertyFlags[0] = gpu3dsGetPropertyFlags(layer->id, true);
        layer->propertyFlags[1] = gpu3dsGetPropertyFlags(layer->id, false);

        gpu3dsResetLayer(layer, list->flip);
    }
}

SLayerSection *getSection(SLayer *layer, int idx) {
    if (layer->sectionsExpanded == NULL) {
        return &layer->sections[idx];
    }

    return  &layer->sectionsExpanded[idx];
}

int compareSections(const void *a, const void *b) {
    const SLayerSection *sa = (const SLayerSection *)a;
    const SLayerSection *sb = (const SLayerSection *)b;

    // sub sections are drawn first
    if (sa->onSub != sb->onSub) {
        return sb->onSub - sa->onSub;
    }
    
    // mode7 tile0 sections are drawn first
    return sb->m7Tile0 - sa->m7Tile0;
}

// Insertion sort implementation
void sortSections(SLayerSection *arr, int n) {
    // Use insertion sort for small arrays (most likely)
    if (n < 50) {
        for (int i = 1; i < n; i++) {
            SLayerSection key = arr[i];
            int j = i - 1;
            while (j >= 0 && (
                (arr[j].onSub < key.onSub) || // sub sections are drawn first
                (arr[j].onSub == key.onSub && arr[j].m7Tile0 < key.m7Tile0) // mode7 tile0 sections are drawn first
            )) {
                arr[j + 1] = arr[j];
                j--;
            }
            arr[j + 1] = key;
        }
    } else {
        qsort(arr, n, sizeof(SLayerSection), compareSections);
    }
}

void gpu3dsDrawLayer(SLayer *layer, u16 *indices, int from, int to) {
    u16 *sectionIndices = indices;
    u16 batchFrom = 0;
    u16 batchCount = 0;

    // starting the first batch of sections
    gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, layer->propertyFlags[0], &getSection(layer, from)->state);
    bool drawLater = true;

    for (int idx = from; idx < to; idx++) {
        SLayerSection *section = getSection(layer, idx);
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

        if (idx != from) {
            drawLater = (layer->propertyFlags[1] == 0 || !gpu3dsRenderStateHasChangedInLayer(&GPU3DS.currentRenderState, layer->propertyFlags[1], &section->state)); 

            if (!drawLater) {
                // draw the current batch of sections
                if (GPU3DS.currentRenderStateFlags)
                    gpu3dsApplyRenderState(&GPU3DS.currentRenderState);

                GPU_DrawElements(GPU_GEOMETRY_PRIM, batchCount, C3D_UNSIGNED_SHORT, (void *)(indices + batchFrom));
                
                // starting a new batch of sections
                gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, layer->propertyFlags[1], &section->state);        
                batchFrom += batchCount;
                batchCount = 0;
            }
        }

        batchCount += sCount;
                
        if (idx < to - 1) 
            sectionIndices += sCount;
        else {
            // draw the last batch of sections
            if (GPU3DS.currentRenderStateFlags)
                gpu3dsApplyRenderState(&GPU3DS.currentRenderState);

            GPU_DrawElements(GPU_GEOMETRY_PRIM, batchCount, C3D_UNSIGNED_SHORT, (void *)(indices + batchFrom));
        }
    }
}

void gpu3dsDrawLayers(SLayerList *layerList) {
	// set render state to default
	SGPURenderState renderState = GPU3DS.currentRenderState;
	renderState.textureEnv = TEX_ENV_REPLACE_COLOR;
	renderState.stencilTest = STENCIL_TEST_DISABLED;
	renderState.depthTest = DEPTH_TEST_DISABLED;
	renderState.alphaTest = ALPHA_TEST_DISABLED;
	renderState.alphaBlending = ALPHA_BLENDING_DISABLED;

	u32 defaultFlags = FLAG_TEXTURE_ENV 
	| FLAG_STENCIL_TEST
	| FLAG_DEPTH_TEST 
	| FLAG_ALPHA_TEST
	| FLAG_ALPHA_BLENDING;

    gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, defaultFlags, &renderState);

    u8 i0 = layerList->anythingOnSub ? 1 : 0;

    for (int i = i0; i >= 0; i--) {
        bool sub = i == TARGET_SNES_SUB;

        // sub: LAYER_BACKDROP - LAYER_BG3, main: LAYER_WINDOW_LR - LAYER_COLOR_MATH
        for (int j = 0; j < layerList->layersTotalByTarget[i]; j++) {
            int idx = layerList->layersByTarget[i][j];
            SLayer *layer = &GPU3DSExt.layers[idx];

            u32 bufferOffset = layer->bufferOffset + (sub ? 0 : layer->verticesByTarget[TARGET_SNES_SUB]);
            u16 *indices = (u16 *)layerList->ibo + bufferOffset;

            int from = sub ? 0 : layer->sectionsByTarget[TARGET_SNES_SUB];
            int to = from + layer->sectionsByTarget[i];
            gpu3dsDrawLayer(layer, indices, from, to);
        }

        if (sub) {
            // restore default flags
            gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, defaultFlags, &renderState);
        }         
    }
}

void gpu3dsPrepareLayers() {
    SLayerList *layerList = &GPU3DSExt.layerList;

    if (!layerList->verticesTotal)
        return;

    SVertexList *list = &GPU3DS.vertices[VBO_SCENE];
    gpu3dsSetAttributeBuffers(&list->attrInfo, list->data);

    LAYER_ID drawOrder[LAYERS_COUNT] = {
        LAYER_WINDOW_LR,
        LAYER_BACKDROP,
        LAYER_OBJ,
        LAYER_BG0,
        LAYER_BG1,
        LAYER_BG2,
        LAYER_BG3,
        LAYER_COLOR_MATH,
    };

    u32 bufferOffset = 0;
    
    for (int i = 0; i < LAYERS_COUNT; i++) {
        SLayer *layer = &GPU3DSExt.layers[drawOrder[i]];
        u16 verticesOnSub = layer->verticesByTarget[TARGET_SNES_SUB];
        u16 verticesOnMain = layer->verticesByTarget[TARGET_SNES_MAIN];

        layer->verticesTotal = verticesOnSub + verticesOnMain;

        if (!layer->verticesTotal) {
            continue;
        }

        if (verticesOnMain) {
            layerList->layersByTarget[TARGET_SNES_MAIN][layerList->layersTotalByTarget[TARGET_SNES_MAIN]++] = layer->id;
        }

        if (verticesOnSub) {
            layerList->layersByTarget[TARGET_SNES_SUB][layerList->layersTotalByTarget[TARGET_SNES_SUB]++] = layer->id;
            layerList->anythingOnSub = true;
        }

        if (verticesOnSub || layer->m7Tile0) {
            sortSections(layer->sectionsExpanded == NULL ? layer->sections : layer->sectionsExpanded, layer->sectionsTotal);
        }
        
        layer->bufferOffset = bufferOffset;
        bufferOffset += layer->verticesTotal;
    }

    gpu3dsDrawLayers(layerList);
}

bool expandMaxSections(SLayer *layer) {
    u16 newMax = layer->sectionsMax * 2;

    if (newMax > 512) return false;

    SLayerSection *sectionsExpanded = (SLayerSection *)linearAlloc(newMax * sizeof(SLayerSection));

    if (!sectionsExpanded) {
        return false;
    }

    if (layer->sectionsExpanded == NULL) {
        // copy static sections
        memcpy(sectionsExpanded, layer->sections, layer->sectionsMax * sizeof(SLayerSection));
    } else {
        memcpy(sectionsExpanded, layer->sectionsExpanded, layer->sectionsMax * sizeof(SLayerSection));
        linearFree(layer->sectionsExpanded);
    }

    layer->sectionsExpanded = sectionsExpanded;
    layer->sectionsMax = newMax;
    
    return true;
}

void gpu3dsCommitLayerSection(LAYER_ID id, SGPURenderState *state, bool reuseVertices) {
    SLayerList *layerList = &GPU3DSExt.layerList;
    SLayer *layer = &GPU3DSExt.layers[id];

    int sectionIdx = layer->sectionsTotal;
    bool allocError;

    // handle max sections overflow
    if (sectionIdx >= layer->sectionsMax) {
        allocError = !expandMaxSections(layer);
    }

    if (!reuseVertices) 
    {
        SVertexList *list = &GPU3DS.vertices[VBO_SCENE];
        u16 currentIdx = list->FromIndex;

        // handle max vertices overflow
        u16 currentVerticesCount = gpu3dsGetValueWithinLimit(list->Count, layerList->verticesTotal, MAX_VERTICES);

        list->FromIndex += list->Count;
        list->Count = 0;

        if (allocError || !currentVerticesCount) return;

        SLayerSection *section = getSection(layer, sectionIdx);

        section->state = *state;
        bool sub = state->target == TARGET_SNES_SUB;
        
        section->from = currentIdx;
        section->count = currentVerticesCount;
        section->startY = GFX.StartY;
        section->endY = GFX.EndY;
        section->m7Tile0 = state->textureBind == SNES_MODE7_TILE_0;

        if (section->m7Tile0) {
            layer->m7Tile0 = true;
        }

        section->onSub = sub;

        layer->verticesByTarget[sub] += currentVerticesCount;
        layer->sectionsByTarget[sub]++;
        layer->sectionsTotal++;
        
        layerList->verticesTotal += currentVerticesCount;
        
        return;
    }

    int prevSectionIndex = sectionIdx - 1;

    if (prevSectionIndex >= 0 && !allocError)
    {
        SLayerSection *section = getSection(layer, sectionIdx);
        *section = *getSection(layer, prevSectionIndex); // reuse last section properties
        
        u16 currentVerticesCount = gpu3dsGetValueWithinLimit(section->count, layerList->verticesTotal, MAX_VERTICES);

        if (!currentVerticesCount) return;

        section->onSub = false; // reused vertices are always on main
        section->state = *state;
        
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
    m7vertices[0].TexCoord = (STexCoord2i){0, 0};
}

void gpu3dsInitializeMode7VertexForTile0(int idx, int x, int y)
{
    int x0 = x;
    int y0 = y;

    int x1 = x0 + 8;
    int y1 = y0 + 8;

    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];
    
    m7vertices[0].Position = (SVector4i){x0, y0, 0, 0x3fff};
    m7vertices[0].TexCoord = (STexCoord2i){0, 0};
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

    SGPURenderState renderState = GPU3DS.currentRenderState;
    renderState.updateFrame = GPU3DSExt.mode7FrameCount; 
    gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_UPDATE_FRAME, &renderState);
}