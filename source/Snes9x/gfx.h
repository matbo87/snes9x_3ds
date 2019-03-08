#include "copyright.h"


#ifndef _GFX_H_
#define _GFX_H_

#include "port.h"
#include "snes9x.h"


struct SGFX{
    // Initialize these variables
    uint8  *Screen;
    uint8  *SubScreen;
    uint8  *ZBuffer;
    uint8  *SubZBuffer;
    uint32 Pitch;

    // Setup in call to S9xGraphicsInit()
    int   Delta;
    uint16 *X2;
    uint16 *ZERO_OR_X2;
    uint16 *ZERO;
    uint32 RealPitch; // True pitch of Screen buffer.
    uint32 Pitch2;    // Same as RealPitch except while using speed up hack for Glide.
    uint32 ZPitch;    // Pitch of ZBuffer
    uint32 PPL;	      // Number of pixels on each of Screen buffer
    uint32 PPLx2;
    uint32 PixSize;
    uint8  *S;
    uint8  *DB;
    uint16 *ScreenColors;
    uint32 DepthDelta;
    uint8  Z1;          // Depth for comparison
    uint8  Z2;          // Depth to save
    uint8  ZSprite;     // Used to ensure only 1st sprite is drawn per pixel
    uint32 FixedColour;
    uint32 StartY;
    uint32 EndY;
    struct ClipData *pCurrentClip;
    uint32 Mode7Mask;
    uint32 Mode7PriorityMask;
    uint8  OBJWidths[128];
    uint8  OBJVisibleTiles[128];
    struct {
        uint8 RTOFlags;
        int16 Tiles;
        struct {
            int8 Sprite;
            uint8 Line;
        } OBJ[32];
        int OBJCount;
    } OBJLines [SNES_HEIGHT_EXTENDED];

    uint8  r212c;
    uint8  r212d;
    uint8  r2130;
    uint8  r2131;
    bool8  Pseudo;
    
#ifdef GFX_MULTI_FORMAT
    uint32 PixelFormat;
    uint32 (*BuildPixel) (uint32 R, uint32 G, uint32 B);
    uint32 (*BuildPixel2) (uint32 R, uint32 G, uint32 B);
    void   (*DecomposePixel) (uint32 Pixel, uint32 &R, uint32 &G, uint32 &B);
#endif

    uint8  PreviousFrameBrightness = 0;
    
    // Screen colors for Mode 7 Ext BG
    //
    uint16 ScreenRGB555toRGBA4[0x10000];
    uint16 ScreenColors128[256]; 

    // Tracks when the palette was last modified.
    //
    uint32 PaletteFrame256[1] = { 1 };    
    uint32 PaletteFrame[16] = { 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1 };    // 16-color BGs / OBJs
    uint32 PaletteFrame4BG[4][16] = 
        { 
            { 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1 }, 
            { 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1 }, 
            { 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1 }, 
            { 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1 } 
        };   // Mode 1 4-color BGs
    
    // Memory Usage = 0.50 MB    for the frame the palette was changed for the given Tile.
    //
    uint32 VRAMPaletteFrame[8192][16];
    bool   Use3DSHardware = true;

};

struct SLineData {
    struct {
	uint16 VOffset;
	uint16 HOffset;
    } BG [4];

    uint8   FixedColour[4];             // Sub Screen

};

#define H_FLIP 0x4000
#define V_FLIP 0x8000
#define BLANK_TILE 2

struct SBG
{
    uint32 TileSize;
    uint32 BitShift;
    uint32 TileShift;
    uint32 TileAddress;
    uint32 NameSelect;
    uint32 SCBase;

    uint32 StartPalette;
    uint32 PaletteShift;
    uint32 PaletteMask;
    
    uint8 *Buffer;
    uint8 *Buffered;
    bool8  DirectColourMode;

    
    int    Depth;
    
    //bool8 TileFull[4096];
    
    //uint16 BufferTile16Bit[1][1];
    
    /*
    // Memory Usage = 0.5 MB for background parameters
    //
    uint16 DrawTileParameters[4][8192][8];
    uint16 DrawTileCount[4];

    // Memory Usage = 3.0 MB for sprite parameters    
    int32 DrawOBJTileLaterParameters[65536][8];
    uint16 DrawOBJTileLaterParametersCount;
    uint16 DrawOBJTileLaterIndex[4][65536];
    uint16 DrawOBJTileLaterIndexCount[4];
    */

};


struct SLineMatrixData
{
    short MatrixA;
    short MatrixB;
    short MatrixC;
    short MatrixD;
    short CentreX;
    short CentreY;
};

extern uint32 odd_high [4][16];
extern uint32 odd_low [4][16];
extern uint32 even_high [4][16];
extern uint32 even_low [4][16];
extern SBG BG;
extern uint16 DirectColourMaps [8][256];

extern uint8 add32_32 [32][32];
extern uint8 add32_32_half [32][32];
extern uint8 sub32_32 [32][32];
extern uint8 sub32_32_half [32][32];
extern uint8 mul_brightness [16][32];

// Could use BSWAP instruction on Intel port...
#define SWAP_DWORD(dw) dw = ((dw & 0xff) << 24) | ((dw & 0xff00) << 8) | \
		            ((dw & 0xff0000) >> 8) | ((dw & 0xff000000) >> 24)

