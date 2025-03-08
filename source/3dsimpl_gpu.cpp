
#include "snes9x.h"
#include "ppu.h"

#include <3ds.h>
#include "3dsgpu.h"
#include "3dsimpl.h"
#include "3dsimpl_gpu.h"

SGPU3DSExtended GPU3DSExt;

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
        gpu3dsSetMode7TileTexturePos(i, VRAM[i * 2]);
        gpu3dsSetMode7TileModifiedFlag(i);
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
        for (int i = 0; i < 16384; )
        {
            gpu3dsSetMode7TileModifiedFlag(i++, -1);
            gpu3dsSetMode7TileModifiedFlag(i++, -1);
            gpu3dsSetMode7TileModifiedFlag(i++, -1);
            gpu3dsSetMode7TileModifiedFlag(i++, -1);

            gpu3dsSetMode7TileModifiedFlag(i++, -1);
            gpu3dsSetMode7TileModifiedFlag(i++, -1);
            gpu3dsSetMode7TileModifiedFlag(i++, -1);
            gpu3dsSetMode7TileModifiedFlag(i++, -1);
        }
    }

    gpu3dsUpdateRenderState(&GPU3DS.currentRenderState, FLAG_UPDATE_FRAME, (u32)GPU3DSExt.mode7FrameCount, (u32)GPU3DS.currentRenderState.updateFrame);
}