//=============================================================================
// Basic user interface framework for low-level drawing operations to
// the bottom screen.
//=============================================================================

#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <3ds.h>
#include "snes9x.h"
#include <sys/stat.h>

#include "3dslog.h"
#include "3dsimpl_gpu.h"
#include "3dsui.h"
#include "3dsfont.cpp"
#include "3dssettings.h"
#include <png_utils.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ASSERT(x)
#define STBI_ONLY_PNG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define MAX_ALPHA 8

#define UI_TEX_COUNT 4
#define BEZEL_INNER_WIDTH 320
#define BEZEL_INNER_HEIGHT 239
#define WIDTH_SCALE 1 / BEZEL_INNER_WIDTH
#define HEIGHT_SCALE 1 / BEZEL_INNER_HEIGHT

typedef struct
{
    int red[MAX_ALPHA + 1][32];
    int green[MAX_ALPHA + 1][32];
    int blue[MAX_ALPHA + 1][32];
} SAlpha;

typedef struct {
    u16 width;
    u16 height;
} AssetDimensions;

typedef struct {
    u16 opacity;
    u16 screenWidth;
    gfxScreen_t targetScreen;
    AssetDisplayMode displayMode;
} AssetDrawContext;

typedef struct {
    C3D_Tex tex;
    AssetDimensions dim;
    char path[_MAX_PATH];
    bool active;
} UiAsset;

typedef struct {
    u64 visibleUntil;
    u32 color;
    u16 x0, y0, x1, y1;
    u8 borderSize, padding, margin;
    bool visible;
    char message[64];
} UI_NotificationState;

static UI_NotificationState notification = {0};

SAlpha alphas;

int foreColor = 0xffffff;
int backColor = 0x000000;

int translateX = 0;
int translateY = 0;
int viewportX1, viewportY1, viewportX2, viewportY2;

int fontHeight = FONT_HEIGHT;

int viewportStackCount = 0;
int viewportStack[20][4];

int bounds[10];

uint8 *fontWidthArray[] = { fontTempestaWidth, fontRondaWidth, fontArialWidth };
uint8 *fontBitmapArray[] = { fontTempestaBitmap, fontRondaBitmap, fontArialBitmap };

uint8 *fontBitmap;
uint8 *fontWidth;

Tex3DS_Texture textureInfo[UI_TEX_COUNT];

// everything except UI_ATLAS
static UiAsset defaultAssets[UI_TEX_COUNT - 1]; // holds bundled t3x files, overwritten by runtime PNGs if available
static UiAsset externalAssets[UI_TEX_COUNT - 1]; // holds runtime PNGs if available

static u32 imageBufferSize;
static u8* stagingBuffer;
static u8* texUploadBuffer;

static bool captureRegionToBuffer(int width, int height, int x0, int y0, gfxScreen_t screen) {
    if (!stagingBuffer) return false;

    u8* fb = (u8*)gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL); 
    u8* dst = (u8*)stagingBuffer;
    const int stride = SCREEN_HEIGHT * 3; 

    for (int y = 0; y < height; y++) {
        int img_y = y0 + y;
        int col = 239 - img_y;
        u8* src = fb + (x0 * stride) + (col * 3);
        
        u8* dstRow = dst + (y * width * 3);

        for (int x = 0; x < width; x++) {
            dstRow[0] = src[2];
            dstRow[1] = src[1];
            dstRow[2] = src[0];

            dstRow += 3;
            src += stride;
        }
    }
    return true;
}

static AssetDrawContext getAssetDrawContext(SGPU_TEXTURE_ID textureId) {
    AssetDrawContext ctx;

    switch (textureId) {
        case UI_BORDER:
            ctx.targetScreen = screenSettings.GameScreen;
            ctx.displayMode  = (AssetDisplayMode)settings3DS.GameBorder;
            ctx.opacity      = settings3DS.GameBorderOpacity;
            ctx.screenWidth  = screenSettings.GameScreenWidth;
            break;
        case UI_BEZEL:
            ctx.targetScreen = screenSettings.GameScreen;
            ctx.displayMode  = (AssetDisplayMode)settings3DS.GameBezel;
            ctx.opacity      = OPACITY_STEPS;
            ctx.screenWidth  = screenSettings.GameScreenWidth;
            break;
        case UI_COVER:
            ctx.targetScreen = screenSettings.SecondScreen;
            ctx.displayMode  = (AssetDisplayMode)settings3DS.SecondScreenContent;
            ctx.opacity      = settings3DS.SecondScreenOpacity;
            ctx.screenWidth  = screenSettings.SecondScreenWidth;
            break;
        
        default:
            ctx.targetScreen = screenSettings.GameScreen;
            ctx.displayMode  = ASSET_NONE;
            ctx.opacity      = 0;
            ctx.screenWidth  = screenSettings.GameScreenWidth;
            break;
    }
    
    return ctx;
}

// decodes PNG, uploads to VRAM and updates UiAsset metadata
static bool ui3dsLoadPngToAsset(UiAsset* asset, int textureIdx, const char* path) {
    C3D_Tex* tex = &asset->tex;

    if (!tex->data) {
        log3dsWrite("[ui3ds] VRAM not allocated for asset idx %d", textureIdx);
        return false;
    }

    s8 transferFormat = gpu3dsGetTransferFmt(tex->fmt);

    // currently CPU swizzling logic only supports RGB565 and RGBA8 for convenience
    if (transferFormat != GX_TRANSFER_FMT_RGB565 && transferFormat != GX_TRANSFER_FMT_RGBA8) {
        log3dsWrite("[ui3ds] Unsupported format %d for %s", transferFormat, path);
        return false;
    }

    int width, height;
    if (!decodePngFromFile(stagingBuffer, imageBufferSize, path, width, height)) {
        log3dsWrite("[ui3ds] PNG decode failed: %s", path);
        return false;
    }

    const Tex3DS_SubTexture* subTex = Tex3DS_GetSubTexture(textureInfo[textureIdx], 0);
    if (!width || !height || width > subTex->width || height > subTex->height) {
        log3dsWrite("[ui3ds] Invalid dimensions for %s (%dx%d has to be < %dx%d)", path, width, height, subTex->width, subTex->height);
        return false;
    }

    u32 textureByteSize = tex->width * tex->height * gpu3dsGetPixelSize(tex->fmt);
    memset(texUploadBuffer, 0, textureByteSize);

    int tx = (int)(subTex->left * tex->width);
    int ty = (int)((1.0f - subTex->top) * tex->height); 
    u32* src = (u32*)stagingBuffer;

    if (transferFormat == GX_TRANSFER_FMT_RGB565) {
        u16* dst = (u16*)texUploadBuffer + (ty * tex->width) + tx;
        int dstStride = tex->width - width;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                u32 p = *src++;
                *dst++ = ((p & 0xF8) << 8) | ((p & 0xFC00) >> 5) | ((p & 0xF80000) >> 19);
            }        
            dst += dstStride;
        }
    } else {
         // RGBA8
        u32* dst = (u32*)texUploadBuffer + (ty * tex->width) + tx;
        int dstStride = tex->width - width;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                *dst++ = __builtin_bswap32(*src++);
            }
            dst += dstStride;
        }
    }
    
    GSPGPU_FlushDataCache(texUploadBuffer, textureByteSize);

    C3D_SyncDisplayTransfer(
        (u32*)texUploadBuffer, GX_BUFFER_DIM(tex->width, tex->height),
        (u32*)tex->data, GX_BUFFER_DIM(tex->width, tex->height),
        GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_IN_FORMAT(transferFormat) | GX_TRANSFER_OUT_FORMAT(transferFormat)
    );

    strncpy(asset->path, path, _MAX_PATH - 1);
    asset->path[_MAX_PATH - 1] = '\0';
    asset->dim.width = width;
    asset->dim.height = height;
    asset->active = true;

    return true;
}

