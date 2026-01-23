
#ifndef _3DSUI_H_
#define _3DSUI_H_

#include <3ds.h>
#include <string>
#include <cstdint>

#include "3dsgpu.h"
#include "3dsthemes.h"

typedef struct Bounds {
    int left;
    int top;
    int right;
    int bottom;
};

typedef struct ImageBorder {
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

typedef enum {
    NOTIFICATION_DEFAULT,
    NOTIFICATION_SUCCESS,
    NOTIFICATION_ERROR,
} UI_NotificationType;

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
void ui3dsDrawCheckerboard(int x0, int y0, int x1, int y1, int color1, int color2);
int ui3dsOverlayBlendColor(int backgroundColor, int foregroundColor);
void ui3dsDraw32BitRect(uint32 * fb, int x0, int y0, int x1, int y1, int color, float alpha = 1.0f);

void ui3dsDrawStringWithWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);
int ui3dsDrawStringWithNoWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);

void ui3dsCopyFromFrameBuffer(uint16 *destBuffer);
void ui3dsBlitToFrameBuffer(uint16 *srcBuffer, float alpha = 1.0f);
void ui3dsRenderImage(gfxScreen_t targetScreen, const char *imagePath, IMAGE_TYPE type, bool ignoreAlphaMask = true);
void ui3dsRenderImage(gfxScreen_t targetScreen, const char *imagePath,  unsigned char *imageData, int bufferSize, IMAGE_TYPE type, bool ignoreAlphaMask = true);

int ui3dsGetScreenWidth(gfxScreen_t targetScreen);
Bounds ui3dsGetBounds(int screenWidth, int width, int height, Position position, int offsetX, int offsetY);

bool ui3dsAllocVramTextures();
bool ui3dsAllocTextureBuffers();
void ui3dsDrawSubTexture(SGPU_TEXTURE_ID textureId, const Tex3DS_SubTexture* subTexture, int sx0, int sy0, int sx1, int sy1, u32 overlayColor = 0, float scaleX = 1.0f, float scaleY = 1.0f);
bool ui3dsUpdateSubtexture(SGPU_TEXTURE_ID textureId, const char* imagePath, bool isDefault = false);
void ui3dsRestoreDefault(SGPU_TEXTURE_ID textureId);

void ui3dsDrawPauseText();
void ui3dsDrawSplash(SGPU_TEXTURE_ID textureId, float iod, float *bg1_y, float *bg2_y);
void ui3dsDrawBackground(SGPU_TEXTURE_ID textureId, bool paused = false);
void ui3dsDrawGameOverlay(SGPU_TEXTURE_ID textureId, int sWidth, int sHeight, bool paused = false);

void ui3dsTriggerNotification(const char* text, UI_NotificationType type = NOTIFICATION_DEFAULT, double durationInMs = 1200);
void ui3dsUpdateNotification(bool isEnabled);
void ui3dsDrawNotificationOverlay();
void ui3dsDrawNotificationText();
bool ui3dsNotificationIsVisible();

bool ui3dsSaveScreenRegion(const char* path, int width, int height, int x0, int y0, gfxScreen_t screen, bool isTopStereo = false);
void ui3dsFinalize();

#define HALIGN_LEFT     -1
#define HALIGN_CENTER   0
#define HALIGN_RIGHT    1

#define FONT_HEIGHT     13
#define PADDING         10

extern int bounds[10];

#endif