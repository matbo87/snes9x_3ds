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

#include "3dsgpu.h"
#include "3dsui.h"
#include "3dsfont.cpp"
#include "3dssettings.h"

#include <stb_image.h>

#define MAX_ALPHA 8

typedef struct
{
    int red[MAX_ALPHA + 1][32];
    int green[MAX_ALPHA + 1][32];
    int blue[MAX_ALPHA + 1][32];
} SAlpha;

SAlpha alphas;

dialog_state secondScreenDialogState = HIDDEN;

int foreColor = 0xffffff;
int backColor = 0x000000;

int translateX = 0;
int translateY = 0;
int viewportX1, viewportY1, viewportX2, viewportY2;

int fontHeight = 13;

int viewportStackCount = 0;
int viewportStack[20][4];

int bounds[10];

uint8 *fontWidthArray[] = { fontTempestaWidth, fontRondaWidth, fontArialWidth };
uint8 *fontBitmapArray[] = { fontTempestaBitmap, fontRondaBitmap, fontArialBitmap };

uint8 *fontBitmap;
uint8 *fontWidth;

bool screenSwapped = false;

//---------------------------------------------------------------
// Initialize this library
//---------------------------------------------------------------

int ui3dsGetScreenWidth(gfxScreen_t targetScreen) {
    return (targetScreen == GFX_TOP) ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH;
}

// this is called on init and if game screen has swapped
void ui3dsUpdateScreenSettings(gfxScreen_t gameScreen) {
	screenSettings.GameScreen = gameScreen;
	screenSettings.SecondScreen = (screenSettings.GameScreen == GFX_TOP) ? GFX_BOTTOM : GFX_TOP;
	screenSettings.GameScreenWidth  = ui3dsGetScreenWidth(screenSettings.GameScreen);
    screenSettings.SecondScreenWidth  = ui3dsGetScreenWidth(screenSettings.SecondScreen);
    
    // reset menu viewport
    viewportX1 = 0;
    viewportY1 = 0;
    viewportX2 = screenSettings.SecondScreenWidth;
    viewportY2 = SCREEN_HEIGHT;
    viewportStackCount = 0;
}

void ui3dsInitialize()
{
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

        fontWidth[1] = 10;
        fontWidth[10] = 0;
        fontWidth[13] = 0;
        fontWidth[248] = 10;
        fontWidth[249] = 10;
        fontWidth[250] = 9;
        fontWidth[251] = 1;
        fontWidth[253] = 10;
        fontWidth[254] = 10;
        fontWidth[255] = 7;
    }

    fontBitmap = fontTempestaBitmap;
    fontWidth = fontTempestaWidth;

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

void ui3dsDraw32BitChar(uint32 *frameBuffer, int x, int y, int color, uint8 c)
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
                               ui3dsApplyAlphaToColor(frameBuffer[fi], 1.0 - alpha, true);
                    
                    frameBuffer[fi] = newColor;  
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
void ui3dsDrawStringOnly(gfxScreen_t targetScreen, uint16 *fb, int absoluteX, int absoluteY, int color, const char *buffer, int startPos = 0, int endPos = 0xffff)
{
    int x = absoluteX;
    int y = absoluteY;

    if (color < 0)
        return;
    if (y >= viewportY1 - 16 && y <= viewportY2)
    {
        
        bool color565 = gfxGetScreenFormat(targetScreen) == GSP_RGB565_OES;

        color = color565 ? CONVERT_TO_565(color) : color;
        for (int i = startPos; i <= endPos; i++)    
        {
            uint8 c = buffer[i];
            if (c == 0)
                break;
            if (c != 32)
                if (color565)
                    ui3dsDrawChar(fb, x, y, color, c);
                else
                    ui3dsDraw32BitChar((uint32 *)fb, x, y, color, c);
            x += fontWidth[c];
        }
    }
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

        uint16* fb = (uint16 *) gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL);
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

            ui3dsDrawStringOnly(targetScreen, fb, x, y0, color, buffer, strLineStart[i], strLineEnd[i]);
            y0 += 12;
        }
    }

    ui3dsPopViewport();
}


