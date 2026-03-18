//=============================================================================
// Basic user interface framework for low-level drawing operations to
// the bottom screen.
//=============================================================================

#include <cstdio>
#include <cstring>

#include "snes9x.h"

#include "3dssettings.h"
#include "3dslog.h"
#include "3dsfiles.h"
#include "3dsfont.h"
#include "3dsui.h"

#define MAX_ALPHA 8
#define GETFONTBITMAP(c, x, y) fontBitmap[c * 256 + x + (y) * 16]

typedef struct
{
    int red[MAX_ALPHA + 1][32];
    int green[MAX_ALPHA + 1][32];
    int blue[MAX_ALPHA + 1][32];
} SAlpha;


static u8 *fontWidthArray[] = { fontTempestaWidth, fontRondaWidth, fontArialWidth };
static u8 *fontBitmapArray[] = { fontTempestaBitmap, fontRondaBitmap, fontArialBitmap };

static u8 *fontBitmap;
static u8 *fontWidth;
static int fontHeight = FONT_HEIGHT;

static int translateX = 0;
static int translateY = 0;
static int viewportX1, viewportY1, viewportX2, viewportY2;

static int viewportStackCount = 0;
static int viewportStack[20][4];

static SAlpha alphas;
static u16 alphas4Bit[MAX_ALPHA + 1];

// shared between 3dsui_img and 3dsui_notif — never use concurrently
u8* g_texUploadBuffer;


void ui3dsPrepare()
{
    for (int a = 0; a <= MAX_ALPHA; a++) {
        alphas4Bit[a] = (a * 15 + 4) >> 3;
    }

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

    for (int f = 0; f < 3; f++)
    {
        fontBitmap = fontBitmapArray[f];
        fontWidth = fontWidthArray[f];
        for (int i = 0; i < 65536; i++)
        {
            u8 c = fontBitmap[i];
            if (c == ' ')
                fontBitmap[i] = 0;
            else
                fontBitmap[i] = c - '0';
        }
    }

    ui3dsSetFont();
    ui3dsSetScreenLayout();
}
    
void ui3dsSetScreenLayout() {
    if (settings3DS.GameScreen == GFX_TOP) {
	    settings3DS.GameScreenWidth = SCREEN_TOP_WIDTH;
	    settings3DS.SecondScreen = GFX_BOTTOM;
        settings3DS.SecondScreenWidth = SCREEN_BOTTOM_WIDTH;
    } else {
	    settings3DS.GameScreenWidth = SCREEN_BOTTOM_WIDTH;
	    settings3DS.SecondScreen = GFX_TOP;
        settings3DS.SecondScreenWidth = SCREEN_TOP_WIDTH;
    }
    
    viewportX1 = 0;
    viewportY1 = 0;
    viewportX2 = settings3DS.SecondScreenWidth;
    viewportY2 = SCREEN_HEIGHT;
    viewportStackCount = 0;
}