//---------------------------------------------------------------
// Initialize this library
//---------------------------------------------------------------

void ui3dsInitialize()
{
    memset(defaultAssets, 0, sizeof(defaultAssets));
    memset(externalAssets, 0, sizeof(externalAssets));

    for (int i = 0; i < 32; i++)
    {
        for (int a = 0; a <= MAX_ALPHA; a++)
        {
            int f = i * a / MAX_ALPHA;
            alphas.red[a][i] = f << 11;
            alphas.green[a][i] = f << 6;
            alphas.blue[a][i] = f;
        }
    }

    // Initialize fonts
    //
    for (int f = 0; f < 3; f++)
    {
        fontBitmap = fontBitmapArray[f];
        fontWidth = fontWidthArray[f];
        for (int i = 0; i < 65536; i++)
        {
            uint8 c = fontBitmap[i];
            if (c == ' ')
                fontBitmap[i] = 0;
            else
                fontBitmap[i] = c - '0';
        }
    }

    fontBitmap = fontTempestaBitmap;
    fontWidth = fontTempestaWidth;

    ui3dsSetFont(settings3DS.Font);
}

int ui3dsGetScreenWidth(gfxScreen_t targetScreen) {
    return (targetScreen == GFX_TOP) ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH;
}

// this is called on init and if game screen has swapped
void ui3dsUpdateScreenSettings(gfxScreen_t gameScreen) {
    screenSettings.GameScreen = gameScreen;

    if (screenSettings.GameScreen == GFX_TOP) {
	    screenSettings.GameScreenWidth = SCREEN_TOP_WIDTH;
	    screenSettings.SecondScreen = GFX_BOTTOM;
        screenSettings.SecondScreenWidth = SCREEN_BOTTOM_WIDTH;
    } else {
	    screenSettings.GameScreenWidth = SCREEN_BOTTOM_WIDTH;
	    screenSettings.SecondScreen = GFX_TOP;
        screenSettings.SecondScreenWidth = SCREEN_TOP_WIDTH;
    }
    
    // reset menu viewport
    viewportX1 = 0;
    viewportY1 = 0;
    viewportX2 = screenSettings.SecondScreenWidth;
    viewportY2 = SCREEN_HEIGHT;
    viewportStackCount = 0;
}

//---------------------------------------------------------------
// Sets the font
//---------------------------------------------------------------
void ui3dsSetFont(int fontIndex)
{
    if (fontIndex >= 0 && fontIndex < 3)
    {
        fontBitmap = fontBitmapArray[fontIndex];
        fontWidth = fontWidthArray[fontIndex];
    }
}

//---------------------------------------------------------------
// Sets the global viewport for all drawing
//---------------------------------------------------------------
void ui3dsSetViewport(int x1, int y1, int x2, int y2)
{
    viewportX1 = x1;
    viewportX2 = x2;
    viewportY1 = y1;
    viewportY2 = y2;

    if (viewportX1 < 0) viewportX1 = 0;
    if (viewportX2 > screenSettings.SecondScreenWidth) viewportX2 = screenSettings.SecondScreenWidth;
    if (viewportY1 < 0) viewportY1 = 0;
    if (viewportY2 > SCREEN_HEIGHT) viewportY2 = SCREEN_HEIGHT;
}

//---------------------------------------------------------------
// Push the global viewport for all drawing
//---------------------------------------------------------------
void ui3dsPushViewport(int x1, int y1, int x2, int y2)
{
    if (viewportStackCount < 10)
    {
        if (x1 < viewportX1) x1 = viewportX1;
        if (x2 > viewportX2) x2 = viewportX2;
        if (y1 < viewportY1) y1 = viewportY1;
        if (y2 > viewportY2) y2 = viewportY2;

        viewportStack[viewportStackCount][0] = viewportX1;
        viewportStack[viewportStackCount][1] = viewportX2;
        viewportStack[viewportStackCount][2] = viewportY1;
        viewportStack[viewportStackCount][3] = viewportY2;
        viewportStackCount++;

        ui3dsSetViewport(x1, y1, x2, y2);
    }
}

//---------------------------------------------------------------
// Pop the global viewport 
//---------------------------------------------------------------
void ui3dsPopViewport()
{
    if (viewportStackCount > 0)
    {
        viewportStackCount--;
        viewportX1 = viewportStack[viewportStackCount][0];
        viewportX2 = viewportStack[viewportStackCount][1];
        viewportY1 = viewportStack[viewportStackCount][2];
        viewportY2 = viewportStack[viewportStackCount][3];
    }
}


//---------------------------------------------------------------
// Applies alpha to a given colour.
// NOTE: Alpha is a value from 0 to 10. (0 = transparent, 10 = opaque)
//---------------------------------------------------------------
inline uint16 __attribute__((always_inline)) ui3dsApplyAlphaToColour565(int color565, int alpha)
{
    int red = (color565 >> 11) & 0x1f;
    int green = (color565 >> 6) & 0x1f; // drop the LSB of the green colour
    int blue = (color565) & 0x1f;

    int result = alphas.red[alpha][red] | alphas.blue[alpha][blue] | alphas.green[alpha][green];
    
    return result;
}


int ui3dsApplyAlphaToColor(int color, float alpha, bool rgb8)
{
    if (alpha < 0)      alpha = 0;
    if (alpha > 1.0f)   alpha = 1.0f;

    int a = (int)(alpha * 255);
    int shift = rgb8 ? 8 : 0;

    return 
        ((((color >> 16 + shift) & 0xff) * a / 255) << (16 + shift)) |
        ((((color >> 8 + shift) & 0xff) * a / 255) << (8 + shift)) |
        ((((color >> shift) & 0xff) * a / 255) << shift);
}


//---------------------------------------------------------------
// Sets the global translate for all drawing
//---------------------------------------------------------------
void ui3dsSetTranslate(int tx, int ty)
{
    translateX = tx;
    translateY = ty;
}


//---------------------------------------------------------------
// Computes the frame buffer offset given the x, y
// coordinates.
//---------------------------------------------------------------
inline int __attribute__((always_inline)) ui3dsComputeFrameBufferOffset(int x, int y)
{
    return ((x) * SCREEN_HEIGHT + (239 - y));
}


//---------------------------------------------------------------
// Gets a pixel colour.
//---------------------------------------------------------------
inline uint16 __attribute__((always_inline)) ui3dsGetPixelInline(uint16 *frameBuffer, int x, int y)
{
    return frameBuffer[ui3dsComputeFrameBufferOffset((x), (y))];  
}


//---------------------------------------------------------------
// Sets a pixel colour.
//---------------------------------------------------------------
inline void __attribute__((always_inline)) ui3dsSetPixelInline(uint16 *frameBuffer, int x, int y, int color)
{
    if (color < 0) return;
    if ((x) >= viewportX1 && (x) < viewportX2 && 
        (y) >= viewportY1 && (y) < viewportY2) 
    { 
        frameBuffer[ui3dsComputeFrameBufferOffset((x), (y))] = color;  
    }
}


