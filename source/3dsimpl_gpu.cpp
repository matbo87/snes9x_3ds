
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

    linearFree(list->sections);
    linearFree(list->ibo);
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
            ? PACKED_MASK_TEX_BIND | PACKED_MASK_STENCIL | PACKED_MASK_ALPHA_TEST | PACKED_MASK_TEX_OFFSET
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
            ? PACKED_MASK_TEX_BIND | PACKED_MASK_STENCIL | PACKED_MASK_ALPHA_TEST | PACKED_MASK_TEX_OFFSET
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
    // draw window_lr into depth buffer first
    SLayer *layer = &list->layers[LAYER_WINDOW_LR];

    if (layer->verticesByTarget[0]) {
        GPU3DS.currentRenderState.target = TARGET_SNES_DEPTH;

        gpu3dsDrawLayer(layer, layer->sectionsOffset, layer->sectionsOffset + layer->sectionsByTarget[TARGET_SNES_MAIN]);
    }

    u8 i0 = list->anythingOnSub ? 1 : 0;

    for (int i = i0; i >= 0; i--) {
        GPU3DS.currentRenderState.target = (SGPU_TARGET_ID)i;

        bool sub = i == TARGET_SNES_SUB;

        for (int j = 0; j < list->layersTotalByTarget[i]; j++) {
            LAYER_ID id = list->layersByTarget[i][j];
            SLayer *layer = &list->layers[id];

            GPU3DS.currentRenderState.depthTest = id < LAYER_OBJ ? SGPU_STATE_ENABLED : SGPU_STATE_DISABLED;

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

			// invalidate so next gpu3dsApplyRenderState re-applies the target (framebuf address changes per section)
			GPU3DS.appliedRenderState.target = TARGET_UNSET;
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
        SLayerSection *section = &list->sections[sectionIdx]; // reuse last section properties

        *section = list->sections[prevSectionIndex];
        
        if (!section->count) return;

        section->state = *state;
        section->onSub = false; // reuse only happens on main
        
        layer->sectionsByTarget[TARGET_SNES_MAIN]++;
        layer->verticesByTarget[TARGET_SNES_MAIN] += section->count;
        layer->sectionsTotal++;
    }
}

void gpu3dsInitializeMode7Vertex(int idx, s16 x, s16 y)
{
    s16 x0 = 0;
    s16 y0 = 0;

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

    SMode7TileVertex *m7vertices = &((SMode7TileVertex *)GPU3DS.vertices[VBO_MODE7_TILE].data) [idx];

    m7vertices[0].Position = (SVector4i){x0, y0, 0, -1};
}

void gpu3dsInitializeMode7VertexForTile0(int idx, s16 x, s16 y)
{
    s16 x0 = x;
    s16 y0 = y;

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

    GPU_COLORBUF colorFmt = (GPU_COLORBUF)gpu3dsGetFrameBufferFmt(fmt);
    GPU3DS.textures[SNES_MODE7_FULL].target->frameBuf.colorFmt = colorFmt;
    GPU3DS.textures[SNES_MODE7_TILE_0].target->frameBuf.colorFmt = colorFmt;
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

void gpu3dsAddQuadRect(float x0, float y0, float x1, float y1, u16 wx, u16 wy, int z, u32 fillColor, u32 borderColor, u8 borderSize) 
{
    if (borderSize > 0) {
        float cx0 = x0 + borderSize;
        float cy0 = y0 + borderSize;
        float cx1 = x1 - borderSize;
        float cy1 = y1 - borderSize;
        
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
