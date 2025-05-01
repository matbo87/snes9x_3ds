#include "copyright.h"


#include "snes9x.h"

#include "memmap.h"
#include "ppu.h"
#include "cpuexec.h"
#include "gfx.h"
#include "apu.h"
#include "cheats.h"
#include "cliphw.h"

#include <3ds.h>
#include "3dsopt.h"
#include "3dsgpu.h"
#include "3dsimpl_tilecache.h"
#include "3dsimpl_gpu.h"

#define M7 19

extern uint8 Depths[8][4];
extern uint8 BGSizes [2];
extern struct SBG BG;

extern struct SLineData LineData[240];
extern struct SLineMatrixData LineMatrixData [240];

#define ON_MAIN(N) \
(GFX.r212c & (1 << (N)) && \
 !(PPU.BG_Forced & (1 << (N))))

#define SUB_OR_ADD(N) \
(GFX.r2131 & (1 << (N)))

#define ON_SUB(N) \
((GFX.r2130 & 0x30) != 0x30 && \
 (GFX.r2130 & 2) && \
 (GFX.r212d & (1 << N)) && \
 !(PPU.BG_Forced & (1 << (N))))

#define ON_SUB_PSEUDO(N) \
 ((GFX.r212d & (1 << N)))

#define ON_SUB_HIRES(N) \
((GFX.r212d & (1 << N)) && \
 !(PPU.BG_Forced & (1 << (N))))

#define ANYTHING_ON_SUB \
((GFX.r2130 & 0x30) != 0x30 && \
 (GFX.r2130 & 2) && \
 (GFX.r212d & 0x1f))

#define ADD_OR_SUB_ON_ANYTHING \
(GFX.r2131 & 0x3f)

#define ALPHA_DEFAULT		 		0x0000
#define ALPHA_ZERO 					0x6000 
#define ALPHA_0_5 					0x2000 
#define ALPHA_1_0 					0x4000 

#define M7_LINE_ROUNDING_OFFSET		128 // Half of 256 for rounding

int16 layerVerticesCount[5];

SGPURenderState renderState;

//-------------------------------------------------------------------
// Render the backdrop
//-------------------------------------------------------------------
void S9xDrawBackdropHardware(bool sub, int depth)
{
	int sectionsCount = sub ? IPPU.FixedColorSections.Count : IPPU.BackdropColorSections.Count;

	if (!sectionsCount)
		return;

	t3dsStartTiming(25, "DrawBKClr");
	
	int backColor;
	
	if (!sub)
	{
		// Performance:
		// Use backdrop color sections for drawing backdrops.
		//

		for (int i = 0; i < sectionsCount; i++)
		{
			if ((GFX.r2130 & 0xc0) == 0xc0)
				backColor = 0xff;
			else
			{
				backColor = IPPU.BackdropColorSections.Section[i].Value;

				backColor =
					((backColor & (0x1F << 11)) << 16) |
					((backColor & (0x1F << 6)) << 13)|
					((backColor & (0x1F << 1)) << 10) | 0xFF;
			}

			gpu3dsAddRectangleVertexes(
				0, IPPU.BackdropColorSections.Section[i].StartY + depth, 
				256, IPPU.BackdropColorSections.Section[i].EndY + 1 + depth, backColor);
		}
	}
	else
	{
		// Small performance improvement:
		// Use vertical sections to render the subscreen backdrop
		//
		for (int i = 0; i < sectionsCount; i++)
		{
			backColor = IPPU.FixedColorSections.Section[i].Value;

			// Bug fix: Ensures that the subscreen is cleared with a
			// transparent color. Otherwise, if the transparency (div 2)
			// is activated it can cause an ugly dark tint.
			// This fixes Chrono Trigger's Leene' Square dark floor and
			// Secret of Mana's dark grass.
			//
			if (backColor == 0xff) 
			{
				backColor = 0;
				depth = depth & 0xfff;		// removes the alpha component
			}

			gpu3dsAddRectangleVertexes(
				0, IPPU.FixedColorSections.Section[i].StartY + depth, 
				256, IPPU.FixedColorSections.Section[i].EndY + 1 + depth, backColor);
		}
	}

	renderState.textureEnv = TEX_ENV_REPLACE_COLOR;
	renderState.stencilTest = STENCIL_TEST_DISABLED;
	renderState.depthTest = DEPTH_TEST_DISABLED;
	renderState.alphaTest = ALPHA_TEST_DISABLED;

	u32 propertyFlags = FLAG_TEXTURE_ENV 
	| FLAG_STENCIL_TEST
	| FLAG_DEPTH_TEST 
	| FLAG_ALPHA_TEST;

	gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, propertyFlags, &renderState);
	gpu3dsDrawVertexList(&GPU3DS.vertices[VBO_SCENE]);
	t3dsEndTiming(25);
}


//-------------------------------------------------------------------
// Convert tile to 8-bit.
//-------------------------------------------------------------------
uint8 S9xConvertTileTo8Bit (uint8 *pCache, uint32 TileAddr)
{
    //printf ("Tile Addr: %04x\n", TileAddr);
    uint8 *tp = &Memory.VRAM[TileAddr];
    uint32 *p = (uint32 *) pCache;
    uint32 non_zero = 0;
    uint8 line;

    switch (BG.BitShift)
    {
    case 8:
		for (line = 8; line != 0; line--, tp += 2)
		{
			uint32 p1 = 0;
			uint32 p2 = 0;
			uint8 pix;

			if ((pix = *(tp + 0)))
			{
				p1 |= odd_high[0][pix >> 4];
				p2 |= odd_low[0][pix & 0xf];
			}
			if ((pix = *(tp + 1)))
			{
				p1 |= even_high[0][pix >> 4];
				p2 |= even_low[0][pix & 0xf];
			}
			if ((pix = *(tp + 16)))
			{
				p1 |= odd_high[1][pix >> 4];
				p2 |= odd_low[1][pix & 0xf];
			}
			if ((pix = *(tp + 17)))
			{
				p1 |= even_high[1][pix >> 4];
				p2 |= even_low[1][pix & 0xf];
			}
			if ((pix = *(tp + 32)))
			{
				p1 |= odd_high[2][pix >> 4];
				p2 |= odd_low[2][pix & 0xf];
			}
			if ((pix = *(tp + 33)))
			{
				p1 |= even_high[2][pix >> 4];
				p2 |= even_low[2][pix & 0xf];
			}
			if ((pix = *(tp + 48)))
			{
				p1 |= odd_high[3][pix >> 4];
				p2 |= odd_low[3][pix & 0xf];
			}
			if ((pix = *(tp + 49)))
			{
				p1 |= even_high[3][pix >> 4];
				p2 |= even_low[3][pix & 0xf];
			}
			*p++ = p1;
			*p++ = p2;
			non_zero |= p1 | p2;
		}
		break;

    case 4:
		for (line = 8; line != 0; line--, tp += 2)
		{
			uint32 p1 = 0;
			uint32 p2 = 0;
			uint8 pix;
			if ((pix = *(tp + 0)))
			{
				p1 |= odd_high[0][pix >> 4];
				p2 |= odd_low[0][pix & 0xf];
			}
			if ((pix = *(tp + 1)))
			{
				p1 |= even_high[0][pix >> 4];
				p2 |= even_low[0][pix & 0xf];
			}
			if ((pix = *(tp + 16)))
			{
				p1 |= odd_high[1][pix >> 4];
				p2 |= odd_low[1][pix & 0xf];
			}
			if ((pix = *(tp + 17)))
			{
				p1 |= even_high[1][pix >> 4];
				p2 |= even_low[1][pix & 0xf];
			}
			*p++ = p1;
			*p++ = p2;
			non_zero |= p1 | p2;
		}
		break;

    case 2:
		for (line = 8; line != 0; line--, tp += 2)
		{
			uint32 p1 = 0;
			uint32 p2 = 0;
			uint8 pix;
			if ((pix = *(tp + 0)))
			{
				p1 |= odd_high[0][pix >> 4];
				p2 |= odd_low[0][pix & 0xf];
			}
			if ((pix = *(tp + 1)))
			{
				p1 |= even_high[0][pix >> 4];
				p2 |= even_low[0][pix & 0xf];
			}
			*p++ = p1;
			*p++ = p2;
			non_zero |= p1 | p2;
		}
		break;
    }
    return (non_zero ? TRUE : BLANK_TILE);
}