//---------------------------------------------------------------
// Draws a single character to the screen
//---------------------------------------------------------------
void ui3dsDrawChar(uint16 *frameBuffer, int x, int y, int color565, uint8 c)
{
    // Draws a character to the screen at (x,y) 
    // (0,0) is at the top left of the screen.
    //
    int wid = fontWidth[c];
    uint8 alpha;
    //printf ("d %c (%d)\n", c, bmofs);

    if ((y) >= viewportY1 && (y) < viewportY2)
    {
        for (int x1 = 0; x1 < wid; x1++)
        {
            #define GETFONTBITMAP(c, x, y) fontBitmap[c * 256 + x + (y)*16]

            #define SETPIXELFROMBITMAP(y1) \
                alpha = GETFONTBITMAP(c,x1,y1); \
                ui3dsSetPixelInline(frameBuffer, cx, cy + y1, \
                alpha == MAX_ALPHA ? color565 : \
                alpha == 0x0 ? -1 : \
                    ui3dsApplyAlphaToColour565(color565, alpha) + \
                    ui3dsApplyAlphaToColour565(ui3dsGetPixelInline(frameBuffer, cx, cy + y1), MAX_ALPHA - alpha));

            int cx = (x + x1);
            int cy = (y);
            if (cx >= viewportX1 && cx < viewportX2)
            {
                for (int h = 0; h < fontHeight; h++)
                {
                    SETPIXELFROMBITMAP(h);
                }
            }
        }
    }
}

void ui3dsDraw8BitChar(u8 *fb, int x, int y, int color, u8 c)
{
    const int wid = fontWidth[c];
    const int bpp = 3;

    const u8 r = (color >> 16) & 0xFF;
    const u8 g = (color >> 8)  & 0xFF;
    const u8 b = (color)       & 0xFF;

    if (y >= viewportY1 && y < viewportY2) 
    {
        for (int x1 = 0; x1 < wid; x1++)
        {

            #define GETFONTBITMAP(c, x, y) fontBitmap[c * 256 + x + (y) * 16]
            int cx = x + x1;

            if (cx >= viewportX1 && cx < viewportX2)
            {
                // Loop through each row of the character, top-to-bottom
                for (int y1 = 0; y1 < fontHeight; y1++)
                {
                    int cy = y + y1;

                    float alpha = (float)(GETFONTBITMAP(c, x1, y1) * 32 - 1) / 255.0f;
                    if (alpha <= 0.0f) continue;

                    if (alpha > 1.0f) alpha = 1.0f;

                    int offset = (cx * SCREEN_HEIGHT) + (239 - cy);
                    u8* dest_ptr = fb + (offset * bpp);

                    dest_ptr[2] = (u8)(r * alpha + dest_ptr[2] * (1.0f - alpha));
                    dest_ptr[1] = (u8)(g * alpha + dest_ptr[1] * (1.0f - alpha));
                    dest_ptr[0] = (u8)(b * alpha + dest_ptr[0] * (1.0f - alpha));
                }
            }
        }
    }
}

void ui3dsDraw32BitChar(u32 *fb, int x, int y, int color, uint8 c)
{
    int wid = fontWidth[c];
    uint8 alpha;
    
    if ((y) >= viewportY1 && (y) < viewportY2)
    {
        for (int x1 = 0; x1 < wid; x1++)
        {
            #define GETFONTBITMAP(c, x, y) fontBitmap[c * 256 + x + (y)*16]

            int cx = (x + x1);
            int cy = (y);
            uint32 newColor;
            if (cx >= viewportX1 && cx < viewportX2)
            {
                for (int y1 = 0; y1 < fontHeight; y1++)
                {
                    int fi = (cx) * SCREEN_HEIGHT + (239 - cy + y1 - fontHeight); 
                    float alpha = (float)(GETFONTBITMAP(c,x1,fontHeight - y1) * 32 - 1) / 255;
                    if (alpha < 0)      
                        alpha = 0;
                    if (alpha > 1.0f)
                        alpha = 1.0f;
                    
                    newColor = (ui3dsApplyAlphaToColor(color, alpha) << 8) + \
                               ui3dsApplyAlphaToColor(fb[fi], 1.0 - alpha, true);
                    
                    fb[fi] = newColor;  
                }
            }
        }
    }
}

//---------------------------------------------------------------
// Computes width of the string
//---------------------------------------------------------------
int ui3dsGetStringWidth(const char *s, int startPos = 0, int endPos = 0xffff)
{
    int totalWidth = 0;
    for (int i = startPos; i <= endPos; i++)
    {
        uint8 c = s[i];
        if (c == 0)
            break;
        totalWidth += fontWidth[c];
    }   
    return totalWidth;
}

#define CONVERT_TO_565(x)    (((x & 0xf8) >> 3) | (((x >> 8) & 0xf8) << 3) | (((x >> 16) & 0xf8) << 8))

//---------------------------------------------------------------
// Colors are in the 888 (RGB) format.
//---------------------------------------------------------------
void ui3dsSetColor(int newForeColor, int newBackColor)
{
    foreColor = newForeColor;
    backColor = newBackColor;
}

void ui3dsDraw32BitRect(uint32 * fb, int x0, int y0, int x1, int y1, int color, float alpha)
{
    uint32 c = ui3dsApplyAlphaToColor(color, alpha) << 8;
    
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            int fbofs = (x) * SCREEN_HEIGHT + (SCREEN_HEIGHT - 1 - y);
            fb[fbofs] = c + (alpha < 1.0 ? ui3dsApplyAlphaToColor(fb[fbofs], 1.0 - alpha, true) : 0);
        }
    }

}

//---------------------------------------------------------------
// Draws a rectangle with the colour (in RGB888 format).
// 
// Note: x0,y0 are inclusive. x1,y1 are exclusive.
//---------------------------------------------------------------
void ui3dsDrawRect(int x0, int y0, int x1, int y1, int color, float alpha)
{
    if (color < 0)
        return;

    x0 += translateX;
    x1 += translateX;
    y0 += translateY;
    y1 += translateY;

    if (x0 < viewportX1) x0 = viewportX1;
    if (x1 > viewportX2) x1 = viewportX2;
    if (y0 < viewportY1) y0 = viewportY1;
    if (y1 > viewportY2) y1 = viewportY2;
    
    if (alpha < 0) alpha = 0;
    if (alpha > 1.0f) alpha = 1.0f;
        
    uint16* fb = (uint16 *) gfxGetFramebuffer(screenSettings.SecondScreen, GFX_LEFT, NULL, NULL);

    if (gfxGetScreenFormat(screenSettings.SecondScreen) == GSP_RGBA8_OES) {
        return ui3dsDraw32BitRect((uint32 *)fb, x0, y0, x1, y1, color, alpha);
    }

    color = CONVERT_TO_565(color);
   
    if (alpha == 1.0f)
    {
        for (int x = x0; x < x1; x++)
        {
            int fbofs = (x) * SCREEN_HEIGHT + (239 - y0);
            for (int y = y0; y < y1; y++)
                fb[fbofs--] = color;
        }
    }
    else if (alpha == 0.0)
    {
        return;
    }
    else
    {
        int iAlpha = alpha * MAX_ALPHA;
        for (int x = x0; x < x1; x++)
        {
            int fbofs = (x) * SCREEN_HEIGHT + (239 - y0);
            for (int y = y0; y < y1; y++)
            {
                fb[fbofs] = 
                    ui3dsApplyAlphaToColour565(color, iAlpha) +
                    ui3dsApplyAlphaToColour565(fb[fbofs], MAX_ALPHA - iAlpha);
                fbofs --;
            }
        }
    }
}

