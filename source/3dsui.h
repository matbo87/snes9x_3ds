
#ifndef _3DSUI_H_
#define _3DSUI_H_

#include <3ds.h>
#include <string>
#include <cstdint>
#include "3dsthemes.h"

typedef struct
{
	uint32_t 		*PixelData;
	std::string     File;
	uint16_t        Width;
	uint16_t        Height;
	int8_t        	ParallaxOffset;
	int8_t			Layer;
} RGB8Image;

typedef struct Bounds {
    int left;
    int top;
    int right;
    int bottom;
};

typedef struct Border {
    int width;
    int color;
};

enum class Position {
    TL,
    TC,
    TR,
    ML,
    MC,
    MR,
    BL,
    BC,
    BR,
};

enum class IMAGE_TYPE {
    START_SCREEN,
    PREVIEW,
    COVER,
    LOGO,
};

typedef enum
{
    HIDDEN = 0,
	VISIBLE = 1,
	WAIT = 2,
 } dialog_state;


void ui3dsUpdateScreenSettings(gfxScreen_t gameScreen);
void ui3dsInitialize();
void ui3dsSetFont(int fontIndex);

void ui3dsSetViewport(int x1, int y1, int x2, int y2);
void ui3dsPushViewport(int x1, int y1, int x2, int y2);
void ui3dsPopViewport();
void ui3dsSetTranslate(int tx, int ty);

void ui3dsSetColor(int newForeColor, int newBackColor);
int ui3dsApplyAlphaToColor(int color, float alpha, bool rgb8 = false);

void ui3dsDrawRect(int x0, int y0, int x1, int y1);
void ui3dsDrawRect(int x0, int y0, int x1, int y1, int color, float alpha = 1.0f);
void ui3dsDraw32BitRect(uint32 * fb, int x0, int y0, int x1, int y1, int color, float alpha = 1.0f);

void ui3dsDrawStringWithWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);
void ui3dsDrawStringWithNoWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);

void ui3dsCopyFromFrameBuffer(uint16 *destBuffer);
void ui3dsBlitToFrameBuffer(uint16 *srcBuffer, float alpha = 1.0f);
void ui3dsRenderImage(gfxScreen_t targetScreen, const char *imagePath, IMAGE_TYPE type, bool ignoreAlphaMask = true);
void ui3dsRenderImage(gfxScreen_t targetScreen, const char *imagePath,  unsigned char *imageData, int bufferSize, IMAGE_TYPE type, bool ignoreAlphaMask = true);

void ui3dsSetSecondScreenDialogState(dialog_state state);
dialog_state ui3dsGetSecondScreenDialogState();

int ui3dsGetScreenWidth(gfxScreen_t targetScreen);
Bounds ui3dsGetBounds(int screenWidth, int width, int height, Position position, int offsetX, int offsetY);

#define HALIGN_LEFT     -1
#define HALIGN_CENTER   0
#define HALIGN_RIGHT    1

#define FONT_HEIGHT     13
#define PADDING         10

extern int bounds[10];

#endif