#ifdef FAST_LSB_WORD_ACCESS
#define READ_2BYTES(s) (*(uint16 *) (s))
#define WRITE_2BYTES(s, d) *(uint16 *) (s) = (d)
#else
#ifdef LSB_FIRST
#define READ_2BYTES(s) (*(uint8 *) (s) | (*((uint8 *) (s) + 1) << 8))
#define WRITE_2BYTES(s, d) *(uint8 *) (s) = (d), \
			   *((uint8 *) (s) + 1) = (d) >> 8
#else  // else MSB_FISRT
#define READ_2BYTES(s) (*(uint8 *) (s) | (*((uint8 *) (s) + 1) << 8))
#define WRITE_2BYTES(s, d) *(uint8 *) (s) = (d), \
			   *((uint8 *) (s) + 1) = (d) >> 8
#endif // LSB_FIRST
#endif // i386

#define SUB_SCREEN_DEPTH 0
#define MAIN_SCREEN_DEPTH 32

#define OLD_COLOUR_BLENDING

#if defined(OLD_COLOUR_BLENDING)
#define COLOR_ADD(C1, C2) \
GFX.X2 [((((C1) & RGB_REMOVE_LOW_BITS_MASK) + \
	  ((C2) & RGB_REMOVE_LOW_BITS_MASK)) >> 1)]
#else
#define COLOR_ADD(C1, C2) \
(GFX.X2 [((((C1) & RGB_REMOVE_LOW_BITS_MASK) + \
	  ((C2) & RGB_REMOVE_LOW_BITS_MASK)) >> 1) + \
	 ((C1) & (C2) & RGB_LOW_BITS_MASK)] | \
 (((C1) ^ (C2)) & RGB_LOW_BITS_MASK))	   
#endif

#define COLOR_ADD1_2(C1, C2) \
(((((C1) & RGB_REMOVE_LOW_BITS_MASK) + \
          ((C2) & RGB_REMOVE_LOW_BITS_MASK)) >> 1) | ALPHA_BITS_MASK)

#if defined(OLD_COLOUR_BLENDING)
#define COLOR_SUB(C1, C2) \
GFX.ZERO_OR_X2 [(((C1) | RGB_HI_BITS_MASKx2) - \
		 ((C2) & RGB_REMOVE_LOW_BITS_MASK)) >> 1]
#elif !defined(NEW_COLOUR_BLENDING)
#define COLOR_SUB(C1, C2) \
(GFX.ZERO_OR_X2 [(((C1) | RGB_HI_BITS_MASKx2) - \
                  ((C2) & RGB_REMOVE_LOW_BITS_MASK)) >> 1] + \
((C1) & RGB_LOW_BITS_MASK) - ((C2) & RGB_LOW_BITS_MASK))
#else
inline uint16 COLOR_SUB(uint16, uint16);

inline uint16 COLOR_SUB(uint16 C1, uint16 C2)
{
	uint16	mC1, mC2, v = 0;

	mC1 = C1 & FIRST_COLOR_MASK;
	mC2 = C2 & FIRST_COLOR_MASK;
	if (mC1 > mC2) v += (mC1 - mC2);
	
	mC1 = C1 & SECOND_COLOR_MASK;
	mC2 = C2 & SECOND_COLOR_MASK;
	if (mC1 > mC2) v += (mC1 - mC2);

	mC1 = C1 & THIRD_COLOR_MASK;
	mC2 = C2 & THIRD_COLOR_MASK;
	if (mC1 > mC2) v += (mC1 - mC2);
	
	return v;
}
#endif

#define COLOR_SUB1_2(C1, C2) \
GFX.ZERO [(((C1) | RGB_HI_BITS_MASKx2) - \
	   ((C2) & RGB_REMOVE_LOW_BITS_MASK)) >> 1]

typedef void (*NormalTileRenderer) (uint32 Tile, uint32 Offset, 
				    uint32 StartLine, uint32 LineCount);
typedef void (*ClippedTileRenderer) (uint32 Tile, uint32 Offset,
				     uint32 StartPixel, uint32 Width,
				     uint32 StartLine, uint32 LineCount);
typedef void (*LargePixelRenderer) (uint32 Tile, uint32 Offset,
				    uint32 StartPixel, uint32 Pixels,
				    uint32 StartLine, uint32 LineCount);

START_EXTERN_C
void S9xStartScreenRefresh ();
void S9xDrawScanLine (uint8 Line);
void S9xEndScreenRefresh ();
void S9xSetupOBJ ();
void S9xUpdateScreenSoftware ();
void S9xUpdateScreenHardware ();
void RenderLine (uint8 line);
void S9xBuildDirectColourMaps ();
void S9xUpdatePalettes();

// External port interface which must be implemented or initialised for each
// port.
extern struct SGFX GFX;

bool8 S9xGraphicsInit ();
void S9xGraphicsDeinit();
bool8 S9xInitUpdate (void);
bool8 S9xDeinitUpdate (int Width, int Height, bool8 sixteen_bit);
void S9xSetPalette ();
void S9xSyncSpeed ();

#ifdef GFX_MULTI_FORMAT
bool8 S9xSetRenderPixelFormat (int format);
#endif

END_EXTERN_C

#endif