void ui3dsDrawCheckerboard(int x0, int y0, int x1, int y1, int color1, int color2)
{
    x0 += translateX;
    x1 += translateX;
    y0 += translateY;
    y1 += translateY;

    if (x0 < viewportX1) x0 = viewportX1;
    if (x1 > viewportX2) x1 = viewportX2;
    if (y0 < viewportY1) y0 = viewportY1;
    if (y1 > viewportY2) y1 = viewportY2;
        
    uint16* fb = (uint16 *) gfxGetFramebuffer(screenSettings.SecondScreen, GFX_LEFT, NULL, NULL);

    int color1_565 = CONVERT_TO_565(color1);
    int color2_565 = CONVERT_TO_565(color2);
   
    int tileWidth = 2;
    int tileHeight = 2;

    for (int x = x0; x < x1; x += tileWidth)
    {
        for (int y = y0; y < y1; y += tileHeight)
        {
            // Determine the color for this tile based on its position
            int tileColor = ((x / tileWidth) + (y / tileHeight)) % 2 == 0 ? color1_565 : color2_565;

            // Fill the current tile
            for (int dx = 0; dx < tileWidth; dx++)
            {
                for (int dy = 0; dy < tileHeight; dy++)
                {
                    int fbofs = (x + dx) * SCREEN_HEIGHT + (239 - (y + dy));
                    fb[fbofs] = tileColor;
                }
            }
        }
    }
}

// overlay blending mode: returns a color in RGB888 format
// may have more performance impact than simple blending mode 
// but will provide more vibrant colors
// TODO: add alpha value support
int ui3dsOverlayBlendColor(int backgroundColor, int foregroundColor) {
    // Extract the red, green, and blue components of the colors
    float baseR = ((backgroundColor >> 16) & 0xFF) / 255.0f;
    float baseG = ((backgroundColor >> 8) & 0xFF) / 255.0f;
    float baseB = (backgroundColor & 0xFF) / 255.0f;
    
    float blendR = ((foregroundColor >> 16) & 0xFF) / 255.0f;
    float blendG = ((foregroundColor >> 8) & 0xFF) / 255.0f;
    float blendB = (foregroundColor & 0xFF) / 255.0f;

    float resultR = baseR <= 0.5f ? 2 * baseR * blendR : 1 - 2 * (1 - baseR) * (1 - blendR);
    float resultG = baseG <= 0.5f ? 2 * baseG * blendG : 1 - 2 * (1 - baseG) * (1 - blendG);
    float resultB = baseB <= 0.5f ? 2 * baseB * blendB : 1 - 2 * (1 - baseB) * (1 - blendB);

    unsigned char r = static_cast<unsigned int>(resultR * 255);
    unsigned char g = static_cast<unsigned int>(resultG * 255);
    unsigned char b = static_cast<unsigned int>(resultB * 255);

    return (r << 16) | (g << 8) | b;
}

//---------------------------------------------------------------
// Draws a rectangle with the back colour 
// 
// Note: x0,y0 are inclusive. x1,y1 are exclusive.
//---------------------------------------------------------------
void ui3dsDrawRect(int x0, int y0, int x1, int y1)
{
    ui3dsDrawRect(x0, y0, x1, y1, backColor, 1.0f);
}


//---------------------------------------------------------------
// Draws a string at the given position without translation.
//---------------------------------------------------------------
int ui3dsDrawStringOnly(gfxScreen_t targetScreen, int absoluteX, int absoluteY, int color, const char *buffer, int startPos = 0, int endPos = 0xffff)
{
    int x = absoluteX;
    int y = absoluteY;

    if (color < 0)
        return x;
    if (y >= viewportY1 - 16 && y <= viewportY2)
    {
        GSPGPU_FramebufferFormat fmt = gfxGetScreenFormat(targetScreen);

        if (fmt == GSP_RGB565_OES)
        {
            u16 color565 = CONVERT_TO_565(color);
            u16 *fb = (u16 *)gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL);

            for (int i = startPos; i <= endPos; i++)    
            {
                u8 c = buffer[i];
                if (c == 0) break;
                if (c != ' ')
                    ui3dsDrawChar(fb, x, y, color565, c);
                x += fontWidth[c];
            }
        }
        else if (fmt == GSP_BGR8_OES)
        {
            u8 *fb = (u8 *)gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL);

            for (int i = startPos; i <= endPos; i++)    
            {
                u8 c = buffer[i];
                if (c == 0) break;
                if (c != ' ')
                    ui3dsDraw8BitChar(fb, x, y, color, c);
                x += fontWidth[c];
            }
        }
        else
        {
            u32 *fb = (u32 *)gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL);

            for (int i = startPos; i <= endPos; i++)    
            {
                u8 c = buffer[i];
                if (c == 0) break;
                if (c != ' ')
                    ui3dsDraw32BitChar(fb, x, y, color, c);
                x += fontWidth[c];
            }
        }
    }

    return x;
}


//---------------------------------------------------------------
// Draws a string with the forecolor, with wrapping
//---------------------------------------------------------------
void ui3dsDrawStringWithWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer)
{
    int strLineCount = 0;
    int strLineStart[30];
    int strLineEnd[30];

    x0 += translateX;
    x1 += translateX;
    y0 += translateY;
    y1 += translateY;
    
    ui3dsPushViewport(x0, y0, x1, y1);
    //ui3dsDrawRect(x0, y0, x1, y1, backColor);  // Draw the background color
   
    if (buffer != NULL)
    {
        int maxWidth = x1 - x0;
        int slen = strlen(buffer);

        int curStartPos = 0;
        int curEndPos = slen - 1;
        int lineWidth = 0;
        for (int i = 0; i < slen; )
        {
            if (i != curStartPos)
            {
                if (buffer[i] == ' ' && i > 0 && buffer[i-1] != ' ')
                    curEndPos = i - 1;
                else if (buffer[i] == '-')  // use space or dash as line breaks
                    curEndPos = i;
                else if (buffer[i] == '/')
                    curEndPos = i;
                else if (buffer[i] == '\n')  // \n as line breaks.
                {
                    curEndPos = i - 1;
                    lineWidth = 999999;     // force the line break.
                }
            }
            lineWidth += fontWidth[buffer[i]];
            if (lineWidth > maxWidth)
            {
                // Break the line here
                strLineStart[strLineCount] = curStartPos;
                strLineEnd[strLineCount] = curEndPos;
                strLineCount++;

                if (strLineCount >= 30) break; 

                if (lineWidth != 999999)
                {
                    i = curEndPos + 1;
                    while (buffer[i] == ' ')
                        i++;
                }
                else
                {
                    i = curEndPos + 2;
                }
                curStartPos = i;
                curEndPos = slen - 1;
                lineWidth = 0;
            }
            else
                i++;
        }

        // Output the last line.
        curEndPos = slen - 1;
        if (curStartPos <= curEndPos)
        {
            strLineStart[strLineCount] = curStartPos;
            strLineEnd[strLineCount] = curEndPos;
            strLineCount++;
        }

        for (int i = 0; i < strLineCount; i++)
        {
            int x = x0;
            if (horizontalAlignment >= 0)
            {
                int sWidth = ui3dsGetStringWidth(buffer, strLineStart[i], strLineEnd[i]);

                if (horizontalAlignment == 0)   // center aligned
                    x = (maxWidth - sWidth) / 2 + x0;
                else                            // right aligned
                    x = maxWidth - sWidth + x0;
            }

            ui3dsDrawStringOnly(targetScreen, x, y0, color, buffer, strLineStart[i], strLineEnd[i]);
            y0 += 12;
        }
    }

    ui3dsPopViewport();
}