uint8 stencilFunc[128][4]
{
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:0 | Logic = OR   (idx: 0)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:0 | Logic = AND  (idx: 1)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:0 | Logic = XOR  (idx: 2)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:0 | Logic = XNOR (idx: 3)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:0 | Logic = OR   (idx: 4)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:0 | Logic = AND  (idx: 5)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:0 | Logic = XOR  (idx: 6)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:0 | Logic = XNOR (idx: 7)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:0 | Logic = OR   (idx: 8)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:0 | Logic = AND  (idx: 9)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:0 | Logic = XOR  (idx: 10)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:0 | Logic = XNOR (idx: 11)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:0 | Logic = OR   (idx: 12)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:0 | Logic = AND  (idx: 13)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:0 | Logic = XOR  (idx: 14)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:0 | Logic = XNOR (idx: 15)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:0 | Logic = OR   (idx: 16)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:0 | Logic = AND  (idx: 17)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:0 | Logic = XOR  (idx: 18)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:0 | Logic = XNOR (idx: 19)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:0 | Logic = OR   (idx: 20)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:0 | Logic = AND  (idx: 21)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:0 | Logic = XOR  (idx: 22)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:0 | Logic = XNOR (idx: 23)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:0 | Logic = OR   (idx: 24)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:0 | Logic = AND  (idx: 25)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:0 | Logic = XOR  (idx: 26)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:0 | Logic = XNOR (idx: 27)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:0 | Logic = OR   (idx: 28)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:0 | Logic = AND  (idx: 29)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:0 | Logic = XOR  (idx: 30)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:0 | Logic = XNOR (idx: 31)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:1 | Logic = OR   (idx: 32)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:1 | Logic = AND  (idx: 33)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:1 | Logic = XOR  (idx: 34)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:1 | Logic = XNOR (idx: 35)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:1 | Logic = OR   (idx: 36)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:1 | Logic = AND  (idx: 37)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:1 | Logic = XOR  (idx: 38)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:1 | Logic = XNOR (idx: 39)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:1 | Logic = OR   (idx: 40)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:1 | Logic = AND  (idx: 41)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:1 | Logic = XOR  (idx: 42)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:1 | Logic = XNOR (idx: 43)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:1 | Logic = OR   (idx: 44)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:1 | Logic = AND  (idx: 45)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:1 | Logic = XOR  (idx: 46)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:1 | Logic = XNOR (idx: 47)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:1 | Logic = OR   (idx: 48)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:1 | Logic = AND  (idx: 49)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:1 | Logic = XOR  (idx: 50)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:1 | Logic = XNOR (idx: 51)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:1 | Logic = OR   (idx: 52)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:1 | Logic = AND  (idx: 53)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:1 | Logic = XOR  (idx: 54)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:1 | Logic = XNOR (idx: 55)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:1 | Logic = OR   (idx: 56)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:1 | Logic = AND  (idx: 57)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:1 | Logic = XOR  (idx: 58)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:1 | Logic = XNOR (idx: 59)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:1 | Logic = OR   (idx: 60)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:1 | Logic = AND  (idx: 61)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:1 | Logic = XOR  (idx: 62)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:0 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:1 | Logic = XNOR (idx: 63)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:0 | Logic = OR   (idx: 64)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:0 | Logic = AND  (idx: 65)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:0 | Logic = XOR  (idx: 66)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:0 | Logic = XNOR (idx: 67)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:0 | Logic = OR   (idx: 68)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:0 | Logic = AND  (idx: 69)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:0 | Logic = XOR  (idx: 70)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:0 | Logic = XNOR (idx: 71)
    { GPU_EQUAL,    0x00, 0x01 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:0 | Logic = OR   (idx: 72)
    { GPU_EQUAL,    0x00, 0x01 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:0 | Logic = AND  (idx: 73)
    { GPU_EQUAL,    0x00, 0x01 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:0 | Logic = XOR  (idx: 74)
    { GPU_EQUAL,    0x00, 0x01 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:0 | Logic = XNOR (idx: 75)
    { GPU_EQUAL,    0x01, 0x01 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:0 | Logic = OR   (idx: 76)
    { GPU_EQUAL,    0x01, 0x01 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:0 | Logic = AND  (idx: 77)
    { GPU_EQUAL,    0x01, 0x01 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:0 | Logic = XOR  (idx: 78)
    { GPU_EQUAL,    0x01, 0x01 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:0 | Logic = XNOR (idx: 79)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:0 | Logic = OR   (idx: 80)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:0 | Logic = AND  (idx: 81)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:0 | Logic = XOR  (idx: 82)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:0 | Logic = XNOR (idx: 83)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:0 | Logic = OR   (idx: 84)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:0 | Logic = AND  (idx: 85)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:0 | Logic = XOR  (idx: 86)
    { GPU_ALWAYS,   0x00, 0x00 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:0 | Logic = XNOR (idx: 87)
    { GPU_EQUAL,    0x00, 0x01 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:0 | Logic = OR   (idx: 88)
    { GPU_EQUAL,    0x00, 0x01 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:0 | Logic = AND  (idx: 89)
    { GPU_EQUAL,    0x00, 0x01 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:0 | Logic = XOR  (idx: 90)
    { GPU_EQUAL,    0x00, 0x01 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:0 | Logic = XNOR (idx: 91)
    { GPU_EQUAL,    0x01, 0x01 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:0 | Logic = OR   (idx: 92)
    { GPU_EQUAL,    0x01, 0x01 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:0 | Logic = AND  (idx: 93)
    { GPU_EQUAL,    0x01, 0x01 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:0 | Logic = XOR  (idx: 94)
    { GPU_EQUAL,    0x01, 0x01 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:0 | Logic = XNOR (idx: 95)
    { GPU_EQUAL,    0x00, 0x02 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:1 | Logic = OR   (idx: 96)
    { GPU_EQUAL,    0x00, 0x02 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:1 | Logic = AND  (idx: 97)
    { GPU_EQUAL,    0x00, 0x02 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:1 | Logic = XOR  (idx: 98)
    { GPU_EQUAL,    0x00, 0x02 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:0 Ena:1 | Logic = XNOR (idx: 99)
    { GPU_EQUAL,    0x00, 0x02 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:1 | Logic = OR   (idx: 100)
    { GPU_EQUAL,    0x00, 0x02 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:1 | Logic = AND  (idx: 101)
    { GPU_EQUAL,    0x00, 0x02 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:1 | Logic = XOR  (idx: 102)
    { GPU_EQUAL,    0x00, 0x02 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:0 Ena:1 | Logic = XNOR (idx: 103)
    { GPU_EQUAL,    0x00, 0x03 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:1 | Logic = OR   (idx: 104)
    { GPU_NOTEQUAL, 0x03, 0x03 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:1 | Logic = AND  (idx: 105)
    { GPU_NOTEQUAL, 0x04, 0x04 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:1 | Logic = XOR  (idx: 106)
    { GPU_NOTEQUAL, 0x00, 0x04 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:0 Ena:1 | Logic = XNOR (idx: 107)
    { GPU_EQUAL,    0x01, 0x03 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:1 | Logic = OR   (idx: 108)
    { GPU_NOTEQUAL, 0x02, 0x03 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:1 | Logic = AND  (idx: 109)
    { GPU_NOTEQUAL, 0x00, 0x04 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:1 | Logic = XOR  (idx: 110)
    { GPU_NOTEQUAL, 0x04, 0x04 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:0 Ena:1 | Logic = XNOR (idx: 111)
    { GPU_EQUAL,    0x02, 0x02 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:1 | Logic = OR   (idx: 112)
    { GPU_EQUAL,    0x02, 0x02 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:1 | Logic = AND  (idx: 113)
    { GPU_EQUAL,    0x02, 0x02 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:1 | Logic = XOR  (idx: 114)
    { GPU_EQUAL,    0x02, 0x02 },    // 212e/f:1 | W1 Inv:0 Ena:0 | W2 Inv:1 Ena:1 | Logic = XNOR (idx: 115)
    { GPU_EQUAL,    0x02, 0x02 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:1 | Logic = OR   (idx: 116)
    { GPU_EQUAL,    0x02, 0x02 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:1 | Logic = AND  (idx: 117)
    { GPU_EQUAL,    0x02, 0x02 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:1 | Logic = XOR  (idx: 118)
    { GPU_EQUAL,    0x02, 0x02 },    // 212e/f:1 | W1 Inv:1 Ena:0 | W2 Inv:1 Ena:1 | Logic = XNOR (idx: 119)
    { GPU_EQUAL,    0x02, 0x03 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:1 | Logic = OR   (idx: 120)
    { GPU_NOTEQUAL, 0x01, 0x03 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:1 | Logic = AND  (idx: 121)
    { GPU_NOTEQUAL, 0x00, 0x04 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:1 | Logic = XOR  (idx: 122)
    { GPU_NOTEQUAL, 0x04, 0x04 },    // 212e/f:1 | W1 Inv:0 Ena:1 | W2 Inv:1 Ena:1 | Logic = XNOR (idx: 123)
    { GPU_EQUAL,    0x03, 0x03 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:1 | Logic = OR   (idx: 124)
    { GPU_NOTEQUAL, 0x00, 0x03 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:1 | Logic = AND  (idx: 125)
    { GPU_NOTEQUAL, 0x04, 0x04 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:1 | Logic = XOR  (idx: 126)
    { GPU_NOTEQUAL, 0x00, 0x04 },    // 212e/f:1 | W1 Inv:1 Ena:1 | W2 Inv:1 Ena:1 | Logic = XNOR (idx: 127)

};


// Based on the original enumeration:
//    GPU_NEVER    - 0
//    GPU_ALWAYS   - 1
//    GPU_EQUAL    - 2
//    GPU_NOTEQUAL - 3
//
GPU_TESTFUNC inverseFunction[] = { GPU_ALWAYS, GPU_NEVER, GPU_NOTEQUAL, GPU_EQUAL };
char *funcName[] = { "NVR", "ALW", "EQ ", "NEQ" };

inline u32 S9xComputeAndEnableStencilFunction(int layer, int subscreen)
{
	if (!IPPU.WindowingEnabled)
	{
		if (layer != LAYER_BACKDROP || PPU.BGMode == 5 || PPU.BGMode == 6)
			return (u32)STENCIL_TEST_DISABLED;
		
		if (subscreen == 0 && ((GFX.r2130 & 0xc0) == 0x80 || (GFX.r2130 & 0xc0) == 00))
			return (u32)STENCIL_TEST_ENABLED_WINDOWING_DISABLED;
		
		if (subscreen == 1 && ((GFX.r2130 & 0x30) == 0x10 || (GFX.r2130 & 0x30) == 0x30))
			return (u32)STENCIL_TEST_ENABLED_WINDOWING_DISABLED;
			
		return (u32)STENCIL_TEST_DISABLED;
	}
	
	uint8 windowMaskEnableFlag = (layer == LAYER_BACKDROP) ? 1 : ((Memory.FillRAM[0x212e + subscreen] >> layer) & 1);
	uint32 windowLogic = (uint32)Memory.FillRAM[0x212a] + ((uint32)Memory.FillRAM[0x212b] << 8); 
	windowLogic = (windowLogic >> (layer * 2)) & 0x3;

	uint32 windowEnableInv = (uint32)Memory.FillRAM[0x2123] + ((uint32)Memory.FillRAM[0x2124] << 8) + ((uint32)Memory.FillRAM[0x2125] << 16);
	windowEnableInv = (windowEnableInv >> (layer * 4)) & 0xF;

	int idx = windowMaskEnableFlag << 6 | windowEnableInv << 2 | windowLogic;
	GPU_TESTFUNC func = (GPU_TESTFUNC)stencilFunc[idx][0];

	if (layer == LAYER_BACKDROP)
	{
		if (subscreen == 1)
		{
			switch (GFX.r2130 & 0x30)
			{
				case 0x00:  // never prevent color math 
					func = GPU_ALWAYS;
					break;
				case 0x10:  // prevent color math outside window
					func = inverseFunction[func];
					break;
				case 0x20:  // prevent color math inside window (no change to the original mask)
					break;
				case 0x30: // always prevent color math
					func = GPU_NEVER;
					break;
			}
		}
		else 
		{
			// clip to black
			//
			switch (GFX.r2130 & 0xc0)
			{
				case 0x00:  // never clear to black
					func = GPU_NEVER;
					break;
				case 0x40:  // clear to black outside window (no change to the original mask)
					break;
				case 0x80:  // clear to black inside window 
					func = inverseFunction[func];
					break;
				case 0xc0: // always clear to black
					func = GPU_ALWAYS;
					break;
			}
		}
	}

	if (func == GPU_ALWAYS)
		return (u32)STENCIL_TEST_DISABLED;
		
	u8 ref = stencilFunc[idx][1] << 5;
	u8 inputMask = stencilFunc[idx][2] << 5;
	u8 writeMask = 0;

	return (true | ((func & 7) << 4) | (writeMask << 8) | (ref << 16) | (inputMask << 24));
}

inline void __attribute__((always_inline)) drawLayer(bool repeatLastDraw, int layer, bool sub, SGPU_TEXTURE_ID texture, SGPU_DEPTH_TEST depthTest, SGPU_ALPHA_TEST alphaTest) {
	renderState.textureBind = texture;
	renderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0_COLOR_ALPHA;
	renderState.stencilTest = S9xComputeAndEnableStencilFunction(layer, sub);
	renderState.depthTest = depthTest;
	renderState.alphaTest = alphaTest;

	u32 propertyFlags = FLAG_TEXTURE_BIND
	| FLAG_TEXTURE_ENV 
	| FLAG_STENCIL_TEST
	| FLAG_DEPTH_TEST 
	| FLAG_ALPHA_TEST;
	
	gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, propertyFlags, &renderState);
	
	SVertexList *list = &GPU3DS.vertices[VBO_SCENE];

	if (!repeatLastDraw) {
		gpu3dsDrawVertexList(list, layer);
	} else {
		gpu3dsRedrawVertexList(&GPU3DS.verticesStored[layer], &list->attrInfo);
	}
}

//-------------------------------------------------------------------
// Draw a full tile 8xh tile using 3D hardware
//-------------------------------------------------------------------
inline void __attribute__((always_inline)) S9xDrawBGFullTileHardwareInline (
    int tileSize, int tileShift, int paletteShift, int paletteMask, int startPalette, bool directColourMode,
	int prio, int depth0, int depth1, 
	int32 snesTile, int32 screenX, int32 screenY, 
	int32 startLine, int32 height)
{
    uint32 TileAddr = BG.TileAddress + ((snesTile & 0x3ff) << tileShift);

	// Bug fix: overflow in Dragon Ball Budoten 3 
	// (this was accidentally removed while optimizing for this 3DS port)
	TileAddr &= 0xff00ffff;		// hope the compiler generates a BIC instruction.

    uint32 TileNumber = TileAddr >> tileShift;
	uint32 tileAddrDiv8 = TileAddr >> 3;
    uint8 *pCache = &BG.Buffer[TileNumber << 6];
	
    if (!BG.Buffered [TileNumber])
    {
	    BG.Buffered[TileNumber] = S9xConvertTileTo8Bit (pCache, TileAddr);
        if (BG.Buffered [TileNumber] == BLANK_TILE)
            return;

		GFX.VRAMPaletteFrame[tileAddrDiv8][0] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][1] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][2] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][3] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][4] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][5] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][6] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][7] = 0;
    }

    if (BG.Buffered [TileNumber] == BLANK_TILE)
	    return;

	uint8 pal = (snesTile >> 10) & paletteMask;
	int texturePos = cache3dsGetTexturePositionFast(tileAddrDiv8, pal);

    uint32 *paletteFrame;
    uint16 *screenColors;

    if (directColourMode)
    {
        if (IPPU.DirectColourMapsNeedRebuild)
            S9xBuildDirectColourMaps ();
				
		paletteFrame = GFX.PaletteFrame;
		screenColors = DirectColourMaps[pal];	
    }
	else 
	{
		if (paletteShift == 2)
        {
            paletteFrame = GFX.PaletteFrame4BG[startPalette >> 5];
        }
        else if (paletteShift == 0)
        {
            paletteFrame = GFX.PaletteFrame256;
            pal = 0;
        }
        else
        {
            paletteFrame = GFX.PaletteFrame;
        }

		screenColors = &IPPU.ScreenColors[(pal << paletteShift) + startPalette];
	}

	if (GFX.VRAMPaletteFrame[tileAddrDiv8][pal] != paletteFrame[pal])
    {
        texturePos = cacheGetSwapTexturePositionForAltFrameFast(tileAddrDiv8, pal);
        GFX.VRAMPaletteFrame[tileAddrDiv8][pal] = paletteFrame[pal];
        cache3dsCacheSnesTileToTexturePosition(pCache, screenColors, texturePos);
    }
	

	// Render tile
	//
	int x0 = screenX;
	int y0 = screenY + (prio == 0 ? depth0 : depth1);
	int x1 = x0 + 8;
	int y1 = y0 + height;

	int tx0 = 0;
	int ty0 = startLine >> 3;
	int tx1 = 8;
	int ty1 = ty0 + height;

	gpu3dsAddTileVertexes(
		x0, y0, x1, y1,
		tx0, ty0,
		tx1, ty1, (snesTile & (H_FLIP | V_FLIP)) + texturePos);
}


//-------------------------------------------------------------------
// Draw a hi-res full tile 8xh tile using 3D hardware
//-------------------------------------------------------------------
inline void __attribute__((always_inline)) S9xDrawHiresBGFullTileHardwareInline (
    int tileSize, int tileShift, int paletteShift, int paletteMask, int startPalette, bool directColourMode,
	int prio, int depth0, int depth1,
	int32 snesTile, int32 screenX, int32 screenY,
	int32 startLine, int32 height)
{
    uint32 TileAddr = BG.TileAddress + ((snesTile & 0x3ff) << tileShift);

	// Bug fix: overflow in Dragon Ball Budoten 3 
	// (this was accidentally removed while optimizing for this 3DS port)
	TileAddr &= 0xff00ffff;		// hope the compiler generates a BIC instruction.

    uint32 TileNumber = TileAddr >> tileShift;
	uint32 tileAddrDiv8 = TileAddr >> 3;
    uint8 *pCache = &BG.Buffer[TileNumber << 6];

    if (!BG.Buffered [TileNumber])
    {
	    BG.Buffered[TileNumber] = S9xConvertTileTo8Bit (pCache, TileAddr);
        if (BG.Buffered [TileNumber] == BLANK_TILE)
            return;

		GFX.VRAMPaletteFrame[tileAddrDiv8][0] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][1] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][2] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][3] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][4] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][5] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][6] = 0;
		GFX.VRAMPaletteFrame[tileAddrDiv8][7] = 0;
    }

    if (BG.Buffered [TileNumber] == BLANK_TILE)
	    return;
		
	uint8 pal = (snesTile >> 10) & paletteMask;
	int texturePos = cache3dsGetTexturePositionFast(tileAddrDiv8, pal);
    
    uint32 *paletteFrame;
    uint16 *screenColors;
	
	if (directColourMode)
    {
        if (IPPU.DirectColourMapsNeedRebuild)
            S9xBuildDirectColourMaps ();
			
		// GFX.ScreenColors = DirectColourMaps [pal];
        screenColors = DirectColourMaps[pal];
        paletteFrame = GFX.PaletteFrame;
	}
	else 
	{
		if (paletteShift == 2)
        {
            paletteFrame = GFX.PaletteFrame4BG[startPalette >> 5];
        }
        else if (paletteShift == 0)
        {
            paletteFrame = GFX.PaletteFrame256;
            pal = 0;
        }
        else
        {
            paletteFrame = GFX.PaletteFrame;
        }

		screenColors = &IPPU.ScreenColors[(pal << paletteShift) + startPalette];
	}

	if (GFX.VRAMPaletteFrame[tileAddrDiv8][pal] != paletteFrame[pal])
    {
        texturePos = cacheGetSwapTexturePositionForAltFrameFast(tileAddrDiv8, pal);
        GFX.VRAMPaletteFrame[tileAddrDiv8][pal] = paletteFrame[pal];
        cache3dsCacheSnesTileToTexturePosition(pCache, screenColors, texturePos);
    }
	
	int x0 = screenX >> 1;
	int y0 = screenY + (prio == 0 ? depth0 : depth1);
	
	int x1 = x0 + 4;
	int y1 = y0 + height;

	int tx0 = 0;
	int ty0 = startLine >> 3;
	int tx1 = 7;
	int ty1 = ty0 + height;

	if (IPPU.Interlace)
		ty1 = ty1 + height - 1;

	gpu3dsAddTileVertexes(
		x0, y0, x1, y1,
		tx0, ty0,
		tx1, ty1, (snesTile & (H_FLIP | V_FLIP)) + texturePos);	
}



//-------------------------------------------------------------------
// Draw offset-per-tile background.
//-------------------------------------------------------------------

inline void __attribute__((always_inline)) S9xDrawOffsetBackgroundHardwarePriority0Inline (
    int tileSize, int tileShift, int bitShift, int paletteShift, int paletteMask, int startPalette, bool directColourMode,
	uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    uint32 Tile;
    uint16 *SC0;
    uint16 *SC1;
    uint16 *SC2;
    uint16 *SC3;
    uint16 *BPS0;
    uint16 *BPS1;
    uint16 *BPS2;
    uint16 *BPS3;
    //uint32 Width;
    int VOffsetOffset = BGMode == 4 ? 0 : 32;

	// Note: We draw subscreens first, then the main screen.
	// So if the subscreen has already been drawn, and we are drawing the main screen,
	// we simply just redraw the same vertices that we have saved.
	//
	if (layerVerticesCount[bg] > 0)
	{
		drawLayer(true, bg, sub, SNES_TILE_CACHE, DEPTH_TEST_ENABLED, ALPHA_TEST_NE_ZERO);

		return;
	}

    GFX.PixSize = 1;

    BG.TileSize = tileSize;
    BG.BitShift = bitShift;
    BG.TileShift = tileShift;
    BG.TileAddress = PPU.BG[bg].NameBase << 1;
    BG.NameSelect = 0;
    BG.Buffer = IPPU.TileCache [Depths [BGMode][bg]];
    BG.Buffered = IPPU.TileCached [Depths [BGMode][bg]];
    BG.PaletteShift = paletteShift;
    BG.PaletteMask = paletteMask;
    BG.DirectColourMode = directColourMode;
	//BG.Depth = depth;
	//printf ("mode:%d depth: %d\n", PPU.BGMode, BG.Depth);
	
	//BG.DrawTileCount[bg] = 0;
	
    BPS0 = (uint16 *) &Memory.VRAM[PPU.BG[2].SCBase << 1];
	
    if (PPU.BG[2].SCSize & 1)
		BPS1 = BPS0 + 1024;
    else
		BPS1 = BPS0;
	
    if (PPU.BG[2].SCSize & 2)
		BPS2 = BPS1 + 1024;
    else
		BPS2 = BPS0;
	
    if (PPU.BG[2].SCSize & 1)
		BPS3 = BPS2 + 1024;
    else
		BPS3 = BPS2;
    
    SC0 = (uint16 *) &Memory.VRAM[PPU.BG[bg].SCBase << 1];
	
    if (PPU.BG[bg].SCSize & 1)
		SC1 = SC0 + 1024;
    else
		SC1 = SC0;
	
	if(((uint8*)SC1-Memory.VRAM)>=0x10000)
		SC1-=0x08000;


    if (PPU.BG[bg].SCSize & 2)
		SC2 = SC1 + 1024;
    else
		SC2 = SC0;

	if(((uint8*)SC2-Memory.VRAM)>=0x10000)
		SC2-=0x08000;


    if (PPU.BG[bg].SCSize & 1)
		SC3 = SC2 + 1024;
    else
		SC3 = SC2;
	
	if(((uint8*)SC3-Memory.VRAM)>=0x10000)
		SC3-=0x08000;


    
    int OffsetMask;
    int OffsetShift;
    int OffsetEnableMask = 1 << (bg + 13);
	
    if (tileSize == 16)
    {
		OffsetMask = 0x3ff;
		OffsetShift = 4;
    }
    else
    {
		OffsetMask = 0x1ff;
		OffsetShift = 3;
    }

	// Optimized version of the offset per tile renderer
	//
	for (uint32 OY = GFX.StartY; OY <= GFX.EndY; )
    {
		// Do a check to find out how many scanlines
		// that the BGnVOFS, BGnHOFS, BG2VOFS, BG2HOS
		// remains constant
		//
		int TotalLines = 1;
		for (; TotalLines < PPU.ScreenHeight - 1; TotalLines++)
		{
			int y = OY + TotalLines - 1;
			if (y >= GFX.EndY)
				break;
			if (!(LineData [y].BG[bg].VOffset == LineData [y + 1].BG[bg].VOffset &&
				LineData [y].BG[bg].HOffset == LineData [y + 1].BG[bg].HOffset &&
				LineData [y].BG[2].VOffset == LineData [y + 1].BG[2].VOffset && 
				LineData [y].BG[2].HOffset == LineData [y + 1].BG[2].HOffset))
				break;
		}
		
		// For those lines, draw the tiles column by column 
		// (from the left to the right of the screen)
		//
		for (int Left = 0; Left <= 256; Left += 8)	// Bug fix: It should be Left <= 256 instead of Left < 256
		{
			for (int Y = OY; Y < OY + TotalLines; )
			{
				uint32 VOff = LineData [Y].BG[2].VOffset - 1;
		//		uint32 VOff = LineData [Y].BG[2].VOffset;
				uint32 HOff = LineData [Y].BG[2].HOffset;

				int VirtAlign;
				int ScreenLine = VOff >> 3;
				int t1;
				int t2;
				uint16 *s0;
				uint16 *s1;
				uint16 *s2;
				
				if (ScreenLine & 0x20)
					s1 = BPS2, s2 = BPS3;
				else
					s1 = BPS0, s2 = BPS1;
				
				
				s1 += (ScreenLine & 0x1f) << 5;
				s2 += (ScreenLine & 0x1f) << 5;
				
				if(BGMode != 4)
				{
					if((ScreenLine & 0x1f) == 0x1f)
					{
						if(ScreenLine & 0x20)
							VOffsetOffset = BPS0 - BPS2 - 0x1f*32;
						else
							VOffsetOffset = BPS2 - BPS0 - 0x1f*32;
					}
					else
					{
						VOffsetOffset = 32;
					}
				}
				
				//for (int clip = 0; clip < clipcount; clip++)
			
				uint32 VOffset;
				uint32 HOffset;
				//added:
				uint32 LineHOffset=LineData [Y].BG[bg].HOffset;
				
				uint32 Offset;
				uint32 HPos;
				uint32 Quot;
				uint16 *t;
				uint32 Quot2;
				uint32 VCellOffset;
				uint32 HCellOffset;
				uint16 *b1;
				uint16 *b2;
				uint32 Lines;
				
				//int sX = Left;
				bool8 left_hand_edge = (Left == 0);
				//Width = Right - Left;
				
				//while (Left < Right) 
				{
					if (left_hand_edge)
					{
						// The SNES offset-per-tile background mode has a
						// hardware limitation that the offsets cannot be set
						// for the tile at the left-hand edge of the screen.
						VOffset = LineData [Y].BG[bg].VOffset;

						//MKendora; use temp var to reduce memory accesses
						//HOffset = LineData [Y].BG[bg].HOffset;

						HOffset = LineHOffset;
						//End MK

						left_hand_edge = FALSE;
					}
					else
					{
						// All subsequent offset tile data is shifted left by one,
						// hence the - 1 below.

						Quot2 = ((HOff + Left - 1) & OffsetMask) >> 3;
						
						if (Quot2 > 31)
							s0 = s2 + (Quot2 & 0x1f);
						else
							s0 = s1 + Quot2;
						
						HCellOffset = READ_2BYTES (s0);
						
						if (BGMode == 4)
						{
							VOffset = LineData [Y].BG[bg].VOffset;
							
							//MKendora another mem access hack
							//HOffset = LineData [Y].BG[bg].HOffset;
							HOffset=LineHOffset;
							//end MK

							if ((HCellOffset & OffsetEnableMask))
							{
								if (HCellOffset & 0x8000)
									VOffset = HCellOffset + 1;
								else
									HOffset = HCellOffset;
							}
						}
						else
						{
							VCellOffset = READ_2BYTES (s0 + VOffsetOffset);
							if ((VCellOffset & OffsetEnableMask))
								VOffset = VCellOffset + 1;
							else
								VOffset = LineData [Y].BG[bg].VOffset;

							//MKendora Strike Gunner fix
							if ((HCellOffset & OffsetEnableMask))
							{
								//HOffset= HCellOffset;
								
								HOffset = (HCellOffset & ~7)|(LineHOffset&7);
								//HOffset |= LineData [Y].BG[bg].HOffset&7;
							}
							else
								HOffset=LineHOffset;
								//HOffset = LineData [Y].BG[bg].HOffset - 
								//Settings.StrikeGunnerOffsetHack;
							//HOffset &= (~7);
							//end MK
						}
					}
					VirtAlign = ((Y + VOffset) & 7) << 3;
					Lines = 8 - (VirtAlign >> 3);
					//printf ("    L=%d\n", Lines);
					if (Y + Lines >= OY + TotalLines)
						Lines = OY + TotalLines - Y;
					ScreenLine = (VOffset + Y) >> OffsetShift;
					
					if (((VOffset + Y) & 15) > 7)
					{
						t1 = 16;
						t2 = 0;
					}
					else
					{
						t1 = 0;
						t2 = 16;
					}
					
					if (ScreenLine & 0x20)
						b1 = SC2, b2 = SC3;
					else
						b1 = SC0, b2 = SC1;
					
					b1 += (ScreenLine & 0x1f) << 5;
					b2 += (ScreenLine & 0x1f) << 5;
					
					HPos = (HOffset + Left) & OffsetMask;
					
					Quot = HPos >> 3;
					
					if (tileSize == 8)
					{
						if (Quot > 31)
							t = b2 + (Quot & 0x1f);
						else
							t = b1 + Quot;
					}
					else
					{
						if (Quot > 63)
							t = b2 + ((Quot >> 1) & 0x1f);
						else
							t = b1 + (Quot >> 1);
					}
					
					Offset = HPos & 7;
					
					int sX = Left - Offset;

					// Don't display anything beyond the right edge.
					if (sX >= 256)
						break;

					Tile = READ_2BYTES(t);

					int tpriority = (Tile & 0x2000) >> 13;
					
					int32 modifiedTile;

					if (tileSize == 8) {
						modifiedTile = Tile;
					} else {
						if (!(Tile & (V_FLIP | H_FLIP))) {
							modifiedTile = Tile + t1 + (Quot & 1);
						} else if (Tile & H_FLIP) {
							modifiedTile = (Tile & V_FLIP) 
								? Tile + t2 + 1 - (Quot & 1)
								: Tile + t1 + 1 - (Quot & 1);
						} else {
							modifiedTile = Tile + t2 + (Quot & 1);
						}
					}

					S9xDrawBGFullTileHardwareInline(
						tileSize, tileShift, paletteShift, paletteMask, startPalette, directColourMode,
						tpriority, depth0, depth1,
						modifiedTile, sX, Y, 
						VirtAlign, Lines);
				}

				// Proceed to the tile below, in the same column.
				//
				Y += Lines;
			}
		}
		OY += TotalLines;
    }

	layerVerticesCount[bg] = GPU3DS.vertices[VBO_SCENE].Count;

	if (layerVerticesCount[bg] > 0)
		drawLayer(false, bg, sub, SNES_TILE_CACHE, DEPTH_TEST_ENABLED, ALPHA_TEST_NE_ZERO);
}


//-------------------------------------------------------------------
// 4-color offset-per tile BGs, priority 0
//-------------------------------------------------------------------

void S9xDrawOffsetBackgroundHardwarePriority0Inline_4Color_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawOffsetBackgroundHardwarePriority0Inline(
        8,              // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawOffsetBackgroundHardwarePriority0Inline_4Color_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawOffsetBackgroundHardwarePriority0Inline(
        16,             // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawOffsetBackgroundHardwarePriority0Inline_4Color
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    if (BGSizes [PPU.BG[bg].BGSize] == 8)
    {
        S9xDrawOffsetBackgroundHardwarePriority0Inline_4Color_8x8(
            BGMode, bg, sub, depth0, depth1);
    }
    else
    {
        S9xDrawOffsetBackgroundHardwarePriority0Inline_4Color_16x16(
            BGMode, bg, sub, depth0, depth1);
    }
}

//-------------------------------------------------------------------
// 16-color offset-per tile BGs, priority 0
//-------------------------------------------------------------------

void S9xDrawOffsetBackgroundHardwarePriority0Inline_16Color_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawOffsetBackgroundHardwarePriority0Inline(
        8,              // tileSize
        5,              // tileShift
		4,				// bitShift
        4,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawOffsetBackgroundHardwarePriority0Inline_16Color_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawOffsetBackgroundHardwarePriority0Inline(
        16,             // tileSize
        5,              // tileShift
		4,				// bitShift
        4,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawOffsetBackgroundHardwarePriority0Inline_16Color
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    if (BGSizes [PPU.BG[bg].BGSize] == 8)
    {
        S9xDrawOffsetBackgroundHardwarePriority0Inline_16Color_8x8(
            BGMode, bg, sub, depth0, depth1);
    }
    else
    {
        S9xDrawOffsetBackgroundHardwarePriority0Inline_16Color_16x16(
            BGMode, bg, sub, depth0, depth1);
    }
}


//-------------------------------------------------------------------
// 256-color offset-per tile BGs, priority 0
//-------------------------------------------------------------------

void S9xDrawOffsetBackgroundHardwarePriority0Inline_256NormalColor_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawOffsetBackgroundHardwarePriority0Inline(
        8,              // tileSize
        6,              // tileShift
		8,				// bitShift
        0,              // paletteShift
        0,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawOffsetBackgroundHardwarePriority0Inline_256NormalColor_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawOffsetBackgroundHardwarePriority0Inline(
        16,             // tileSize
        6,              // tileShift
		8,				// bitShift
        0,              // paletteShift
        0,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawOffsetBackgroundHardwarePriority0Inline_256DirectColor_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawOffsetBackgroundHardwarePriority0Inline(
        8,              // tileSize
        6,              // tileShift
		8,				// bitShift
        0,              // paletteShift
        0,              // paletteMask
        0,              // startPalette
        TRUE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawOffsetBackgroundHardwarePriority0Inline_256DirectColor_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawOffsetBackgroundHardwarePriority0Inline(
        16,             // tileSize
        6,              // tileShift
		8,				// bitShift
        0,              // paletteShift
        0,              // paletteMask
        0,              // startPalette
        TRUE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawOffsetBackgroundHardwarePriority0Inline_256Color
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    if (BGSizes [PPU.BG[bg].BGSize] == 8)
    {
        if (!(GFX.r2130 & 1))
            S9xDrawOffsetBackgroundHardwarePriority0Inline_256NormalColor_8x8(
                BGMode, bg, sub, depth0, depth1);
        else
            S9xDrawOffsetBackgroundHardwarePriority0Inline_256DirectColor_8x8(
                BGMode, bg, sub, depth0, depth1);
    }
    else
    {
        if (!(GFX.r2130 & 1))
            S9xDrawOffsetBackgroundHardwarePriority0Inline_256NormalColor_16x16(
                BGMode, bg, sub, depth0, depth1);
        else
            S9xDrawOffsetBackgroundHardwarePriority0Inline_256DirectColor_16x16(
                BGMode, bg, sub, depth0, depth1);
    }
}



//-------------------------------------------------------------------
// Draw non-offset-per-tile backgrounds
//-------------------------------------------------------------------
inline void __attribute__((always_inline)) S9xDrawBackgroundHardwarePriority0Inline (
    int tileSize, int tileShift, int bitShift, int paletteShift, int paletteMask, int startPalette, bool directColourMode,
    uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    GFX.PixSize = 1;

	//printf ("BG%d Y=%d-%d W1:%d-%d W2:%d-%d\n", bg, GFX.StartY, GFX.EndY, PPU.Window1Left, PPU.Window1Right, PPU.Window2Left, PPU.Window2Right);

	// Note: We draw subscreens first, then the main screen.
	// So if the subscreen has already been drawn, and we are drawing the main screen,
	// we simply just redraw the same vertices that we have saved.
	//
	if (layerVerticesCount[bg] > 0)
	{
		drawLayer(true, bg, sub, SNES_TILE_CACHE, DEPTH_TEST_ENABLED, ALPHA_TEST_NE_ZERO);

		return;
	}

    BG.TileSize = tileSize;
    BG.BitShift = bitShift;
    BG.TileShift = tileShift;
    BG.TileAddress = PPU.BG[bg].NameBase << 1;
    BG.NameSelect = 0;
    BG.Buffer = IPPU.TileCache [Depths [BGMode][bg]];
    BG.Buffered = IPPU.TileCached [Depths [BGMode][bg]];
    BG.PaletteShift = paletteShift;
    BG.PaletteMask = paletteMask;
    BG.DirectColourMode = directColourMode;
	//BG.Depth = depth;

	//BG.DrawTileCount[bg] = 0;


    uint32 Tile;
    uint16 *SC0;
    uint16 *SC1;
    uint16 *SC2;
    uint16 *SC3;
    uint32 Width;

    if (BGMode == 0)
		BG.StartPalette = startPalette;
    else BG.StartPalette = 0;

    SC0 = (uint16 *) &Memory.VRAM[PPU.BG[bg].SCBase << 1];

	
    if (PPU.BG[bg].SCSize & 1)
		SC1 = SC0 + 1024;
    else
		SC1 = SC0;

	if(SC1>=(unsigned short*)(Memory.VRAM+0x10000))
		SC1=(uint16*)&Memory.VRAM[((uint8*)SC1-&Memory.VRAM[0])%0x10000];

    if (PPU.BG[bg].SCSize & 2)
		SC2 = SC1 + 1024;
    else
		SC2 = SC0;

	if(((uint8*)SC2-Memory.VRAM)>=0x10000)
		SC2-=0x08000;

    if (PPU.BG[bg].SCSize & 1)
		SC3 = SC2 + 1024;
    else
		SC3 = SC2;

	if(((uint8*)SC3-Memory.VRAM)>=0x10000)
		SC3-=0x08000;

    int Lines;
    int OffsetMask;
    int OffsetShift;

    if (tileSize == 16)
    {
		OffsetMask = 0x3ff;
		OffsetShift = 4;
    }
    else
    {
		OffsetMask = 0x1ff;
		OffsetShift = 3;
    }

    for (uint32 Y = GFX.StartY; Y <= GFX.EndY; Y += Lines)
    {
		uint32 VOffset = LineData [Y].BG[bg].VOffset;
		uint32 HOffset = LineData [Y].BG[bg].HOffset;

		int VirtAlign = (Y + VOffset) & 7;

		for (Lines = 1; Lines < 8 - VirtAlign; Lines++)
			if ((VOffset != LineData [Y + Lines].BG[bg].VOffset) ||
				(HOffset != LineData [Y + Lines].BG[bg].HOffset))
				break;

		if (Y + Lines > GFX.EndY)
			Lines = GFX.EndY + 1 - Y;

		//if (GFX.EndY - GFX.StartY < 10)
		//printf ("bg:%d Y/L:%3d/%3d OFS:%d,%d\n", bg, Y, Lines, HOffset, VOffset);

		VirtAlign <<= 3;

		uint32 ScreenLine = (VOffset + Y) >> OffsetShift;
		uint32 t1;
		uint32 t2;
		if (((VOffset + Y) & 15) > 7)
		{
			t1 = 16;
			t2 = 0;
		}
		else
		{
			t1 = 0;
			t2 = 16;
		}
		uint16 *b1;
		uint16 *b2;

		if (ScreenLine & 0x20)
			b1 = SC2, b2 = SC3;
		else
			b1 = SC0, b2 = SC1;

		b1 += (ScreenLine & 0x1f) << 5;
		b2 += (ScreenLine & 0x1f) << 5;

		//int clipcount = GFX.pCurrentClip->Count [bg];
		//if (!clipcount)
		//	clipcount = 1;
		//for (int clip = 0; clip < clipcount; clip++)
		{
			uint32 Left;
			uint32 Right;

			//if (!GFX.pCurrentClip->Count [bg])
			{
				Left = 0;
				Right = 256;
			}


			//uint32 s = Left * GFX.PixSize + Y * GFX.PPL;
			uint32 s = Left * GFX.PixSize + Y * 256;	
			//printf ("s = %d, Lines = %d\n", s, Lines);
			uint32 HPos = (HOffset + Left) & OffsetMask;

			uint32 Quot = HPos >> 3;
			uint32 Count = 0;

			uint16 *t;
			if (tileSize == 8)
			{
				if (Quot > 31)
					t = b2 + (Quot & 0x1f);
				else
					t = b1 + Quot;
			}
			else
			{
				if (Quot > 63)
					t = b2 + ((Quot >> 1) & 0x1f);
				else
					t = b1 + (Quot >> 1);
			}

			// screen coordinates of the tile.
			int sX = 0 - (HPos & 7);
			int sY = Y;


			int tilesToDraw = 32;
			if (sX != 0)
				tilesToDraw++;

			// Middle, unclipped tiles
			//Count = Width - Count;
			//int Middle = Count >> 3;
			//Count &= 7;

			//for (int C = Middle; C > 0; s += 8 * GFX.PixSize, Quot++, C--)
			for (int tno = 0; tno <= tilesToDraw; tno++, sX += 8, Quot++)
			{
				Tile = READ_2BYTES(t);

				int tpriority = (Tile & 0x2000) >> 13;
				//if (tpriority == priority)
				{
					int32 modifiedTile;
					
					if (tileSize == 8) {
						modifiedTile = Tile;
					} else {
						if (Tile & H_FLIP) {
							modifiedTile = (Tile & V_FLIP)
								? Tile + t2 + 1 - (Quot & 1)
								: Tile + t1 + 1 - (Quot & 1);
						} else {
							modifiedTile = (Tile & V_FLIP)
								? Tile + t2 + (Quot & 1)
								: Tile + t1 + (Quot & 1);
						}
					}

					S9xDrawBGFullTileHardwareInline(
						tileSize, tileShift, paletteShift, paletteMask, startPalette, directColourMode,
						tpriority, depth0, depth1,
						modifiedTile, sX, sY, VirtAlign, Lines);
				}

				if (tileSize == 8)
				{
					t++;
					if (Quot == 31)
						t = b2;
					else
						if (Quot == 63)
							t = b1;
				}
				else
				{
					t += Quot & 1;
					if (Quot == 63)
						t = b2;
					else
						if (Quot == 127)
							t = b1;
				}
			}

		}
    }

	layerVerticesCount[bg] = GPU3DS.vertices[VBO_SCENE].Count;
	
	if (layerVerticesCount[bg] > 0)
		drawLayer(false, bg, sub, SNES_TILE_CACHE, DEPTH_TEST_ENABLED, ALPHA_TEST_NE_ZERO);
}


//-------------------------------------------------------------------
// 4-color BGs, priority 0
//-------------------------------------------------------------------

void S9xDrawBackgroundHardwarePriority0Inline_4Color_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        8,              // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawBackgroundHardwarePriority0Inline_4Color_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        16,             // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawBackgroundHardwarePriority0Inline_Mode0_4Color_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        8,              // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        bg << 5,        // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawBackgroundHardwarePriority0Inline_Mode0_4Color_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        16,             // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        bg << 5,        // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}


void S9xDrawBackgroundHardwarePriority0Inline_4Color
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    if (BGMode != 0)
    {
        if (BGSizes [PPU.BG[bg].BGSize] == 8)
            S9xDrawBackgroundHardwarePriority0Inline_4Color_8x8(
                BGMode, bg, sub, depth0, depth1);
        else
            S9xDrawBackgroundHardwarePriority0Inline_4Color_16x16(
                BGMode, bg, sub, depth0, depth1);
    }
    else
    {
        if (BGSizes [PPU.BG[bg].BGSize] == 8)
            S9xDrawBackgroundHardwarePriority0Inline_Mode0_4Color_8x8(
                BGMode, bg, sub, depth0, depth1);
        else
            S9xDrawBackgroundHardwarePriority0Inline_Mode0_4Color_16x16(
                BGMode, bg, sub, depth0, depth1);
    }
}



//-------------------------------------------------------------------
// 16-color BGs, priority 0
//-------------------------------------------------------------------

void S9xDrawBackgroundHardwarePriority0Inline_16Color_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        8,              // tileSize
        5,              // tileShift
		4,				// bitShift
        4,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawBackgroundHardwarePriority0Inline_16Color_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        16,             // tileSize
        5,              // tileShift
		4,				// bitShift
        4,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawBackgroundHardwarePriority0Inline_16Color
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    if (BGSizes [PPU.BG[bg].BGSize] == 8)
    {
        S9xDrawBackgroundHardwarePriority0Inline_16Color_8x8(
            BGMode, bg, sub, depth0, depth1);
    }
    else
    {
        S9xDrawBackgroundHardwarePriority0Inline_16Color_16x16(
            BGMode, bg, sub, depth0, depth1);
    }
}



//-------------------------------------------------------------------
// 256-color BGs, priority 0
//-------------------------------------------------------------------

void S9xDrawBackgroundHardwarePriority0Inline_256NormalColor_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        8,              // tileSize
        6,              // tileShift
		8,				// bitShift
        0,              // paletteShift
        0,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawBackgroundHardwarePriority0Inline_256NormalColor_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        16,             // tileSize
        6,              // tileShift
		8,				// bitShift
        0,              // paletteShift
        0,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawBackgroundHardwarePriority0Inline_256DirectColor_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        8,              // tileSize
        6,              // tileShift
		8,				// bitShift
        0,              // paletteShift
        0,              // paletteMask
        0,              // startPalette
        TRUE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawBackgroundHardwarePriority0Inline_256DirectColor_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawBackgroundHardwarePriority0Inline(
        16,             // tileSize
        6,              // tileShift
		8,				// bitShift
        0,              // paletteShift
        0,              // paletteMask
        0,              // startPalette
        TRUE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawBackgroundHardwarePriority0Inline_256Color
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    if (BGSizes [PPU.BG[bg].BGSize] == 8)
    {
        if (!(GFX.r2130 & 1))
            S9xDrawBackgroundHardwarePriority0Inline_256NormalColor_8x8(
                BGMode, bg, sub, depth0, depth1);
        else
            S9xDrawBackgroundHardwarePriority0Inline_256DirectColor_8x8(
                BGMode, bg, sub, depth0, depth1);
    }
    else
    {
        if (!(GFX.r2130 & 1))
            S9xDrawBackgroundHardwarePriority0Inline_256NormalColor_16x16(
                BGMode, bg, sub, depth0, depth1);
        else
            S9xDrawBackgroundHardwarePriority0Inline_256DirectColor_16x16(
                BGMode, bg, sub, depth0, depth1);
    }
}


//-------------------------------------------------------------------
// Draw hires backgrounds
//-------------------------------------------------------------------
inline void __attribute__((always_inline)) S9xDrawHiresBackgroundHardwarePriority0Inline (
    int tileSize, int tileShift, int bitShift, int paletteShift, int paletteMask, int startPalette, bool directColourMode,
    uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    GFX.PixSize = 1;

 	// Note: We draw subscreens first, then the main screen.
	// So if the subscreen has already been drawn, and we are drawing the main screen,
	// we simply just redraw the same vertices that we have saved.
	//
	if (layerVerticesCount[bg] > 0)
	{
		drawLayer(true, bg, sub, SNES_TILE_CACHE, DEPTH_TEST_ENABLED, ALPHA_TEST_NE_ZERO);

		return;
	}

	//printf ("BG%d Y=%d-%d W1:%d-%d W2:%d-%d\n", bg, GFX.StartY, GFX.EndY, PPU.Window1Left, PPU.Window1Right, PPU.Window2Left, PPU.Window2Right);

    BG.TileSize = tileSize;
    BG.BitShift = bitShift;
    BG.TileShift = tileShift;
    BG.TileAddress = PPU.BG[bg].NameBase << 1;
    BG.NameSelect = 0;
    BG.Buffer = IPPU.TileCache [Depths [BGMode][bg]];
    BG.Buffered = IPPU.TileCached [Depths [BGMode][bg]];
    BG.PaletteShift = paletteShift;
    BG.PaletteMask = paletteMask;
    BG.DirectColourMode = directColourMode;
	//BG.Depth = depth;

	//BG.DrawTileCount[bg] = 0;

    uint32 Tile;
    uint16 *SC0;
    uint16 *SC1;
    uint16 *SC2;
    uint16 *SC3;
    uint32 Width;
    //uint8 depths [2] = {Z1, Z2};

    BG.StartPalette = 0;
	
    SC0 = (uint16 *) &Memory.VRAM[PPU.BG[bg].SCBase << 1];
	
    if ((PPU.BG[bg].SCSize & 1))
		SC1 = SC0 + 1024;
    else
		SC1 = SC0;
	
	if((SC1-(unsigned short*)Memory.VRAM)>0x10000)
		SC1=(uint16*)&Memory.VRAM[(((uint8*)SC1)-Memory.VRAM)%0x10000];
	
    if ((PPU.BG[bg].SCSize & 2))
		SC2 = SC1 + 1024;
    else SC2 = SC0;
	
	if(((uint8*)SC2-Memory.VRAM)>=0x10000)
		SC2-=0x08000;

    if ((PPU.BG[bg].SCSize & 1))
		SC3 = SC2 + 1024;
    else
		SC3 = SC2;
    
	if(((uint8*)SC3-Memory.VRAM)>=0x10000)
		SC3-=0x08000;

	
	
    int Lines;
    int VOffsetMask;
    int VOffsetShift;
	
    if (tileSize == 16)
    {
		VOffsetMask = 0x3ff;
		VOffsetShift = 4;
    }
    else
    {
		VOffsetMask = 0x1ff;
		VOffsetShift = 3;
    }

    int endy = IPPU.Interlace ? 1 + (GFX.EndY << 1) : GFX.EndY;
	
    for (int Y = IPPU.Interlace ? GFX.StartY << 1 : GFX.StartY; Y <= endy; Y += Lines)
    {
		int y = IPPU.Interlace ? (Y >> 1) : Y;
		uint32 VOffset = LineData [y].BG[bg].VOffset;
		uint32 HOffset = LineData [y].BG[bg].HOffset;
		int VirtAlign = (Y + VOffset) & 7;
		
		for (Lines = 1; Lines < 8 - VirtAlign; Lines++)
			if ((VOffset != LineData [y + Lines].BG[bg].VOffset) ||
				(HOffset != LineData [y + Lines].BG[bg].HOffset))
				break;
			
		HOffset <<= 1;
		if (Y + Lines > endy)
			Lines = endy + 1 - Y;
		VirtAlign <<= 3;
		
		int ScreenLine = (VOffset + Y) >> VOffsetShift;
		int t1;
		int t2;
		if (((VOffset + Y) & 15) > 7)
		{
			t1 = 16;
			t2 = 0;
		}
		else
		{
			t1 = 0;
			t2 = 16;
		}
		uint16 *b1;
		uint16 *b2;
		
		if (ScreenLine & 0x20)
			b1 = SC2, b2 = SC3;
		else
			b1 = SC0, b2 = SC1;
		
		b1 += (ScreenLine & 0x1f) << 5;
		b2 += (ScreenLine & 0x1f) << 5;

		//int clipcount = GFX.pCurrentClip->Count [bg];
		//if (!clipcount)
		//	clipcount = 1;
		//for (int clip = 0; clip < clipcount; clip++)
		{
			uint32 Left;
			uint32 Right;

			//if (!GFX.pCurrentClip->Count [bg])
			{
				Left = 0;
				Right = 256;
			}
			/*else
			{
				Left = GFX.pCurrentClip->Left [clip][bg];
				Right = GFX.pCurrentClip->Right [clip][bg];

				if (Right <= Left)
					continue;
			}*/

			//uint32 s = Left * GFX.PixSize + Y * GFX.PPL;
			uint32 s = Left * GFX.PixSize + Y * 256;		// Once hardcoded, Hires mode no longer supported.
			uint32 HPos = (HOffset + Left * GFX.PixSize) & 0x3ff;
			
			uint32 Quot = HPos >> 3;
			uint32 Count = 0;
			
			uint16 *t;
			if (Quot > 63)
				t = b2 + ((Quot >> 1) & 0x1f);
			else
				t = b1 + (Quot >> 1);
				

			// screen coordinates of the tile.
			int sX = 0 - (HPos & 7);
			int sY = Y;
			int actualLines = Lines;
			if (IPPU.Interlace)
			{
				sY = sY >> 1;
				actualLines = actualLines >> 1;
			}

			int tilesToDraw = 64;
			if (sX != 0)
				tilesToDraw += 2;

			for (int tno = 0; tno <= tilesToDraw; tno++, sX += 8, Quot++)
			{
				Tile = READ_2BYTES(t);

				int tpriority = (Tile & 0x2000) >> 13;
				int32 modifiedTile;

				if (tileSize == 8) {
					modifiedTile = Tile + ((Tile & H_FLIP) 
						? (1 - (Quot & 1)) 
						: (Quot & 1));
				} else {				
					if (!(Tile & (V_FLIP | H_FLIP))) {
						modifiedTile = Tile + t1 + (Quot & 1);
					} else if (Tile & H_FLIP) {
						modifiedTile = (Tile & V_FLIP)
							? Tile + t2 + 1 - (Quot & 1)
							: Tile + t1 + 1 - (Quot & 1);
					} else {
						modifiedTile = Tile + t2 + (Quot & 1);
					}
				}

				S9xDrawHiresBGFullTileHardwareInline(
					tileSize, tileShift, paletteShift, paletteMask, startPalette, directColourMode,
					tpriority, depth0, depth1,
					modifiedTile, sX, sY, VirtAlign, actualLines);
				
				t += Quot & 1;
				if (Quot == 63)
					t = b2;
				else
					if (Quot == 127)
						t = b1;
			}
		}
    }

	layerVerticesCount[bg] = GPU3DS.vertices[VBO_SCENE].Count;

	if (layerVerticesCount[bg] > 0)
		drawLayer(false, bg, sub, SNES_TILE_CACHE, DEPTH_TEST_ENABLED, ALPHA_TEST_NE_ZERO);
}




//-------------------------------------------------------------------
// 4-color BGs, priority 0
//-------------------------------------------------------------------

void S9xDrawHiresBackgroundHardwarePriority0Inline_4Color_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawHiresBackgroundHardwarePriority0Inline(
        8,              // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawHiresBackgroundHardwarePriority0Inline_4Color_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawHiresBackgroundHardwarePriority0Inline(
        16,             // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawHiresBackgroundHardwarePriority0Inline_Mode0_4Color_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawHiresBackgroundHardwarePriority0Inline(
        8,              // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        bg << 5,        // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawHiresBackgroundHardwarePriority0Inline_Mode0_4Color_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawHiresBackgroundHardwarePriority0Inline(
        16,             // tileSize
        4,              // tileShift
		2,				// bitShift
        2,              // paletteShift
        7,              // paletteMask
        bg << 5,        // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}


void S9xDrawHiresBackgroundHardwarePriority0Inline_4Color
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    if (BGMode != 0)
    {
        if (BGSizes [PPU.BG[bg].BGSize] == 8)
            S9xDrawHiresBackgroundHardwarePriority0Inline_4Color_8x8(
                BGMode, bg, sub, depth0, depth1);
        else
            S9xDrawHiresBackgroundHardwarePriority0Inline_4Color_16x16(
                BGMode, bg, sub, depth0, depth1);
    }
    else
    {
        if (BGSizes [PPU.BG[bg].BGSize] == 8)
            S9xDrawHiresBackgroundHardwarePriority0Inline_Mode0_4Color_8x8(
                BGMode, bg, sub, depth0, depth1);
        else
            S9xDrawHiresBackgroundHardwarePriority0Inline_Mode0_4Color_16x16(
                BGMode, bg, sub, depth0, depth1);
    }
}


//-------------------------------------------------------------------
// 16-color Hires BGs, priority 0
//-------------------------------------------------------------------

void S9xDrawHiresBackgroundHardwarePriority0Inline_16Color_8x8
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawHiresBackgroundHardwarePriority0Inline(
        8,              // tileSize
        5,              // tileShift
		4,				// bitShift
        4,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawHiresBackgroundHardwarePriority0Inline_16Color_16x16
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    S9xDrawHiresBackgroundHardwarePriority0Inline(
        16,             // tileSize
        5,              // tileShift
		4,				// bitShift
        4,              // paletteShift
        7,              // paletteMask
        0,              // startPalette
        FALSE,          // directColourMode
        BGMode, bg, sub, depth0, depth1);
}

void S9xDrawHiresBackgroundHardwarePriority0Inline_16Color
    (uint32 BGMode, uint32 bg, bool sub, int depth0, int depth1)
{
    if (BGSizes [PPU.BG[bg].BGSize] == 8)
    {
        S9xDrawHiresBackgroundHardwarePriority0Inline_16Color_8x8(
            BGMode, bg, sub, depth0, depth1);
    }
    else
    {
        S9xDrawHiresBackgroundHardwarePriority0Inline_16Color_16x16(
            BGMode, bg, sub, depth0, depth1);
    }
}




inline void __attribute__((always_inline)) S9xDrawOBJTileHardware2 (
	bool sub, int depth, 
	uint32 snesTile,
	int screenX, int screenY, uint32 textureYOffset, int height)
{
    uint32 TileAddr = BG.TileAddress + ((snesTile & 0x1ff) << 5);

	// OBJ tiles can be name-selected.
	if ((snesTile & 0x1ff) >= 256)
		TileAddr += BG.NameSelect;
	TileAddr &= 0xffff;

    uint32 TileNumber = TileAddr >> 5;
	uint32 tileAddrDiv8 = TileAddr >> 3;
    uint8 *pCache = &BG.Buffer[TileNumber << 6];

    if (!BG.Buffered [TileNumber])
    {
	    BG.Buffered[TileNumber] = S9xConvertTileTo8Bit (pCache, TileAddr);
        if (BG.Buffered [TileNumber] == BLANK_TILE)
            return;

        GFX.VRAMPaletteFrame[tileAddrDiv8][8] = 0;
        GFX.VRAMPaletteFrame[tileAddrDiv8][9] = 0;
        GFX.VRAMPaletteFrame[tileAddrDiv8][10] = 0;
        GFX.VRAMPaletteFrame[tileAddrDiv8][11] = 0;
        GFX.VRAMPaletteFrame[tileAddrDiv8][12] = 0;
        GFX.VRAMPaletteFrame[tileAddrDiv8][13] = 0;
        GFX.VRAMPaletteFrame[tileAddrDiv8][14] = 0;
        GFX.VRAMPaletteFrame[tileAddrDiv8][15] = 0;
    }

    if (BG.Buffered [TileNumber] == BLANK_TILE)
	    return;

    uint8 pal = (snesTile >> 10) & 7;
    int texturePos = cache3dsGetTexturePositionFast(tileAddrDiv8, pal + 8);

	if (GFX.VRAMPaletteFrame[tileAddrDiv8][pal + 8] != GFX.PaletteFrame[pal + 8])
    {
        texturePos = cacheGetSwapTexturePositionForAltFrameFast(tileAddrDiv8, pal + 8);
        GFX.VRAMPaletteFrame[tileAddrDiv8][pal + 8] = GFX.PaletteFrame[pal + 8];

        uint16 *screenColors = &IPPU.ScreenColors[(pal << 4) + 128];
        cache3dsCacheSnesTileToTexturePosition(pCache, screenColors, texturePos);
    }

	// Render tile
	//
	// Remove the test for sub screen (fixed Mickey mouse transparency problem when Mickey's
	// talking to the wizard)
	//
	if (pal < 4)					
		depth = depth & 0xfff;		// remove the alpha.
	int x0 = screenX;
	int y0 = screenY + depth;
		
	int x1 = x0 + 8;
	int y1 = y0 + height;

	int tx0 = 0;
	int ty0 = textureYOffset;
	int tx1 = tx0 + 8;
	int ty1 = ty0 + height;

	gpu3dsAddTileVertexes(
		x0, y0, x1, y1,
		tx0, ty0,
		tx1, ty1, (snesTile & (V_FLIP | H_FLIP)) + texturePos);
}


typedef struct 
{
	int		Height;
	int		Y;
	int		StartLine;
} SOBJList;

SOBJList OBJList[128];


//-------------------------------------------------------------------
// Draw the OBJ layers using 3D hardware.
//-------------------------------------------------------------------
void S9xDrawOBJSHardware (bool8 sub, int depth = 0, int priority = 0)
{
	// Note: We draw subscreens first, then the main screen.
	// So if the subscreen has already been drawn, and we are drawing the main screen,
	// we simply just redraw the same vertices that we have saved.
	//
	if (layerVerticesCount[LAYER_OBJ] > 0)
	{
		drawLayer(true, LAYER_OBJ, sub, SNES_TILE_CACHE, DEPTH_TEST_DISABLED, ALPHA_TEST_NE_ZERO);

		return;
	}
	
#ifdef MK_DEBUG_RTO
	if(Settings.BGLayering) fprintf(stderr, "Entering DrawOBJS() for %d-%d\n", GFX.StartY, GFX.EndY);
#endif
	CHECK_SOUND();

	//printf ("--------------------\n");
	int p = 0;			// To be used in the DrawTileLater/DrawClippedTileLater macros.

	BG.BitShift = 4;
	BG.TileShift = 5;
	BG.TileAddress = PPU.OBJNameBase;
	BG.StartPalette = 128;
	BG.PaletteShift = 4;
	BG.PaletteMask = 7;
	BG.Buffer = IPPU.TileCache [TILE_4BIT];
	BG.Buffered = IPPU.TileCached [TILE_4BIT];
	BG.NameSelect = PPU.OBJNameSelect;
	BG.DirectColourMode = FALSE;
	BG.Depth = depth;

	GFX.PixSize = 1;
	
	// Wonder what is the best value for this to get the optimal performance? 
	if (PPU.PriorityDrawFromSprite >= 0 && GFX.EndY - GFX.StartY >= 16)
	{
		//printf ("Fast OBJ draw %d\n", PPU.PriorityDrawFromSprite);
		// Clear all heights
		for (int i = 0; i < 128;)
		{
			OBJList[i++].Height = 0;
			OBJList[i++].Height = 0;
			OBJList[i++].Height = 0;
			OBJList[i++].Height = 0;
			OBJList[i++].Height = 0;
			OBJList[i++].Height = 0;
			OBJList[i++].Height = 0;
			OBJList[i++].Height = 0;
		}
		for(uint32 Y=GFX.StartY; Y<=GFX.EndY; Y++)
		{
			for (int I = GFX.OBJLines[Y].OBJCount - 1; I >= 0; I --)
			{
				int S = GFX.OBJLines[Y].OBJ[I].Sprite;
				if (S < 0) continue;

				if (OBJList[S].Height == 0)
				{
					OBJList[S].Y = Y;
					OBJList[S].StartLine = GFX.OBJLines[Y].OBJ[I].Line;
				}
				OBJList[S].Height ++;
			}
		}

		int FirstSprite = PPU.PriorityDrawFromSprite;
		int StartDrawingSprite = (FirstSprite - 1) & 0x7F;
		int S = StartDrawingSprite;
		do {
			if (OBJList[S].Height)
			{
				int Height = OBJList[S].Height;
				int Y = OBJList[S].Y;
				int StartLine = OBJList[S].StartLine;
				
				int priorityOffset = (PPU.OBJ[S].Priority + 1) * 3 * 256 + depth;
				bool isVFlipped = PPU.OBJ[S].VFlip;
				bool isHFlipped = PPU.OBJ[S].HFlip;
				int objWidth = GFX.OBJWidths[S];

				while (Height > 0)
				{
					int BaseTile = (((StartLine<<1) + (PPU.OBJ[S].Name&0xf0))&0xf0) | (PPU.OBJ[S].Name&0x100) | (PPU.OBJ[S].Palette << 10);
					int TileX = PPU.OBJ[S].Name & 0x0f;
					int TileLine = (StartLine&7);
					int TileInc = 1;
					int TileHeight = 8 - TileLine;
					if (isVFlipped)
					{
						TileHeight = TileLine + 1;
						TileLine = 7 - TileLine;
						BaseTile |= V_FLIP;
					}
					
					if (TileHeight > Height)
						TileHeight = Height;

					if (isHFlipped)
					{
						TileX = (TileX + (objWidth >> 3) - 1) & 0x0f;
						BaseTile |= H_FLIP;
						TileInc = -1;
					}

					int X=PPU.OBJ[S].HPos;
					X = (X == -256) ? 256 : X;

					//if (!clipcount)
					{
						// No clipping at all.
						//
						for (; X<=256 && X<PPU.OBJ[S].HPos+objWidth; X += 8)
						{
							S9xDrawOBJTileHardware2 (sub, priorityOffset, BaseTile|TileX, X, Y, TileLine, TileHeight);
							TileX=(TileX+TileInc) & 0x0f;

						}
					}
					Height -= TileHeight;
					Y += TileHeight;

					if (isVFlipped)
					{
						StartLine -= TileHeight;
						if (StartLine < 0)
							StartLine += objWidth;
					}
					else
						StartLine += TileHeight;
				}
			}

			S = (S-1) & 0x7F;
		} while (S != StartDrawingSprite);
	}
	else
	{
		int priorityDepthOffset = depth + 768; // Pre-calculate (1 * 3 * 256 + depth)

		for(uint32 Y=GFX.StartY, Offset=Y*GFX.PPL; Y<=GFX.EndY; Y++, Offset+=GFX.PPL)
		{
			const auto& objLine = GFX.OBJLines[Y];

			int tiles=GFX.OBJLines[Y].Tiles;

			for (int I = objLine.OBJCount - 1; I >= 0; I --)
			{
				const auto& obj = GFX.OBJLines[Y].OBJ[I];

				int S = obj.Sprite;
				if (S < 0) continue;

				SOBJ ppuObj = PPU.OBJ[S];
				int BaseTile = (((obj.Line<<1) + (ppuObj.Name&0xf0))&0xf0) | (ppuObj.Name&0x100) | (ppuObj.Palette << 10);
				int TileX = ppuObj.Name & 0x0f;
				int TileLine = obj.Line & 7;
				bool isHFlipped = ppuObj.HFlip;
				int TileInc = isHFlipped ? -1 : 1;
				if (isHFlipped)
				{
					TileX = (TileX + (GFX.OBJWidths[S] >> 3) - 1) & 0x0f;
					BaseTile |= H_FLIP;
				}
		
				int X = (ppuObj.HPos == -256) ? 256 : ppuObj.HPos;
				int endX = X + GFX.OBJWidths[S];
		
				while (X <= 256 && X < endX)
				{
					S9xDrawOBJTileHardware2(sub, 
						priorityDepthOffset + ppuObj.Priority * 768, 
						BaseTile | TileX, X, Y, TileLine, 1);
		
					TileX = (TileX + TileInc) & 0x0f;
					X += 8;
				}
			}
		}
	}

	layerVerticesCount[LAYER_OBJ] = GPU3DS.vertices[VBO_SCENE].Count;

	if (layerVerticesCount[LAYER_OBJ] > 0)
		drawLayer(false, LAYER_OBJ, sub, SNES_TILE_CACHE, DEPTH_TEST_DISABLED, ALPHA_TEST_NE_ZERO);
}





//---------------------------------------------------------------------------
// Update one of the 256 mode 7 tiles with the latest texture.
// (uses a 256 color palette)
//---------------------------------------------------------------------------
void S9xPrepareMode7UpdateCharTile(int tileNumber)
{
	uint8 *charMap = &Memory.VRAM[1];	
	cache3dsCacheSnesTileToMode7TexturePosition( 
		&charMap[tileNumber * 128], GFX.ScreenColors, tileNumber, &IPPU.Mode7CharPaletteMask[tileNumber]); 
}


//---------------------------------------------------------------------------
// Update one of the 256 mode 7 tiles with the latest texture.
// (uses a 128 color palette)
//---------------------------------------------------------------------------
void S9xPrepareMode7ExtBGUpdateCharTile(int tileNumber)
{
	uint8 *charMap = &Memory.VRAM[1];	
	cache3dsCacheSnesTileToMode7TexturePosition( \
		&charMap[tileNumber * 128], GFX.ScreenColors128, tileNumber, &IPPU.Mode7CharPaletteMask[tileNumber]); \
}

//---------------------------------------------------------------------------
// Check to see if it is necessary to update the tile to the
// full texture.
//---------------------------------------------------------------------------
void S9xPrepareMode7CheckAndMarkPaletteChangedTiles()
{
	int charcount = 0;
	for (int c = 0; c < 256; c++)
	{
		if (IPPU.Mode7PaletteDirtyFlag & IPPU.Mode7CharPaletteMask[c])
		{
			//printf ("  chr %d, pal mask = %08x\n", c, IPPU.Mode7CharPaletteMask[c]);
			IPPU.Mode7CharDirtyFlag[c] = 2;
			IPPU.Mode7CharDirtyFlagCount = 1;
			charcount++;
		}
	}
	//printf ("M7pal: %08x chars:%d ", IPPU.Mode7PaletteDirtyFlag, charcount);
}


void S9xPrepareMode7CheckAndUpdateCharTiles()
{
	uint8 *tileMap = &Memory.VRAM[0];
	uint8 *charDirtyFlag = IPPU.Mode7CharDirtyFlag;

	int tilecount = 0;
	//register int tileNumber;
	int texturePos;
	int tileNumber;
	uint8 charFlag;

	#define CACHE_MODE7_TILE \
			tileNumber = tileMap[i * 2]; \
			charFlag = charDirtyFlag[tileNumber]; \
			if (charFlag) \
			{  \
				gpu3dsSetMode7TileModified(i, tileNumber); \
				tilecount++; \
				if (charFlag == 2) \
				{ \
					S9xPrepareMode7UpdateCharTile(tileNumber); \
					charDirtyFlag[tileNumber] = 1; \
				} \
			} \
			i++; 

	#define CACHE_MODE7_EXTBG_TILE \
			tileNumber = tileMap[i * 2]; \
			charFlag = charDirtyFlag[tileNumber]; \
			if (charFlag) \
			{  \
				gpu3dsSetMode7TileModified(i, tileNumber); \
				tilecount++; \
				if (charFlag == 2) \
				{ \
					S9xPrepareMode7ExtBGUpdateCharTile(tileNumber); \
					charDirtyFlag[tileNumber] = 1; \
				} \
			} \
			i++; 

	// Bug fix: The logic for the test was previously wrong.
	// This fixes some of the mode 7 tile problems in Secret of Mana.
	//
	//if (!Memory.FillRAM [0x2133] & 0x40)
	if (!IPPU.Mode7EXTBGFlag)
	{
		// Bug fix: Super Mario Kart Bowser Castle's tile 0 
		//
		if (PPU.Mode7Repeat == 3)
		{
			tileNumber = 0;
			charFlag = charDirtyFlag[tileNumber]; 
			if (charFlag == 2) 
			{ 
				S9xPrepareMode7UpdateCharTile(tileNumber); 
				charDirtyFlag[tileNumber] = 1; 
			}
		} 
		
		// Normal BG with 256 colours
		//
		for (int i = 0; i < 16384; )
		{
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
			
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE

			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE

			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
			CACHE_MODE7_TILE
		}
	}
	else
	{
		// Bug fix: Super Mario Kart Bowser Castle's tile 0 
		//
		if (PPU.Mode7Repeat == 3)
		{
			tileNumber = 0;
			charFlag = charDirtyFlag[tileNumber]; 
			if (charFlag == 2) 
			{ 
				S9xPrepareMode7ExtBGUpdateCharTile(tileNumber); 
				charDirtyFlag[tileNumber] = 1; 
			}
		} 
		
		// Prepare the 128 color palette by duplicate colors from 0-127 to 128-255
		//
		// Low priority (set the alpha to 0xe, and make use of the inprecise
		// floating point math to achieve the same alpha translucency)
		//
		for (int i = 0; i < 128; i++)
			GFX.ScreenColors128[i] = GFX.ScreenRGB555toRGBA4[GFX.ScreenColors[i]] & 0xfffe;		
		// High priority 	
		for (int i = 0; i < 128; i++)
			GFX.ScreenColors128[i + 128] = GFX.ScreenRGB555toRGBA4[GFX.ScreenColors[i]];		

		// Ext BG with 128 colours
		//
		for (int i = 0; i < 16384; )
		{
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE
			
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE

			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE

			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE
			CACHE_MODE7_EXTBG_TILE

		}	
	}

	//printf ("t:%d\n ", tilecount);
	
}


//---------------------------------------------------------------------------
// Check to see if it is necessary to update the full texture.
// There are 128x128 full texture tiles and we will have to go through
// them one by one to do it.
//---------------------------------------------------------------------------
void S9xPrepareMode7CheckAndUpdateFullTexture()
{
	gpu3dsSetMode7TexturesPixelFormat(IPPU.Mode7EXTBGFlag ? GPU_RGBA4 : GPU_RGBA5551);

	// we skip gpu3dsUpdateRenderStateIfChanged in S9xPrepareMode7CheckAndUpdateFullTexture()
	// instead we update our current render state + flags directly
	//
	// comparing with previous state (default) is redundant at this point
	// as the render properties have definitely changed
	
	GPU3DS.currentRenderState.target = TARGET_SNES_MODE7_FULL;
	GPU3DS.currentRenderState.textureBind = SNES_MODE7_TILE_CACHE;
	GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0;
	GPU3DS.currentRenderState.shader = SPROGRAM_MODE7;
	GPU3DS.currentRenderState.updateFrame = (u32)GPU3DSExt.mode7FrameCount;

	GPU3DS.currentRenderStateFlags |= FLAG_TARGET
	| FLAG_TEXTURE_BIND 
	| FLAG_TEXTURE_ENV
	| FLAG_SHADER
	| FLAG_UPDATE_FRAME;

	SVertexList *list = &GPU3DS.vertices[VBO_MODE7_TILE];
    
	gpu3dsApplyRenderState(&GPU3DS.currentRenderState);
    gpu3dsSetAttributeBuffers(&list->attrInfo, list->data);
    
	for (int section = 0; section < 4; section++)
	{
		if (GPU3DSExt.mode7SectionsModified[section])
		{
			gpu3dsSetRenderTargetToMode7Texture((3 - section) * 0x40000);
			gpu3dsDrawMode7Vertices(section * 4096, 4096);
			GPU3DSExt.mode7SectionsModified[section] = false;
		}
	}	    
	
	GPU3DSExt.mode7TilesModified = false;

	GPU3DS.currentRenderState.target = TARGET_SNES_MODE7_TILE_0;
	GPU3DS.currentRenderStateFlags |= FLAG_TARGET;
	
	gpu3dsApplyRenderState(&GPU3DS.currentRenderState);
	gpu3dsDrawMode7Vertices(16384, 4);

	// re-bind our tile shader
	GPU3DS.currentRenderState.shader = SPROGRAM_TILES;

	// we need to make sure textureOffset in tile shader isn't undefined
	// which seems to be the case, when re-binding the shader
	GPU3DS.currentRenderState.textureOffset = 0; 
	GPU3DS.currentRenderStateFlags |= FLAG_SHADER | FLAG_TEXTURE_OFFSET;
}

//---------------------------------------------------------------------------
// Prepare the Mode 7 texture. This will be done only once in a single
// frame.
//---------------------------------------------------------------------------
void S9xPrepareMode7()
{
	if (IPPU.Mode7Prepared)
		return;
		
	t3dsStartTiming(70, "PrepM7");
	
	IPPU.Mode7Prepared = 1;

	// Bug fix: Force mode 7 tiles to update.
	//
	if ((Memory.FillRAM [0x2133] & 0x40) != IPPU.Mode7EXTBGFlag)
	{
		IPPU.Mode7EXTBGFlag = (Memory.FillRAM [0x2133] & 0x40);
		IPPU.Mode7PaletteDirtyFlag = 0xffffffff;
	}

	// Prepare the palette
	//
    if (GFX.r2130 & 1) 
    { 
		if (IPPU.DirectColourMapsNeedRebuild)
		{ 
			S9xBuildDirectColourMaps ();
			IPPU.Mode7PaletteDirtyFlag = 0xffffffff;
		} 
		GFX.ScreenColors = DirectColourMaps [0]; 
    } 
    else 
	{
		GFX.ScreenColors = IPPU.ScreenColors;
	} 

	t3dsStartTiming(71, "PrepM7-Palette");

	// If any of the palette colours in a palette group have changed, 
	// then we must refresh all tiles having those colours in that group.
	//
	if (IPPU.Mode7PaletteDirtyFlag)
		S9xPrepareMode7CheckAndMarkPaletteChangedTiles();

	// If any of the characters are updated due to palette changes,
	// or due to change in the bitmaps, then cache the new characters and
	// update the entire map.
	//
	if (IPPU.Mode7CharDirtyFlagCount)
		S9xPrepareMode7CheckAndUpdateCharTiles();

	t3dsEndTiming(71);

	t3dsStartTiming(72, "PrepM7-FullTile");

	if (GPU3DSExt.mode7TilesModified)
		S9xPrepareMode7CheckAndUpdateFullTexture();
		
	t3dsEndTiming(72);

	t3dsStartTiming(73, "PrepM7-CharFlag");
	
	for (int i = 0; i < 256; )
	{
		uint8 f1, f2, f3, f4;

		// We are loading the flags this way to force GCC
		// to re-arrange instructions to avoid the 3-cycle latency.
		//
		#define UPDATE_CHAR_FLAG \
			f1 = IPPU.Mode7CharDirtyFlag[i];   \
			f2 = IPPU.Mode7CharDirtyFlag[i+1]; \
			f3 = IPPU.Mode7CharDirtyFlag[i+2]; \
			f4 = IPPU.Mode7CharDirtyFlag[i+3]; \
			if (f1 == 1) { IPPU.Mode7CharDirtyFlag[i] = 0; }   \
			if (f2 == 1) { IPPU.Mode7CharDirtyFlag[i+1] = 0; } \
			if (f3 == 1) { IPPU.Mode7CharDirtyFlag[i+2] = 0; } \
			if (f4 == 1) { IPPU.Mode7CharDirtyFlag[i+3] = 0; } \
			i += 4; 

		UPDATE_CHAR_FLAG
		UPDATE_CHAR_FLAG
		UPDATE_CHAR_FLAG
		UPDATE_CHAR_FLAG

	}
	IPPU.Mode7PaletteDirtyFlag = 0;
	IPPU.Mode7CharDirtyFlagCount = 0;	

    gpu3dsIncrementMode7UpdateFrameCount();
	
	t3dsEndTiming(73);
		
	t3dsEndTiming(70);
}


extern int adjustableValue;


#define CLIP_10_BIT_SIGNED(a) \
	((a) & ((1 << 10) - 1)) + (((((a) & (1 << 13)) ^ (1 << 13)) - (1 << 13)) >> 3)

//---------------------------------------------------------------------------
// Draws the Mode 7 background.
//---------------------------------------------------------------------------
void S9xDrawBackgroundMode7Hardware(int bg, bool8 sub, int depth, int alphaTestActive)
{
	t3dsStartTiming(27, "DrawBG0_M7");

	SGPU_ALPHA_TEST alphaTest;
	if (alphaTestActive == 0)
		alphaTest = ALPHA_TEST_NE_ZERO;
	else
		alphaTest = GFX.r2131 & 0x40 ? ALPHA_TEST_GTE_FULL : ALPHA_TEST_GTE_HALF;
	
	if (layerVerticesCount[bg] > 0)
	{
		drawLayer(true, bg, sub, SNES_MODE7_FULL, DEPTH_TEST_ENABLED, alphaTest);

		return;
	}

	for (int Y = GFX.StartY; Y <= GFX.EndY; Y++)
	{
		struct SLineMatrixData *p = &LineMatrixData [Y];

		int HOffset = ((int) LineData [Y].BG[0].HOffset << M7) >> M7; 
		int VOffset = ((int) LineData [Y].BG[0].VOffset << M7) >> M7; 
	
		int CentreX = ((int) p->CentreX << M7) >> M7; 
		int CentreY = ((int) p->CentreY << M7) >> M7; 

		int Left = 0;
		int Right = 256;
		int m7Left = Left;
		int m7Right = Right;

		// Bug fix: The mode 7 flipping problem.
		//
		int yy = Y;
		if (PPU.Mode7VFlip) 
			yy = 255 - Y; 
		if (PPU.Mode7HFlip) 
		{
			m7Left = 255 - m7Left;
			m7Right = 255 - m7Right;
		}

		// Bug fix: Used the original CLIP_10_BIT_SIGNED from Snes9x
		// This fixes the intro for Super Chase HQ.
		yy = yy + CLIP_10_BIT_SIGNED(VOffset - CentreY);
		int xx0 = m7Left + CLIP_10_BIT_SIGNED(HOffset - CentreX);
		int xx1 = m7Right + CLIP_10_BIT_SIGNED(HOffset - CentreX);

		int BB = p->MatrixB * yy + (CentreX << 8); 
		int DD = p->MatrixD * yy + (CentreY << 8); 

		int AA0 = p->MatrixA * xx0; 
		int CC0 = p->MatrixC * xx0; 
		int AA1 = p->MatrixA * xx1; 
		int CC1 = p->MatrixC * xx1; 

        // Use fixed-point arithmetic with rounding
        int tx0 = (AA0 + BB + M7_LINE_ROUNDING_OFFSET) >> 8;
        int ty0 = (CC0 + DD + M7_LINE_ROUNDING_OFFSET) >> 8;
        int tx1 = (AA1 + BB + M7_LINE_ROUNDING_OFFSET) >> 8;
        int ty1 = (CC1 + DD + M7_LINE_ROUNDING_OFFSET) >> 8;

		// using -16384 for the geometry shader to detect detect mode 7
		gpu3dsAddMode7LineVertexes(Left, Y+depth, Right, -16384, tx0, ty0, tx1, ty1);
	}

	layerVerticesCount[bg] = GPU3DS.vertices[VBO_SCENE].Count;

	if (layerVerticesCount[bg] > 0)
		drawLayer(false, bg, sub, SNES_MODE7_FULL, DEPTH_TEST_ENABLED, alphaTest);

	t3dsEndTiming(27);
}

//---------------------------------------------------------------------------
// Draws the Mode 7 background (with repeat tile0)
//---------------------------------------------------------------------------
void S9xDrawBackgroundMode7HardwareRepeatTile0(int bg, bool8 sub, int depth)
{
	t3dsStartTiming(27, "DrawBG0_M7");
	
	bool verticesUpdated = false;
	
	for (int Y = GFX.StartY; Y <= GFX.EndY; Y++)
	{
		struct SLineMatrixData *p = &LineMatrixData [Y];

		int HOffset = ((int) LineData [Y].BG[0].HOffset << M7) >> M7; 
		int VOffset = ((int) LineData [Y].BG[0].VOffset << M7) >> M7; 
	
		int CentreX = ((int) p->CentreX << M7) >> M7; 
		int CentreY = ((int) p->CentreY << M7) >> M7; 
		
		uint32 Left = 0;
		uint32 Right = 256;
 
		int yy = Y;

		// Bug fix: Used the original CLIP_10_BIT_SIGNED from Snes9x
		// This fixes the intro for Super Chase HQ.
		yy = yy + CLIP_10_BIT_SIGNED(VOffset - CentreY);
		int xx0 = Left + CLIP_10_BIT_SIGNED(HOffset - CentreX);
		int xx1 = Right + CLIP_10_BIT_SIGNED(HOffset - CentreX);

		int BB = p->MatrixB * yy + (CentreX << 8); 
		int DD = p->MatrixD * yy + (CentreY << 8); 

		int AA0 = p->MatrixA * xx0; 
		int CC0 = p->MatrixC * xx0; 
		int AA1 = p->MatrixA * xx1; 
		int CC1 = p->MatrixC * xx1; 
		
        // Use fixed-point arithmetic with rounding
        int tx0 = (AA0 + BB + M7_LINE_ROUNDING_OFFSET) >> 8;
        int ty0 = (CC0 + DD + M7_LINE_ROUNDING_OFFSET) >> 8;
        int tx1 = (AA1 + BB + M7_LINE_ROUNDING_OFFSET) >> 8;
        int ty1 = (CC1 + DD + M7_LINE_ROUNDING_OFFSET) >> 8;

		// This is used for repeating tile 0.
		// So the texture is completely within the 0-1024 boundary,
		// the tile 0 will not show up anyway, so we will skip drawing 
		// tile 0.
		//
		bool withinTexture = true;
		if (tx0 < 0 || tx0 > 1024) withinTexture = false;
		else if (ty0 < 0 || ty0 > 1024) withinTexture = false;
		else if (tx1 < 0 || tx1 > 1024) withinTexture = false;
		else if (ty1 < 0 || ty1 > 1024) withinTexture = false;

		if (!withinTexture)
		{
			// using -16384 for the geometry shader to detect detect mode 7
			gpu3dsAddMode7LineVertexes(Left, Y+depth, Right, -16384, tx0, ty0, tx1, ty1);

			verticesUpdated = true;
		}
	}

	if (verticesUpdated)
		drawLayer(false, bg, sub, SNES_MODE7_TILE_0, DEPTH_TEST_ENABLED, ALPHA_TEST_NE_ZERO);
	
	t3dsEndTiming(27);
}


//---------------------------------------------------------------------------
// Renders the screen from GFX.StartY to GFX.EndY
//---------------------------------------------------------------------------

void S9xRenderScreenHardware (bool8 sub)
{
	GFX.pCurrentClip = &IPPU.Clip[sub ? 1 : 0];

	bool8 bgEnabled[5];
	
	int bgAlpha[6]; // including backdrop alpha
	bool isMode5or6 = PPU.BGMode == 5 || PPU.BGMode == 6;

    if (!isMode5or6 && !GFX.Pseudo) {
        int alpha = (GFX.r2131 & 0x40) ? ALPHA_0_5 : ALPHA_1_0;
        
        for (int i = 0; i < 6; i++) {
            bgAlpha[i] = SUB_OR_ADD(i) ? alpha : ALPHA_ZERO;
        }
    } else {
        for (int i = 0; i < 6; i++) {
            bgAlpha[i] = ALPHA_0_5;
        }
    }

    if (!sub) {
        for (int i = 0; i < 5; i++) {
			// also set bgEnabled[i] to false if the previous subscreen call resulted in zero tiles
            bgEnabled[i] = ON_MAIN(i) && layerVerticesCount[i] != 0;
        }
    } else {
        if (!isMode5or6) {
            for (int i = 0; i < 5; i++) {
                bgEnabled[i] = GFX.Pseudo ? ON_SUB_PSEUDO(i) : ON_SUB(i);
            }
        } else {
            for (int i = 0; i < 5; i++) {
                bgEnabled[i] = ON_SUB_HIRES(i);
            }
        }
    }

	#define DRAW_4COLOR_BG_INLINE(bg, p, d0, d1) \
		if (bgEnabled[bg]) \
			S9xDrawBackgroundHardwarePriority0Inline_4Color (PPU.BGMode, bg, sub, d0 * 256 + bgAlpha[bg], d1 * 256 + bgAlpha[bg]); \

	#define DRAW_16COLOR_BG_INLINE(bg, p, d0, d1) \
		if (bgEnabled[bg]) \
			S9xDrawBackgroundHardwarePriority0Inline_16Color (PPU.BGMode, bg, sub, d0 * 256 + bgAlpha[bg], d1 * 256 + bgAlpha[bg]); \

	#define DRAW_256COLOR_BG_INLINE(bg, p, d0, d1) \
		if (bgEnabled[bg]) \
			S9xDrawBackgroundHardwarePriority0Inline_256Color (PPU.BGMode, bg, sub, d0 * 256 + bgAlpha[bg], d1 * 256 + bgAlpha[bg]); \

	#define DRAW_4COLOR_OFFSET_BG_INLINE(bg, p, d0, d1) \
		if (bgEnabled[bg]) \
			S9xDrawOffsetBackgroundHardwarePriority0Inline_4Color (PPU.BGMode, bg, sub, d0 * 256 + bgAlpha[bg], d1 * 256 + bgAlpha[bg]); \

	#define DRAW_16COLOR_OFFSET_BG_INLINE(bg, p, d0, d1) \
		if (bgEnabled[bg]) \
			S9xDrawOffsetBackgroundHardwarePriority0Inline_16Color (PPU.BGMode, bg, sub, d0 * 256 + bgAlpha[bg], d1 * 256 + bgAlpha[bg]); \

	#define DRAW_256COLOR_OFFSET_BG_INLINE(bg, p, d0, d1) \
		if (bgEnabled[bg]) \
			S9xDrawOffsetBackgroundHardwarePriority0Inline_256Color (PPU.BGMode, bg, sub, d0 * 256 + bgAlpha[bg], d1 * 256 + bgAlpha[bg]); \

	#define DRAW_4COLOR_HIRES_BG_INLINE(bg, p, d0, d1) \
		if (bgEnabled[bg]) \
			S9xDrawHiresBackgroundHardwarePriority0Inline_4Color (PPU.BGMode, bg, sub, d0 * 256 + bgAlpha[bg], d1 * 256 + bgAlpha[bg]); \

	#define DRAW_16COLOR_HIRES_BG_INLINE(bg, p, d0, d1) \
		if (bgEnabled[bg]) \
			S9xDrawHiresBackgroundHardwarePriority0Inline_16Color (PPU.BGMode, bg, sub, d0 * 256 + bgAlpha[bg], d1 * 256 + bgAlpha[bg]); \		


	S9xDrawBackdropHardware(!isMode5or6 ? sub : false, bgAlpha[LAYER_BACKDROP]);
	
	if (bgEnabled[LAYER_OBJ]) {
		S9xDrawOBJSHardware(sub, bgAlpha[LAYER_OBJ], 0);
	}

    switch (PPU.BGMode) {
        case 0:
			DRAW_4COLOR_BG_INLINE(0, 0, 8, 11);
			DRAW_4COLOR_BG_INLINE(1, 0, 7, 10);
			DRAW_4COLOR_BG_INLINE(2, 0, 2, 5);
			DRAW_4COLOR_BG_INLINE(3, 0, 1, 4);

            break;
		case 1:
			DRAW_16COLOR_BG_INLINE(0, 0, 8, 11);
			DRAW_16COLOR_BG_INLINE(1, 0, 7, 10);
			DRAW_4COLOR_BG_INLINE(2, 0, 2, (PPU.BG3Priority ? 13 : 5));

			break;
        case 2:
			DRAW_16COLOR_OFFSET_BG_INLINE(0, 0, 5, 11);
			DRAW_16COLOR_OFFSET_BG_INLINE(1, 0, 2, 8);
            break;
        case 3:
			DRAW_256COLOR_BG_INLINE(0, 0, 5, 11);
			DRAW_16COLOR_BG_INLINE(1, 0, 2, 8);
            break;
        case 4:
			DRAW_256COLOR_OFFSET_BG_INLINE(0, 0, 5, 11);
			DRAW_4COLOR_OFFSET_BG_INLINE(1, 0, 2, 8);
            break;
        case 5:
			renderState.textureOffset = sub ? 0 : 1;
			gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TEXTURE_OFFSET, &renderState);
			DRAW_16COLOR_HIRES_BG_INLINE(0, 0, 5, 11);
			DRAW_4COLOR_HIRES_BG_INLINE(1, 0, 2, 8);
            break;
        case 6:
			renderState.textureOffset = sub ? 0 : 1;
			gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TEXTURE_OFFSET, &renderState);
			DRAW_16COLOR_OFFSET_BG_INLINE(0, 0, 5, 11);
            break;

        case 7:
			#define DRAW_M7BG(bg, d, alphaTestActive, tile0) \
				if (bgEnabled[bg]) \
				{ \
					int depth = bgAlpha[bg] + d*256; \
					if (tile0) \
						S9xDrawBackgroundMode7HardwareRepeatTile0(bg, sub, depth); \
					S9xDrawBackgroundMode7Hardware(bg, sub, depth, alphaTestActive); \
				} \

			bool isTile0 = PPU.Mode7Repeat != 0 && PPU.Mode7Repeat != 2;
            if (IPPU.Mode7EXTBGFlag) {
                DRAW_M7BG(1, 2, 0, isTile0);
                DRAW_M7BG(1, 8, 1, isTile0);
                DRAW_M7BG(0, 5, 0, isTile0);
            } else {
                DRAW_M7BG(0, 5, 0, isTile0);
            }
        
			break;
    }
}

// ********************************************************************************************

//-----------------------------------------------------------
// Render color math.
//-----------------------------------------------------------
inline bool S9xRenderColorMath()
{
	bool modeHiRes = PPU.BGMode == 5 || PPU.BGMode == 6 || GFX.Pseudo;
	bool modeSub = (GFX.r2130 & 2) && (ANYTHING_ON_SUB || ADD_OR_SUB_ON_ANYTHING);
	bool colorMathEnabled = modeHiRes || modeSub;
	
	if (colorMathEnabled)
	{
		gpu3dsAddTileVertexes(0, GFX.StartY, 256, GFX.EndY + 1,
			0, GFX.StartY, 256, GFX.EndY + 1, 0);

		renderState.textureBind = SNES_SUB;
		renderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0;
	}
	else
	{
		for (int i = 0; i < IPPU.FixedColorSections.Count; i++)
		{
			uint32 fixedColour = IPPU.FixedColorSections.Section[i].Value;

			if (fixedColour != 0xff)
			{
				gpu3dsAddRectangleVertexes(
					0, IPPU.FixedColorSections.Section[i].StartY, 
					256, IPPU.FixedColorSections.Section[i].EndY + 1, fixedColour);
				
				colorMathEnabled = true;
			}
		}

		if (!colorMathEnabled)
			return false;

		renderState.textureEnv = TEX_ENV_REPLACE_COLOR;
	}
	
	renderState.depthTest = DEPTH_TEST_DISABLED;
	renderState.alphaTest = ALPHA_TEST_NE_ZERO;
	
	// set blending mode
	//
	// For hi-res modes, we will always do add / 2 blending
	// NOTE: This is not the SNES doing any blending, but
	// we are actually emulating the TV doing the blending 
	// of both main/sub screens!
	if (modeHiRes)
		renderState.alphaBlending = ALPHA_BLENDING_ADD_DIV2;
	else if (GFX.r2131 & 0x80) 
	{
		// We have to render the subscreen as long either of the
		// 212D and 2131 registers are set for any BGs.
		// This fixes Zelda's prologue's where the room is supposed to
		// be dark.
		if (GFX.r2131 & 0x40)
			renderState.alphaBlending = ALPHA_BLENDING_SUB_DIV2;
		else
			renderState.alphaBlending = ALPHA_BLENDING_SUB;
	} 
	else 
	{
		if (GFX.r2131 & 0x40)
			renderState.alphaBlending = ALPHA_BLENDING_ADD_DIV2;			
		else
			renderState.alphaBlending = ALPHA_BLENDING_ADD;
	}

	u32 propertyFlags = FLAG_TEXTURE_BIND 
	| FLAG_TEXTURE_ENV 
	| FLAG_DEPTH_TEST
	| FLAG_ALPHA_TEST
	| FLAG_ALPHA_BLENDING;

	gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, propertyFlags, &renderState);

	return true;
}

inline void S9xRenderClipToBlackAndColorMath()
{
	t3dsStartTiming(29, "Colormath");

	u32 stencilValue;

	if ((GFX.r2130 & 0xc0) != 0)
	{
		// Clip to main screen to black before color math
		//
		stencilValue = S9xComputeAndEnableStencilFunction(LAYER_BACKDROP, 0);
		if (IPPU.WindowingEnabled || stencilValue != STENCIL_TEST_ENABLED_WINDOWING_DISABLED)
		{
			gpu3dsAddRectangleVertexes(
				0, GFX.StartY, 256, GFX.EndY + 1, 0xff);

			renderState.textureEnv = TEX_ENV_REPLACE_COLOR;
			renderState.stencilTest = stencilValue;
			renderState.alphaTest = ALPHA_TEST_DISABLED;
			renderState.alphaBlending = ALPHA_BLENDING_KEEP_DEST_ALPHA;

			u32 propertyFlags = FLAG_TEXTURE_ENV 
			| FLAG_STENCIL_TEST 
			| FLAG_ALPHA_TEST
			| FLAG_ALPHA_BLENDING;

			gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, propertyFlags, &renderState);
			gpu3dsDrawVertexList(&GPU3DS.vertices[VBO_SCENE]);
		}
	}

	if ((GFX.r2130 & 0x30) != 0x30 || PPU.BGMode == 5 || PPU.BGMode == 6 || GFX.Pseudo)
	{
		// Do actual color math
		//
		stencilValue = S9xComputeAndEnableStencilFunction(LAYER_BACKDROP, 1);

		if (IPPU.WindowingEnabled || stencilValue != STENCIL_TEST_ENABLED_WINDOWING_DISABLED)
		{
			if (S9xRenderColorMath())
			{
				renderState.stencilTest = stencilValue;

				gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_STENCIL_TEST, &renderState);
				gpu3dsDrawVertexList(&GPU3DS.vertices[VBO_SCENE]);
			}
		}
	}

	t3dsEndTiming(29);
}

//-----------------------------------------------------------
// Render brightness / forced blanking.
// Improves performance slightly.
//-----------------------------------------------------------
void S9xRenderBrightness(VerticalSections *verticalSections)
{
	if (!verticalSections->Count) 
		return;
	
	for (int i = 0; i < verticalSections->Count; i++)
	{
		int32 alpha = 0xF - verticalSections->Section[i].Value;
		alpha |= alpha << 4;

		gpu3dsAddRectangleVertexes(
			0, verticalSections->Section[i].StartY, 
			256, verticalSections->Section[i].EndY + 1, alpha);
	}

	renderState.target = TARGET_SNES_MAIN;
	renderState.textureEnv = TEX_ENV_REPLACE_COLOR;
	renderState.stencilTest = STENCIL_TEST_DISABLED;
	renderState.depthTest = DEPTH_TEST_DISABLED;
	renderState.alphaTest = ALPHA_TEST_DISABLED;
	renderState.alphaBlending = ALPHA_BLENDING_ENABLED;

	u32 propertyFlags = FLAG_TARGET 
	| FLAG_TEXTURE_ENV
	| FLAG_STENCIL_TEST
	| FLAG_DEPTH_TEST
	| FLAG_ALPHA_TEST
	| FLAG_ALPHA_BLENDING;

	gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, propertyFlags, &renderState);
	gpu3dsDrawVertexList(&GPU3DS.vertices[VBO_SCENE]);
}

//-----------------------------------------------------------
// Draws the windows on the stencils.
//-----------------------------------------------------------
void S9xDrawStencilForWindows(VerticalSections *verticalSections)
{
	if (!verticalSections->Count) 
		return;

	t3dsStartTiming(30, "DrawWindowStencils");

	int stencilEndX[10];
	int stencilMask[10];
	
	for (int i = 0; i < verticalSections->Count; i++)
	{
		int startY = verticalSections->Section[i].StartY;
		int endY = verticalSections->Section[i].EndY;

		int w1Left = verticalSections->Section[i].V1;
		int w1Right = verticalSections->Section[i].V2;
		int w2Left = verticalSections->Section[i].V3;
		int w2Right = verticalSections->Section[i].V4;

		ComputeClipWindowsForStenciling (w1Left, w1Right, w2Left, w2Right, stencilEndX, stencilMask);

		int startX = 0;
		for (int s = 0; s < 10; s++)
		{
			int endX = stencilEndX[s];
			int mask = stencilMask[s];
			gpu3dsAddRectangleVertexes(startX, startY, endX, endY + 1, (mask << 29));	
			

			startX = endX;
			if (startX >= 256)
				break;
		}
	}

	renderState.target = TARGET_SNES_DEPTH;
	renderState.textureEnv = TEX_ENV_REPLACE_COLOR;

	gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TARGET | FLAG_TEXTURE_ENV, &renderState);
	gpu3dsDrawVertexList(&GPU3DS.vertices[VBO_SCENE]);
	
	t3dsEndTiming(30);
}



// returns false when GFX.StartY-GFX.EndY has brightness = 0 (hidden)
//
// updates GFX.StartY and/or GFX.EndY when section is partially hidden
// (e.g. y0-y1 = 100-200 with brightness section 170-200 = 0 becomes y0-y1 = 100-169)
bool checkForVisibleSection(VerticalSection *brightnessSection) {
	int y0 = GFX.StartY;
	int y1 = GFX.EndY;

	int by0 = brightnessSection->StartY;
	int by1 = brightnessSection->EndY;
	bool isVisible = brightnessSection->Value != 0;

	// most likely
	if (isVisible) {
		return true;
	}
	
	if (y0 >= by0 && y1 <= by1) {
		return false;
	}

    if (by0 <= y0 && by1 >= y0) {
        y0 = by1 + 1;
    }

    if (by0 <= y1 && by1 >= y1) {
        y1 = by0 - 1;
    }

    if (y0 <= y1) {
        GFX.StartY = y0;
        GFX.EndY = y1;
    }

	return true;
}


//-----------------------------------------------------------
// Updates the screen using the 3D hardware.
//-----------------------------------------------------------

void S9xUpdateScreenHardware ()
{	
	t3dsStartTiming(11, "S9xUpdateScreen");

    GFX.S = GFX.Screen;
    GFX.r2131 = Memory.FillRAM [0x2131];
    GFX.r212c = Memory.FillRAM [0x212c];
    GFX.r212d = Memory.FillRAM [0x212d];
    GFX.r2130 = Memory.FillRAM [0x2130];

	// Fixed pseudo hi-res (Kirby Dreamland 3)
    GFX.Pseudo = (Memory.FillRAM [0x2133] & 8) != 0 &&
				 (GFX.r212c & 15) != (GFX.r212d & 15) &&
				 (GFX.r2131 & 0x3f) == 0;

    GFX.StartY = IPPU.PreviousLine;
	GFX.EndY = IPPU.CurrentLine - 1;
    IPPU.PreviousLine = IPPU.CurrentLine;

	layerVerticesCount[LAYER_BG0] = -1;
	layerVerticesCount[LAYER_BG1] = -1;
	layerVerticesCount[LAYER_BG2] = -1;
	layerVerticesCount[LAYER_BG3] = -1;
	layerVerticesCount[LAYER_OBJ] = -1;

	bool isLastLine = GFX.EndY >= PPU.ScreenHeight - 1;
    if (isLastLine)
		GFX.EndY = PPU.ScreenHeight - 1;

    if (IPPU.OBJChanged)
		S9xSetupOBJ ();

	VerticalSections *brightnessSections = &IPPU.BrightnessSections;	
	S9xCommitVerticalSection2(brightnessSections, brightnessSections->CurrentValue != 0xF);

	bool RenderThisSection = true;

	if (brightnessSections->Count) {
		// safe to use? if this leads to hidden/broken areas, leave RenderThisSection at `true`
		RenderThisSection = checkForVisibleSection(&brightnessSections->Section[brightnessSections->Count - 1]);
	}
	
	// XXX: Check ForceBlank? Or anything else?
	PPU.RangeTimeOver |= GFX.OBJLines[GFX.EndY].RTOFlags;

	// set render state to default
	renderState = GPU3DS.currentRenderState;
	renderState.textureEnv = TEX_ENV_REPLACE_COLOR;
	renderState.stencilTest = STENCIL_TEST_DISABLED;
	renderState.depthTest = DEPTH_TEST_DISABLED;
	renderState.alphaTest = ALPHA_TEST_DISABLED;
	renderState.alphaBlending = ALPHA_BLENDING_DISABLED;

	u32 propertyFlags = FLAG_TEXTURE_ENV 
	| FLAG_STENCIL_TEST
	| FLAG_DEPTH_TEST 
	| FLAG_ALPHA_TEST
	| FLAG_ALPHA_BLENDING;
	
	gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, propertyFlags, &renderState);

	if (PPU.BGMode == 7)
	{
		S9xPrepareMode7();
	}
	
	if (RenderThisSection)
	{
		
		// Vertical sections
		// We commit the current values to create a new section up
		// till the current rendered line - 1.
		//
		S9xCommitVerticalSection(&IPPU.BackdropColorSections);
		S9xCommitVerticalSection(&IPPU.FixedColorSections);
		S9xCommitVerticalSection(&IPPU.WindowLRSections);

		
		// If none of the windows are enabled, we are not going to draw the current section in IPPU.WindowLRSections
		//
		uint8 windowEnableMask = Memory.FillRAM[0x212e] | Memory.FillRAM[0x212f] | 0x20;

		IPPU.WindowingEnabled = false;

		for (int layer = 0; layer < 6; layer++)
		{

			if ((PPU.ClipWindow1Enable[layer] || PPU.ClipWindow2Enable[layer]) && 
				((windowEnableMask >> layer) & 1) )
			{
				IPPU.WindowingEnabled = true;

				break;
			}
		}

		if (IPPU.WindowingEnabled)
			S9xDrawStencilForWindows(&IPPU.WindowLRSections);

		// Bug fix: We have to render as long as 
		// the 2130 register says that we have are
		// doing color math using the subscreen 
		// (instead of the fixed color)
		//
		// This is because the backdrop color will be
		// used for the color math.
		//
		//printf ("Render Y:%d-%d M%d\n", GFX.StartY, GFX.EndY, PPU.BGMode);
		if (ANYTHING_ON_SUB || (GFX.r2130 & 2) || PPU.BGMode == 5 || PPU.BGMode == 6 || GFX.Pseudo)
		{
			renderState.target = TARGET_SNES_SUB;
			gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TARGET, &renderState);
			S9xRenderScreenHardware (TRUE);	
		}
		
		// Render the main screen.
		//
		renderState.target = TARGET_SNES_MAIN;
		gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TARGET, &renderState);
		S9xRenderScreenHardware (FALSE);

		S9xRenderClipToBlackAndColorMath();

		S9xResetVerticalSection(&IPPU.BackdropColorSections);
		S9xResetVerticalSection(&IPPU.FixedColorSections);
		S9xResetVerticalSection(&IPPU.WindowLRSections);
	}	

	if (isLastLine) {
		S9xRenderBrightness(&IPPU.BrightnessSections);
	}

	t3dsEndTiming(11);
}