void ui3dsSetFont()
{
    int fontIndex = static_cast<int>(settings3DS.Font);

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
    if (viewportX2 > settings3DS.SecondScreenWidth) viewportX2 = settings3DS.SecondScreenWidth;
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
inline u16 __attribute__((always_inline)) ui3dsApplyAlphaToColour565(int color565, int alpha)
{
    int red = (color565 >> 11) & 0x1f;
    int green = (color565 >> 6) & 0x1f; // drop the LSB of the green colour
    int blue = (color565) & 0x1f;

    return alphas.red[alpha][red] | alphas.blue[alpha][blue] | alphas.green[alpha][green];
}


inline u16 __attribute__((always_inline)) ui3dsBlendPixel565(u16 fg, u16 bg, int alpha)
{
    return ui3dsApplyAlphaToColour565(fg, alpha) + ui3dsApplyAlphaToColour565(bg, MAX_ALPHA - alpha);
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
inline u16 __attribute__((always_inline)) ui3dsGetPixelInline(u16 *frameBuffer, int x, int y)
{
    return frameBuffer[ui3dsComputeFrameBufferOffset((x), (y))];  
}


//---------------------------------------------------------------
// Sets a pixel colour.
//---------------------------------------------------------------
inline void __attribute__((always_inline)) ui3dsSetPixelInline(u16 *frameBuffer, int x, int y, int color)
{
    if (color < 0) return;
    if ((x) >= viewportX1 && (x) < viewportX2 && 
        (y) >= viewportY1 && (y) < viewportY2) 
    { 
        frameBuffer[ui3dsComputeFrameBufferOffset((x), (y))] = color;  
    }
}


inline u16 color32toRGBA4(u32 color, u8 alphaIndex)
{
    u32 r = (color >> 24) & 0xFF; // Top byte
    u32 g = (color >> 16) & 0xFF; 
    u32 b = (color >> 8)  & 0xFF; 

    u16 r4 = r >> 4;
    u16 g4 = g >> 4;
    u16 b4 = b >> 4;
    
    // We ignore the 32-bit alpha (color & 0xFF) here
    // instead we use one of those MAX_ALPHA + 1 alpha values from alphas4Bit
    if (alphaIndex > MAX_ALPHA) alphaIndex = MAX_ALPHA;
    u16 a4 = alphas4Bit[alphaIndex];
    
    return (r4 << 12) | (g4 << 8) | (b4 << 4) | a4;
}

//---------------------------------------------------------------
// Draws a single character to the screen
//---------------------------------------------------------------
void ui3dsDrawRGB565_CharToFramebuffer(u16 *frameBuffer, int x, int y, int color565, u8 c)
{
    // Draws a character to the screen at (x,y) 
    // (0,0) is at the top left of the screen.
    //
    int wid = fontWidth[c];
    u8 alpha;

    if ((y) >= viewportY1 && (y) < viewportY2)
    {
        for (int x1 = 0; x1 < wid; x1++)
        {
            #define SETPIXELFROMBITMAP(y1) \
                alpha = GETFONTBITMAP(c,x1,y1); \
                ui3dsSetPixelInline(frameBuffer, cx, cy + y1, \
                alpha == MAX_ALPHA ? color565 : \
                alpha == 0x0 ? -1 : \
                    ui3dsBlendPixel565(color565, ui3dsGetPixelInline(frameBuffer, cx, cy + y1), alpha));

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

int ui3dsDrawRGBA4_CharToTexture(u16 *buffer, u8 c, int xStart, int yStart, int xMax, int yMax,  u16 color)
{
    if (c == 0) return 0;
    if (c == ' ') return fontWidth[' '];

    int charWidth = fontWidth[c];

    if ((xStart + charWidth > xMax) || (yStart + fontHeight > yMax)) return 0;

    u16* dstPtr = &buffer[(yStart * xMax) + xStart];
    int dstStride = xMax - charWidth;

    for (int y = 0; y < FONT_HEIGHT; y++)
    {
        for (int x = 0; x < charWidth; x++)
        {
            u8 alpha = GETFONTBITMAP(c, x, y);

            if (alpha > 0) {
                u16 a4 = alphas4Bit[alpha];
                *dstPtr = color | a4;
            }
            
            dstPtr++;
        }

        dstPtr += dstStride;
    }

    return charWidth;
}

//---------------------------------------------------------------
// Computes width of the string
//---------------------------------------------------------------
int ui3dsGetStringWidth(const char *s, int startPos, int endPos)
{
    int totalWidth = 0;
    for (int i = startPos; i <= endPos; i++)
    {
        u8 c = s[i];
        if (c == 0)
            break;
        totalWidth += fontWidth[c];
    }   
    return totalWidth;
}

#define CONVERT_TO_565(x)    (((x & 0xf8) >> 3) | (((x >> 8) & 0xf8) << 3) | (((x >> 16) & 0xf8) << 8))

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
    
    if (alpha <= 0) return;
    
    if (alpha > 1.0f) alpha = 1.0f;
        
    u16* fb = (u16 *) gfxGetFramebuffer(settings3DS.SecondScreen, GFX_LEFT, NULL, NULL);

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
    else
    {
        int iAlpha = alpha * MAX_ALPHA;
        for (int x = x0; x < x1; x++)
        {
            int fbofs = (x) * SCREEN_HEIGHT + (239 - y0);
            for (int y = y0; y < y1; y++)
            {
                fb[fbofs] = ui3dsBlendPixel565(color, fb[fbofs], iAlpha);
                fbofs--;
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
        
    u16* fb = (u16 *) gfxGetFramebuffer(settings3DS.SecondScreen, GFX_LEFT, NULL, NULL);

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

// RGBA4 only
// returns full length of the string
int ui3dsDrawStringToTexture(u16 *textureBuffer, const char *text, int x, int y, int xMax, int yMax, u32 color)
{
    if (!text || (x > xMax) || (y + fontHeight > yMax)) return x;

    u16 color_rgba4 = color32toRGBA4(color, 0);    
    int i = 0;
    
    while (text[i] != 0)
    {
        int w = ui3dsDrawRGBA4_CharToTexture(textureBuffer, text[i], x, y, xMax, yMax, color_rgba4);
        
        if (w == 0) break; 

        x += w;
        i++;
    }

    return x;
}

//---------------------------------------------------------------
// Draws a string at the given position without translation.
//---------------------------------------------------------------
int ui3dsDrawRGB565_StringToFramebuffer(gfxScreen_t targetScreen, int absoluteX, int absoluteY, int color, const char *buffer, int startPos = 0, int endPos = 0xffff)
{
    int x = absoluteX;
    int y = absoluteY;

    if (color < 0)
        return x;
    if (y >= viewportY1 - 16 && y <= viewportY2)
    {
        u16 color565 = CONVERT_TO_565(color);
        u16 *fb = (u16 *)gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL);

        for (int i = startPos; i <= endPos; i++)    
        {
            u8 c = buffer[i];
            if (c == 0) break;
            if (c != ' ')
                ui3dsDrawRGB565_CharToFramebuffer(fb, x, y, color565, c);
            x += fontWidth[c];
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

            ui3dsDrawRGB565_StringToFramebuffer(targetScreen, x, y0, color, buffer, strLineStart[i], strLineEnd[i]);
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
        xEndPosition = ui3dsDrawRGB565_StringToFramebuffer(targetScreen, x, y0, color, buffer);
    }

    ui3dsPopViewport();

    return xEndPosition;
}

bool ui3dsInitialize()
{
    log3dsWrite("allocate tex upload buffer (%.2fkb)", float(MAX_IO_BUFFER_SIZE) / 1024);
    g_texUploadBuffer = (u8*)linearAlloc(MAX_IO_BUFFER_SIZE);

    if (!g_texUploadBuffer) {
        return false;
    }

    memset(g_texUploadBuffer, 0, MAX_IO_BUFFER_SIZE);
    
    return true;
}

void ui3dsFinalize() {
    log3dsWrite("dealloc tex upload buffer");
    linearFree(g_texUploadBuffer);
}