//---------------------------------------------------------------
// Draws a string with the forecolor, with no wrapping
//---------------------------------------------------------------
int ui3dsDrawStringWithNoWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer)
{
    x0 += translateX;
    x1 += translateX;
    y0 += translateY;
    y1 += translateY;

    int xEndPosition = 0;
    
    ui3dsPushViewport(x0, y0, x1, y1);
   
    if (buffer != NULL)
    {
        int maxWidth = x1 - x0;
        int x = x0;
        if (horizontalAlignment >= HALIGN_CENTER)
        {
            int sWidth = ui3dsGetStringWidth(buffer);

            if (horizontalAlignment == HALIGN_CENTER)   // center aligned
                x = (maxWidth - sWidth) / 2 + x0;
            else                                        // right aligned
                x = maxWidth - sWidth + x0;
        }
        xEndPosition = ui3dsDrawStringOnly(targetScreen, x, y0, color, buffer);
    }

    ui3dsPopViewport();

    return xEndPosition;
}

//---------------------------------------------------------------
// Copies pixel data from the frame buffer to another buffer
//---------------------------------------------------------------
void ui3dsCopyFromFrameBuffer(uint16 *destBuffer)
{
    uint16* fb = (uint16 *) gfxGetFramebuffer(screenSettings.SecondScreen, GFX_LEFT, NULL, NULL);
    memcpy(destBuffer, fb, screenSettings.SecondScreenWidth*SCREEN_HEIGHT*2);
}


//---------------------------------------------------------------
// Copies pixel data from the buffer to the framebuffer
//---------------------------------------------------------------
void ui3dsBlitToFrameBuffer(uint16 *srcBuffer, float alpha)
{
    uint16* fb = (uint16 *) gfxGetFramebuffer(screenSettings.SecondScreen, GFX_LEFT, NULL, NULL);
    
    int a = (int)(alpha * MAX_ALPHA);
    for (int x = viewportX1; x < viewportX2; x++)
        for (int y = viewportY1; y < viewportY2; y++)
        {
            int ofs = ui3dsComputeFrameBufferOffset(x,y);
            int color = ui3dsApplyAlphaToColour565(srcBuffer[ofs], a);
            fb[ofs] = color;
        }
}

// default bounds = [0, 0, width, height]
Bounds ui3dsGetBounds(int screenWidth, int width, int height, Position position, int offsetX, int offsetY) {
    Bounds bounds;

    if (position == Position::TL || position == Position::TC  || position == Position::TR) {
        bounds.top = 0 + offsetY;
    }

    if (position == Position::ML || position == Position::MC  || position == Position::MR) {
        bounds.top = (SCREEN_HEIGHT - height) / 2;
    }

    if (position == Position::BL || position == Position::BC  || position == Position::BR) {
        bounds.top = SCREEN_HEIGHT - height - offsetY;
    }

    if (position == Position::TL || position == Position::ML  || position == Position::BL) {
        bounds.left = 0 + offsetX;
    }

    if (position == Position::TC || position == Position::MC  || position == Position::BC) {
        bounds.left = (screenWidth - width) / 2 + offsetX;
    }

    if (position == Position::TR || position == Position::MR  || position == Position::BR) {
        bounds.left = screenWidth - width - offsetX;
    }

    bounds.right = bounds.left + width;
    bounds.bottom = bounds.top + height;

    return bounds;
}

int ui3dsBlendingColor(int bg, unsigned char r, unsigned char g, unsigned char b, unsigned char a, unsigned char opacity, bool isRGB565) {
    unsigned char bgr, bgg, bgb;

    if (isRGB565) {
        if (opacity == 255) {
            return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }

        bgr = bg >> 11 << 3;
        bgg = bg >> 5 << 2;
        bgb = bg >> 3;
    } else {
        if (opacity == 255 && a == 255) {
            return (r << 24) | (g << 16) | (b << 8);
        }

        bgr = bg >> 24 & 0xff;
        bgg = bg >> 16 & 0xff;
        bgb = bg >> 8 & 0xff;
    }

    float alpha = static_cast<float>(a) / 255.0f;
    float blendOpacity = static_cast<float>(opacity) / 255.0f;
    r = static_cast<unsigned char>(r * alpha * blendOpacity + bgr * (1.0f - alpha * blendOpacity));
    g = static_cast<unsigned char>(g * alpha * blendOpacity + bgg * (1.0f - alpha * blendOpacity));
    b = static_cast<unsigned char>(b * alpha * blendOpacity + bgb * (1.0f - alpha * blendOpacity));

    if (isRGB565) {
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }

    return (r << 24) | (g << 16) | (b << 8);
}