//---------------------------------------------------------------
// Draws a string with the forecolor, with no wrapping
//---------------------------------------------------------------
void ui3dsDrawStringWithNoWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer)
{
    x0 += translateX;
    x1 += translateX;
    y0 += translateY;
    y1 += translateY;
    
    ui3dsPushViewport(x0, y0, x1, y1);
   
    if (buffer != NULL)
    {
        uint16* fb = (uint16 *) gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL);
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
        ui3dsDrawStringOnly(targetScreen, fb, x, y0, color, buffer);
    }

    ui3dsPopViewport();
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
void ui3dsDrawImage(T *fb, gfxScreen_t targetScreen, Bounds bounds, unsigned char *imageData, int channels, float alpha, Border border, const char *errorMessage, int factor) {
    int screenWidth = ui3dsGetScreenWidth(targetScreen);
    int imageWidth = bounds.right - bounds.left;
    int imageHeight = bounds.top - bounds.bottom;
    
    // handle out of bounds (e.g. 400x240 pixel image on bottom screen)
    int x0 = imageWidth > screenWidth ? bounds.left : bounds.left < 0 ? 0 : bounds.left;
    int x1 = bounds.right > screenWidth ? screenWidth : bounds.right;
    int y0 = bounds.top < 0 ? 0 : bounds.top; 
    int y1 = bounds.bottom > SCREEN_HEIGHT ? SCREEN_HEIGHT : bounds.bottom;
    
    bool isRGB565 = gfxGetScreenFormat(targetScreen) == GSP_RGB565_OES;
    unsigned char opacity = (int)(alpha * 255);
    unsigned char a = 255;

    // TODO: ui3dsDrawRect shouldn't be exclusive for SecondScreen
    if (targetScreen == screenSettings.SecondScreen) {
        if (sizeof(border) > 0 && targetScreen == screenSettings.SecondScreen) {
            ui3dsDrawRect(x0 - border.width, y0 - border.width, x1 + border.width, y1 + border.width, border.color);
        }

        if (!imageData) {
            goto noImage;
        }
    }

    for (int x = x0; x < x1; x++) {
        int fbofs = (x) * SCREEN_HEIGHT + (239 - y0);
        for (int y = y0; y < y1; y++) {
            int src_index = ((y - y0) / factor * (imageWidth / factor) + ((x - x0) / factor)) * channels;
            unsigned char r = imageData[src_index];
            unsigned char g = imageData[src_index + 1];
            unsigned char b = imageData[src_index + 2];

            if (channels == 4) {
                a = imageData[src_index + 3];
            }
            
            fb[fbofs--] = ui3dsBlendingColor(fb[fbofs], r, g, b, a, opacity, isRGB565);
        }
    }

    return;

    noImage:
        if (errorMessage) {
            Bounds mBounds = ui3dsGetBounds(screenWidth, screenWidth - 12, 28, Position::MC, 0, 0); 
            ui3dsDrawStringWithWrapping(targetScreen, mBounds.left, mBounds.top, mBounds.right, mBounds.bottom, 0xbbbbbb, HALIGN_CENTER, errorMessage);
        }
}

void ui3dsPrepareImage(gfxScreen_t targetScreen, const char *imagePath, unsigned char *imageData, IMAGE_TYPE type, int width, int height, int channels) {
    int screenWidth = ui3dsGetScreenWidth(targetScreen);
    std::string message;

    // default image properties
    Border border = { 0, NULL };
    Position position = Position::MC;
    float alpha = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
    int scaleFactor = 1;

    // override properties based on image type
    if (type == IMAGE_TYPE::PREVIEW) {
        border.width = 3;
        border.color = 0xFFFFFF;
        position = Position::BR;
        offsetX = border.width;
        offsetY = border.width + 20;
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
        ui3dsDrawImage<uint16>((uint16 *) gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL), targetScreen, bounds, imageData, channels, alpha, border, message.c_str(), scaleFactor);    
    }
    else {
        ui3dsDrawImage<uint32>((uint32 *) gfxGetFramebuffer(targetScreen, GFX_LEFT, NULL, NULL), targetScreen, bounds, imageData, channels, alpha, border, message.c_str(), scaleFactor);        
    }
}

// render image from path
void ui3dsRenderImage(gfxScreen_t targetScreen, const char *imagePath, IMAGE_TYPE type, bool ignoreAlphaMask) {
    int width, height, n;
    std::string message;
    int channels = ignoreAlphaMask ? 3 : 4;
     
   // bool imgFileChanged = strncmp(rgb8Image.File.c_str(), imgFilePath, _MAX_PATH) != 0;
    //if (imgFileChanged) {
    //    success = ui3dsConvertImage(imgFilePath);
    //}
    
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

dialog_state ui3dsGetSecondScreenDialogState() {
    return secondScreenDialogState;
}

void ui3dsSetSecondScreenDialogState(dialog_state state) {
    secondScreenDialogState = state;
}

bool ui3dsGetScreenSwapped() {
    return screenSwapped;
}

void ui3dsSetScreenSwapped(bool swapped) {
    screenSwapped = swapped;
}
