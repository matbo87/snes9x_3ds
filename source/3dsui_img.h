#ifndef _3DS_UI_IMG_H_
#define _3DS_UI_IMG_H_

#include <3ds.h>
#include "3dsgpu.h"

bool img3dsAllocVramTextures();
void img3dsDrawSubTexture(SGPU_TEXTURE_ID textureId, const Tex3DS_SubTexture* subTexture, float sx0, float sy0, u16 width, u16 height, u32 overlayColor = 0, float scaleX = 1.0f, float scaleY = 1.0f);
bool img3dsLoadAsset(SGPU_TEXTURE_ID textureId, const char* path = NULL);

void img3dsDrawSplash(SGPU_TEXTURE_ID textureId, bool renderRightEye, float xOffset, float fade = 1.0f);
void img3dsDrawPause(SGPU_TEXTURE_ID textureId, float xOffset = 0.0f,
                     float opacity = 1.0f, float yOffset = 0.0f);
void img3dsDrawBackground(SGPU_TEXTURE_ID textureId, bool paused = false, float xOffset = 0.0f);
void img3dsDrawGameOverlay(SGPU_TEXTURE_ID textureId, int sWidth, int sHeight);
void img3dsDrawScanlines(float sx0, float sy0, float sx1, float sy1, int sWidth, int cHeight);
void img3dsUpdateScanlineTexture();

// switch between Cache Files (e.g. Boxart -> Title)
// closes old file, opens new one, reloads index
void img3dsOpenThumbnailCache();

// search for a game image based on the ROM filename
// e.g. "Super Mario (USA).sfc" -> hashes "Super Mario" -> loads image
bool img3dsLoadThumb(const char* fullRomName);

bool img3dsLoadStateScreenshot(const char* path);
void img3dsInvalidateStateScreenshot();

void img3dsDrawThumb(int offsetRight, int offsetBottom);
int img3dsGetThumbHeight();
int img3dsGetThumbWidth();

// Software-blits a pre-swizzled RGB565 image onto SecondScreen at (x, y).
// Crops inset pixels from every side.
void img3dsDrawSwizzledRgb565(const u16* src, int width, int height, int x, int y, int inset = 0);

void img3dsSwizzleRgba8ToRgb565(u16* dst, const u32* src, int width, int height);
void img3dsUnswizzleRgb565(u16* dst, int dstStride, const u16* src, int width, int height);

bool img3dsSaveScreenRegion(const char* path, int width, int height, int x0, int y0, gfxScreen_t screen, bool isWide = false);

bool img3dsInitialize();
void img3dsFinalize();

#endif