template <typename T>
void ui3dsDrawImage(T *fb, gfxScreen_t targetScreen, Bounds bounds, unsigned char *imageData, int channels, float alpha, ImageBorder border, const char *errorMessage, int factor) {
    int screenWidth = ui3dsGetScreenWidth(targetScreen);
    int imageWidth = bounds.right - bounds.left;
    int imageHeight = bounds.top - bounds.bottom;
    
    // handle out of bounds (e.g. 400x240 pixel image on bottom screen)
    int x0 = imageWidth > screenWidth ? bounds.left : bounds.left < 0 ? 0 : bounds.left;
    int x1 = bounds.right > screenWidth ? screenWidth : bounds.right;
    int y0 = bounds.top < 0 ? 0 : bounds.top; 
    int y1 = bounds.bottom > SCREEN_HEIGHT ? SCREEN_HEIGHT : bounds.bottom;

    if (targetScreen == screenSettings.SecondScreen) {
        if (sizeof(border) > 0 && targetScreen == screenSettings.SecondScreen) {
            ui3dsDrawRect(x0 - border.width, y0 - border.width, x1 + border.width, y1 + border.width, border.color);
        }

        if (!imageData) {
            if (errorMessage) {
                Bounds mBounds = ui3dsGetBounds(screenWidth, screenWidth - 12, 28, Position::MC, 0, 0); 
                ui3dsDrawStringWithWrapping(targetScreen, mBounds.left, mBounds.top, mBounds.right, mBounds.bottom, 0xbbbbbb, HALIGN_CENTER, errorMessage);
            }

            return;
        }
    }

    GSPGPU_FramebufferFormat fmt = gfxGetScreenFormat(targetScreen);
    
    int bpp = gpu3dsGetPixelSize((GPU_TEXCOLOR(fmt)));

    int rOfs = fmt == GSP_BGR8_OES ? 2 : 0;
    int bOfs = fmt == GSP_BGR8_OES ? 0 : 2;

    for (int x = x0; x < x1; x++) {
        for (int y = y0; y < y1; y++) {
            int src_index = ((y - y0) * imageWidth + (x - x0)) * channels;
            u8 r = imageData[src_index];
            u8 g = imageData[src_index + 1];
            u8 b = imageData[src_index + 2];

            int fb_col = SCREEN_HEIGHT - 1 - y;

            if (fmt == GSP_RGB565_OES) {
                int fbofs = (x * SCREEN_HEIGHT + fb_col);
                fb[fbofs] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            } else {
                int fbofs = (x * SCREEN_HEIGHT + fb_col) * bpp;

                if (fmt == GSP_BGR8_OES) {
                    fb[fbofs + rOfs]    = r; // Red
                    fb[fbofs + 1]       = g; // Green
                    fb[fbofs + bOfs]    = b; // Blue
                } else {
                    u8 a = imageData[src_index + 3];

                    fb[fbofs] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
        }
    }
}

void ui3dsPrepareImage(gfxScreen_t targetScreen, const char *imagePath, unsigned char *imageData, IMAGE_TYPE type, int width, int height, int channels) {
    int screenWidth = ui3dsGetScreenWidth(targetScreen);
    std::string message;

    // default image properties
    ImageBorder border = { 0, NULL };
    Position position = Position::MC;
    float alpha = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
    int scaleFactor = 1;

    // override properties based on image type
    if (type == IMAGE_TYPE::PREVIEW) {
        position = Position::BR;

        if (settings3DS.Theme != THEME_RETROARCH) {
            border.width = 3;
            border.color = Themes[settings3DS.Theme].menuBackColor;
            offsetX = border.width;
            offsetY = border.width + 20;
        } else {
            border.width = 0;
            position = Position::BR;
            offsetX = 8;
            offsetY = 18;
        }
    }

    if (type == IMAGE_TYPE::COVER) {
        alpha = (float)(settings3DS.SecondScreenOpacity) / OPACITY_STEPS;
    }

    if (!imageData || !width || !height || height > SCREEN_HEIGHT || width > 800) {   
        message = "Failed to load image\n" + std::string(imagePath);

        if (type != IMAGE_TYPE::PREVIEW) {
            width = screenWidth;
            height = SCREEN_HEIGHT;
        } else {
            width = 0;
            height = 0;
        }
    }

    Bounds bounds = ui3dsGetBounds(screenWidth, width * scaleFactor, height * scaleFactor, position, offsetX, offsetY);

    if (gfxGetScreenFormat(targetScreen) == GSP_RGB565_OES) {
        ui3dsDrawImage<u16>((u16 *) gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL), targetScreen, bounds, imageData, channels, alpha, border, message.c_str(), scaleFactor);    
    }
    else 
        ui3dsDrawImage<u8>((u8 *) gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL), targetScreen, bounds, imageData, channels, alpha, border, message.c_str(), scaleFactor);
}

// render image from path
void ui3dsRenderImage(gfxScreen_t targetScreen, const char *imagePath, IMAGE_TYPE type, bool ignoreAlphaMask) {
    int width, height, n;
    std::string message;
    int channels = ignoreAlphaMask ? 3 : 4;
    
    unsigned char *imageData = stbi_load(imagePath, &width, &height, &n, channels);
    ui3dsPrepareImage(targetScreen, imagePath, imageData, type, width, height, channels);
    
    if (imageData) {
        stbi_image_free(imageData);
    }
}

// render image from memory
void ui3dsRenderImage(gfxScreen_t targetScreen, const char *imagePath, unsigned char *bufferData, int bufferSize, IMAGE_TYPE type, bool ignoreAlphaMask) {
    int width, height, n;
    int channels = ignoreAlphaMask ? 3 : 4;
    unsigned char *imageData = stbi_load_from_memory(bufferData, bufferSize, &width, &height, &n, channels);

    ui3dsPrepareImage(targetScreen, imagePath, imageData, type, width, height, channels);
    
    if (imageData) {
        stbi_image_free(imageData);
    }
}

bool ui3dsUpdateSubtexture(SGPU_TEXTURE_ID textureId, const char* imagePath, bool isDefault) {
    if (textureId < UI_TEXTURE_START) return false;

    int idx = textureId - UI_TEXTURE_START;
    
    UiAsset* asset = isDefault ? &defaultAssets[idx] : &externalAssets[idx];

    bool isActive = (GPU3DS.textures[textureId].tex.data == asset->tex.data);
    if (isActive && strncmp(asset->path, imagePath, _MAX_PATH) == 0) {
        asset->active = true;

        return true; 
    }

    if (!ui3dsLoadPngToAsset(asset, idx, imagePath)) {
        return false;
    }

    // state swap
    // point the active render index to our custom state
    GPU3DS.textures[textureId].tex = asset->tex;
    
    return true;
}

void ui3dsRestoreDefault(SGPU_TEXTURE_ID textureId) {
    if (textureId != UI_BORDER && textureId != UI_BEZEL && textureId != UI_COVER) {
        return;
    }

    log3dsWrite("[impl3dsUpdateUiAssets] restore default asset for ID %d", textureId);

    int idx = textureId - UI_TEXTURE_START;
    UiAsset* asset = &externalAssets[idx];

    // Reset metadata
    asset->path[0] = '\0';
    asset->active = false;

    if (defaultAssets[idx].tex.data != NULL) {
        GPU3DS.textures[textureId].tex = defaultAssets[idx].tex;
    }
}

void ui3dsDrawSubTexture(SGPU_TEXTURE_ID textureId, const Tex3DS_SubTexture* subTexture, 
    int sx0, int sy0, int width, int height, u32 overlayColor, float scaleX, float scaleY) 
{
    if (!subTexture) return;

    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    SGPUTexture *texture = &GPU3DS.textures[textureId];

    // 0.5f to avoid subpixel issues
    int sx1 = sx0 + (int)(width * scaleX + 0.5f);
    int sy1 = sy0 + (int)(height * scaleY + 0.5f);

    gpu3dAddSubTextureQuadVertexes(sx0, sy0, sx1, sy1, subTexture, width, height, texture->tex.width, texture->tex.height, 0, overlayColor);

	SGPURenderState renderState = GPU3DS.currentRenderState;
    renderState.textureBind = textureId;
	renderState.textureEnv = overlayColor == 0 ? TEX_ENV_REPLACE_TEXTURE0 : TEX_ENV_BLEND_COLOR_TEXTURE0;

	gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TEXTURE_BIND | FLAG_TEXTURE_ENV, &renderState);
    gpu3dsDraw(list, NULL, list->count);
}

void addVerticalShadow(int x0, int width, int color1, int color2) {
    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    SQuadVertex *vertices = (SQuadVertex *) list->data + list->from + list->count;

    int z = 0;
    int y0 = 0;
	int x1 = x0 + width;
	int y1 = y0 + SCREEN_HEIGHT;
	vertices[0].Position = (SVector4i){x0, y0, z, 1};
	vertices[1].Position = (SVector4i){x1, y0, z, 1};
	vertices[2].Position = (SVector4i){x0, y1, z, 1};

	vertices[3].Position = (SVector4i){x1, y1, z, 1};
	vertices[4].Position = (SVector4i){x0, y1, z, 1};
	vertices[5].Position = (SVector4i){x1, y0, z, 1};

	u32 colorSwapped = __builtin_bswap32(color1);
	u32 colorSwapped2 = __builtin_bswap32(color2);

    vertices[0].Color = colorSwapped2; // tl
    vertices[1].Color = colorSwapped; // tr
    vertices[2].Color = colorSwapped2; // bl

    vertices[3].Color = colorSwapped; // br
    vertices[4].Color = colorSwapped2; // bl
    vertices[5].Color = colorSwapped; // tr
    
    list->count += 6;
}


