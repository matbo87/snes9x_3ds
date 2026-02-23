#ifndef _3DS_UI_IMG_H_
#define _3DS_UI_IMG_H_

#include <3ds.h>
#include "3dsgpu.h"

bool img3dsAllocVramTextures();
void img3dsDrawSubTexture(SGPU_TEXTURE_ID textureId, const Tex3DS_SubTexture* subTexture, int sx0, int sy0, int sx1, int sy1, u32 overlayColor = 0, float scaleX = 1.0f, float scaleY = 1.0f);
bool img3dsUpdateSubtexture(SGPU_TEXTURE_ID textureId, const char* imagePath, bool isDefault = false);
void img3dsRestoreDefaultAsset(SGPU_TEXTURE_ID textureId);

void img3dsDrawSplash(SGPU_TEXTURE_ID textureId, float iod, float *bg1_y, float *bg2_y);
void img3dsDrawBackground(SGPU_TEXTURE_ID textureId, bool paused = false);
void img3dsDrawGameOverlay(SGPU_TEXTURE_ID textureId, int sWidth, int sHeight, bool paused = false);

// switch between Cache Files (e.g. Boxart -> Title)
// closes old file, opens new one, reloads index
void img3dsSetThumbMode();

// search for a game image based on the ROM filename
// e.g. "Super Mario (USA).sfc" -> hashes "Super Mario" -> loads image
bool img3dsLoadThumb(const char* fullRomName);

void img3dsDrawThumb();

bool img3dsSaveScreenRegion(const char* path, int width, int height, int x0, int y0, gfxScreen_t screen, bool isTopStereo = false);

bool img3dsInitialize();
void img3dsFinalize();

#endif
