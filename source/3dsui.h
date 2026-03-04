
#ifndef _3DSUI_H_
#define _3DSUI_H_

#include <3ds.h>

#define HALIGN_LEFT     -1
#define HALIGN_CENTER   0
#define HALIGN_RIGHT    1

#define FONT_HEIGHT     13
#define PADDING         10

#define DIV255(x) (((x) + 1 + ((x) >> 8)) >> 8)

// covers the largest possible UI texture (512x256 RGBA8)
extern u8* g_texUploadBuffer; 

inline int __attribute__((always_inline)) ui3dsApplyAlphaToColor(int color, float alpha)
{
    if (alpha < 0.0f) alpha = 0.0f;
    else if (alpha > 1.0f) alpha = 1.0f;
    int a = static_cast<int>(alpha * 255.0f);
    int rb = (color & 0xFF00FF) * a;
    int g  = (color & 0x00FF00) * a;

    return ((rb >> 8) & 0xFF00FF) | ((g >> 8) & 0x00FF00);
}

// overlay blending mode: returns a color in RGB888 format
// will provide more vibrant colors
inline int __attribute__((always_inline)) ui3dsOverlayBlendColor(int backgroundColor, int foregroundColor) {
    int bR = (backgroundColor >> 16) & 0xFF;
    int bG = (backgroundColor >> 8) & 0xFF;
    int bB = backgroundColor & 0xFF;
    
    int fR = (foregroundColor >> 16) & 0xFF;
    int fG = (foregroundColor >> 8) & 0xFF;
    int fB = foregroundColor & 0xFF;

    int r = (bR < 128) ? DIV255(2 * bR * fR) : 255 - DIV255(2 * (255 - bR) * (255 - fR));
    int g = (bG < 128) ? DIV255(2 * bG * fG) : 255 - DIV255(2 * (255 - bG) * (255 - fG));
    int b = (bB < 128) ? DIV255(2 * bB * fB) : 255 - DIV255(2 * (255 - bB) * (255 - fB));
    
    return (r << 16) | (g << 8) | b;
}


void ui3dsPrepare();
void ui3dsSetFont();
void ui3dsSetScreenLayout();

void ui3dsSetViewport(int x1, int y1, int x2, int y2);
void ui3dsPushViewport(int x1, int y1, int x2, int y2);
void ui3dsPopViewport();
void ui3dsSetTranslate(int tx, int ty);

void ui3dsDrawRect(int x0, int y0, int x1, int y1, int color, float alpha = 1.0f);
void ui3dsDrawCheckerboard(int x0, int y0, int x1, int y1, int color1, int color2);

void ui3dsDrawStringWithWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);
int ui3dsDrawStringWithNoWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);

int ui3dsDrawStringToTexture(u16 *textureBuffer, const char *text, int x, int y, int xMax, int yMax, u32 color);

bool ui3dsInitialize();
void ui3dsFinalize();

#endif