void ui3dsDrawSplash(SGPU_TEXTURE_ID textureId, float iod, float *bg1_y, float *bg2_y) {
    const Tex3DS_Texture info = textureInfo[textureId - UI_TEXTURE_START];
	const Tex3DS_SubTexture* left = Tex3DS_GetSubTexture(info, 0);
    
    u32 bg1_tint = 0x00000077;
    u32 bg2_tint = 0x00000099;
    
    if (*bg2_y <= -left->height) {
        *bg2_y = 0;
    }

    ui3dsDrawSubTexture(textureId, left, 0, (int)(*bg2_y), left->width, left->height, bg2_tint);

	const Tex3DS_SubTexture* right = Tex3DS_GetSubTexture(info, 1);
    int right_x0 = screenSettings.GameScreenWidth - right->width;
    ui3dsDrawSubTexture(textureId, right, right_x0, (int)(*bg2_y), right->width, right->height, bg2_tint);

	const Tex3DS_SubTexture* center = Tex3DS_GetSubTexture(info, 2);	
    int center_x0 = (screenSettings.GameScreenWidth - center->width) / 2;
    ui3dsDrawSubTexture(textureId, center, center_x0, (int)(*bg1_y), center->width, center->height, bg1_tint);

    int shadowWidth = 16;
    int shadow_x0 = center_x0 - shadowWidth;
    int shadow_x1 = center_x0 + center->width;
    addVerticalShadow(shadow_x0, shadowWidth, 0x000000dd, 0);
    addVerticalShadow(shadow_x1, shadowWidth, 0, 0x000000dd);

    GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_COLOR;
    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;
    GPU3DS.currentRenderStateFlags |= FLAG_ALPHA_BLENDING | FLAG_TEXTURE_ENV;

    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    gpu3dsDraw(list, NULL, list->count);

    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;
    GPU3DS.currentRenderStateFlags |= FLAG_ALPHA_BLENDING;

	const Tex3DS_SubTexture* logo = Tex3DS_GetSubTexture(info, 3);
	int logo_x0 = (screenSettings.GameScreenWidth - logo->width) / 2;
	int logo_y0 = (SCREEN_HEIGHT - logo->height) / 2;
    ui3dsDrawSubTexture(textureId, logo, logo_x0, logo_y0, logo->width, logo->height);
}


bool ui3dsDrawAsset(SGPU_TEXTURE_ID textureId, const AssetDrawContext& ctx, float scaleX, float scaleY, bool forceAlphaBlending) {
    int idx = textureId - UI_TEXTURE_START;
    bool assetIsInactive = ctx.displayMode == ASSET_NONE || (ctx.displayMode == ASSET_CUSTOM_ONLY && !externalAssets[idx].active);

    if (assetIsInactive) {
        return false;
    }

    float overlayAlpha = 1.0f - ((float)(ctx.opacity) / OPACITY_STEPS);
    u32 overlayColor = overlayAlpha <= 0 ? 0 : (u32)(overlayAlpha * 255.0f);

    int width, height;

    if (externalAssets[idx].active) {
        width = externalAssets[idx].dim.width;
        height = externalAssets[idx].dim.height;
    } else {
        width = defaultAssets[idx].dim.width;
        height = defaultAssets[idx].dim.height;
    }

    // centered
    int sx0 = (ctx.screenWidth - (scaleX * width)) / 2;
    int sy0 = (SCREEN_HEIGHT - (scaleY * height)) / 2;

    if (forceAlphaBlending) {
        GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;
        GPU3DS.currentRenderStateFlags |= FLAG_ALPHA_BLENDING;
    }
    
    ui3dsDrawSubTexture(textureId, Tex3DS_GetSubTexture(textureInfo[idx], 0), sx0, sy0, width, height, overlayColor, scaleX, scaleY);

    return true;
}

void ui3dsDrawBackground(SGPU_TEXTURE_ID textureId, bool paused) {
    const AssetDrawContext ctx = getAssetDrawContext(textureId);

    gpu3dsClearScreen(ctx.targetScreen);
    ui3dsDrawAsset(textureId, ctx, 1.0f, 1.0f, false);
}

void ui3dsDrawGameOverlay(SGPU_TEXTURE_ID textureId,int sWidth, int sHeight, bool paused) {
    const AssetDrawContext ctx = getAssetDrawContext(textureId);
    
    bool autoFitDisabled = !UI_BEZEL || !settings3DS.GameBezelAutoFit;
    float scaleX = (autoFitDisabled || sWidth == BEZEL_INNER_WIDTH) ? 1.0f : (float)sWidth * WIDTH_SCALE;
    float scaleY = (autoFitDisabled || sHeight >= SNES_HEIGHT_EXTENDED) ? 1.0f : (float)sHeight * HEIGHT_SCALE;

    bool textureDrawn = ui3dsDrawAsset(textureId, ctx, scaleX, scaleY, true);

    if (!paused) {
        return;
    }

    gpu3dsAddQuadRect(0, 0, screenSettings.GameScreenWidth, SCREEN_HEIGHT, 0, 0xaa);

    int height = 50;
    int y0 = (SCREEN_HEIGHT - height) / 2;
    gpu3dsAddQuadRect(0, y0, screenSettings.GameScreenWidth, y0 + height, 0, 0xaa);

    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];

    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_COLOR;
    GPU3DS.currentRenderStateFlags |= FLAG_TEXTURE_ENV | FLAG_ALPHA_BLENDING;
    gpu3dsDraw(list, NULL, list->count);
}

bool ui3dsNotificationIsVisible() {
    return notification.visible;
}

void ui3dsTriggerNotification(const char* text, UI_NotificationType type, double durationInMs) {
    notification.borderSize = 1;
    notification.padding = notification.borderSize + 4;
    notification.margin = 4;

    u16 textWidth = ui3dsGetStringWidth(text) + notification.padding * 2;
    u16 maxWidth = screenSettings.GameScreenWidth - notification.margin * 2;
    u16 width = textWidth <= maxWidth ? textWidth : maxWidth;
    u16 height = FONT_HEIGHT + notification.padding * 2; // single line!

    // left bottom position
    notification.x0 = notification.margin;
    notification.y0 = SCREEN_HEIGHT - notification.margin - height;
    notification.x1  = notification.x0 + width;
    notification.y1 = notification.y0 + height;
    
    u32 alpha = (u32)(0.85f * 255.0f);

    switch (type) {
        case NOTIFICATION_SUCCESS:
            notification.color = 0x13753A00 | alpha;
            break;
        case NOTIFICATION_ERROR:
            notification.color = 0xDB3B2100 | alpha;
            break;
        
        default:
            notification.color = 0x1F79D100 | alpha;
            break;
    }

    u64 durationTicks = (u64)(durationInMs * CPU_TICKS_PER_MSEC);
    notification.visibleUntil = svcGetSystemTick() + durationTicks;
    notification.visible = true;

    strncpy(notification.message, text, 63);
}

void ui3dsUpdateNotification(bool isEnabled) {
    if (!notification.visible) return;

    if (!isEnabled || svcGetSystemTick() > notification.visibleUntil) {
        notification.visible = false;
    }
}

void ui3dsDrawNotificationOverlay() {
    if (!notification.visible) return;
    
    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    
    gpu3dsAddQuadRect(
        notification.x0, notification.y0, notification.x1, notification.y1, 0, 
        notification.color, 0xffffffff, notification.borderSize);
    
    SGPURenderState renderState = GPU3DS.currentRenderState;
    renderState.textureEnv = TEX_ENV_REPLACE_COLOR;
    renderState.alphaBlending = ALPHA_BLENDING_ENABLED;
    gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TEXTURE_ENV | FLAG_ALPHA_BLENDING, &renderState);

    gpu3dsDraw(list, NULL, list->count);
}

void ui3dsDrawNotificationText() {
    if (!notification.visible) return;


    int x0 = notification.x0 + notification.padding;
    int y0 = notification.y0 + notification.padding - 1;
    int x1 = notification.x1 - notification.padding + 1;
    int y1 = notification.y1 - notification.padding;

    ui3dsDrawStringWithWrapping(screenSettings.GameScreen, x0, y0, x1, y1, 0xffffff, HALIGN_LEFT, notification.message);
}

// TODO: ideally this should be rendered by gpu
void ui3dsDrawPauseText() {

	const char* message = "\x13\x14\x15\x16\x16 \x0e\x0f\x10\x11\x12 \x17\x18 \x14\x15\x16\x19\x1a\x15";

    u16 textWidth = ui3dsGetStringWidth(message);
	u16 x0 = (screenSettings.GameScreenWidth - textWidth) / 2;
	u16 y0 = (SCREEN_HEIGHT - FONT_HEIGHT) / 2;

	ui3dsDrawStringWithNoWrapping(screenSettings.GameScreen, x0, y0, 
		x0 + textWidth, y0 + FONT_HEIGHT, 0xffffff, HALIGN_CENTER, message);
}

static void ui3dsAllocVramTexture(const char *path, SGPU_TEXTURE_ID textureId, bool vram) {
    FILE *file = fopen(path, "rb");
    if (!file) return;
    
    SGPUTexture *texture = &GPU3DS.textures[textureId];
    int idx = textureId - UI_TEXTURE_START;
    textureInfo[idx] = Tex3DS_TextureImportStdio(file, &texture->tex, NULL, vram);
    fclose(file);

    if (textureInfo[idx]) {
        texture->id = textureId;
        GPU_TEXTURE_FILTER_PARAM filter = GPU_LINEAR;
        C3D_TexSetFilter(&texture->tex, filter, filter);
        
        texture->scale[3] = 1.0f / texture->tex.width;  // x
        texture->scale[2] = 1.0f / texture->tex.height; // y
        texture->scale[1] = 0; // z
        texture->scale[0] = 0; // w

		if (settings3DS.LogFileEnabled) {
            log3dsWrite("ui vram texture \"%s\" dim: %dx%d, size:%.2fkb, format: %s",
                SGPUTextureIDToString(texture->id),
                texture->tex.width, texture->tex.height,
                (float)texture->tex.size / 1024,
                SGPUTexColorToString(texture->tex.fmt)
            );
        }

        // store the default t3x state
        if (textureId == UI_ATLAS) {
            return;
        }

        const Tex3DS_SubTexture* subTex = Tex3DS_GetSubTexture(textureInfo[idx], 0);

        // set default state for our t3x asset
        defaultAssets[idx].tex = texture->tex;
        defaultAssets[idx].active = true; 
        defaultAssets[idx].dim.width = subTex->width;
        defaultAssets[idx].dim.height = subTex->height;
        defaultAssets[idx].path[0] = '\0'; // no PNG path for internal t3x
        
        const char* folderName = NULL;

        switch (textureId) {
            case UI_BORDER: folderName = "borders"; break;
            case UI_BEZEL:  folderName = "bezels";  break;
            default:        folderName = "covers";  break;
        }

        char overridePath[_MAX_PATH];
        snprintf(overridePath, _MAX_PATH, "sdmc:/3ds/snes9x_3ds/%s/_default.png", folderName);
        log3dsWrite("[ui3ds] look for default texture override in directory %s", overridePath);
        ui3dsUpdateSubtexture(textureId, overridePath, true); 
    }
}

bool ui3dsAllocVramTextures() {
    ui3dsAllocVramTexture("romfs:/gfx/border.t3x", UI_BORDER, true);
    ui3dsAllocVramTexture("romfs:/gfx/bezel.t3x", UI_BEZEL, true);
    ui3dsAllocVramTexture("romfs:/gfx/cover.t3x", UI_COVER, true);
    ui3dsAllocVramTexture("romfs:/gfx/atlas.t3x", UI_ATLAS, true);
    
    // skip UI_ATLAS, we won't update this texture
    for(int i=0; i < UI_TEX_COUNT - 1; i++) {
        SGPU_TEXTURE_ID id = SGPU_TEXTURE_ID(i + UI_TEXTURE_START);
        SGPUTexture *texture = &GPU3DS.textures[id];

        int width  = texture->tex.width;
        int height = texture->tex.height;

        if (!width || !height) {
            log3dsWrite("[ui3dsLoadTextures] texture not set %s", SGPUTextureIDToString(id));

            return false;
        }

        if (!C3D_TexInitVRAM(
            &externalAssets[i].tex, 
            width,
            height,
            texture->tex.fmt
        )) {
            log3dsWrite("[ui3dsLoadTextures] C3D_TexInit failed for idx %d (%dx%d)", i, width, height);
            return false;
        }

        C3D_TexSetFilter(&externalAssets[i].tex, GPU_LINEAR, GPU_LINEAR);
    }

    return true;
}

bool ui3dsAllocTextureBuffers() {
    // largest possible image size * 4 bpp (RGBA8)
    imageBufferSize = 512 * 256 * 4;
    log3dsWrite("allocate ui texture buffers (2x %.2fkb)", float(imageBufferSize) / 1024);
    
    stagingBuffer = (u8*)linearAlloc(imageBufferSize);
    texUploadBuffer = (u8*)linearAlloc(imageBufferSize);

    return (stagingBuffer && texUploadBuffer);
}

bool ui3dsSaveScreenRegion(const char* path, 
    int width, int height, int x0, int y0, gfxScreen_t screen, bool isTopStereo) {
    // sync frame buffer
    if (GPU3DS.gpuSwapPending) {
		gspWaitForEvent(GSPGPU_EVENT_PPF, GPU3DS.isReal3DS);
    	gfxScreenSwapBuffers(screen, isTopStereo);

		GPU3DS.gpuSwapPending = false;
	}

    if (!captureRegionToBuffer(width, height, x0, y0, screen)) {
        return false;
    }

    // We access stagingBuffer directly here 
    // because it's already modified in captureRegionToBuffer
    bool success = savePng(path, width, height, stagingBuffer);

    return success;
}

void ui3dsFinalize() {
    for (int i = 0; i < UI_TEX_COUNT; i++) {
        if (textureInfo[i]) {
            Tex3DS_TextureFree(textureInfo[i]);
            textureInfo[i] = NULL;
        }
        
        // because UI_ATLAS is not part of externalAssets
        if (i < UI_TEX_COUNT - 1) {
            C3D_TexDelete(&externalAssets[i].tex);
        }
    }

    linearFree(stagingBuffer);
    linearFree(texUploadBuffer);
    stagingBuffer = NULL;
    texUploadBuffer = NULL;
}