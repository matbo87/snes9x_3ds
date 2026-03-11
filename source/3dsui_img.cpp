#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include "snes9x.h"

#include "3dsutils.h"
#include "png_utils.h"
#include "3dssettings.h"
#include "3dslog.h"
#include "3dsimpl_gpu.h"
#include "3dsimpl.h"
#include "3dsui.h"
#include "3dsui_notif.h"
#include "3dsui_img.h"

#define UI_TEX_COUNT 4
#define BEZEL_INNER_WIDTH 320
#define BEZEL_INNER_HEIGHT 239
#define WIDTH_SCALE 1 / BEZEL_INNER_WIDTH
#define HEIGHT_SCALE 1 / BEZEL_INNER_HEIGHT

typedef struct {
    u16 width;
    u16 height;
} AssetDimensions;

typedef struct {
    u16 opacity;
    u16 screenWidth;
    gfxScreen_t targetScreen;
    Setting::AssetMode displayMode;
} AssetDrawContext;

typedef struct {
    C3D_Tex tex;
    AssetDimensions dim;
    char path[PATH_MAX];
    bool active;
} UiAsset;

typedef struct {
    char magic[4]; // "IMGZ"
    u32 count;
    u16 width;     // all images in cache file have the same dimensions
    u16 height;
} ThumbCacheHeader;

typedef struct {
    u32 gameID;    // DJB2 Hash of trimmed filename
    u32 offset;    // offset in bytes to the pixel data
} ThumbIndex;

Tex3DS_Texture textureInfo[UI_TEX_COUNT];

// bezel, border, cover
static UiAsset defaultAssets[UI_TEX_COUNT - 1]; // holds bundled t3x files, overwritten by runtime PNGs if available
static UiAsset externalAssets[UI_TEX_COUNT - 1]; // holds runtime PNGs if available

static u16* thumbPixelBuffer;
static ThumbIndex* thumbIndexTable;

static FILE* thumbCacheFile;

static u16 thumbMaxWidth = 128; 
static u16 thumbMaxHeight = 128;
static const size_t thumbMaxCount = 1024; // for thumbnail index table, max 1024 games (8kb linear ram)
static const size_t thumbPixelBufferSize = thumbMaxWidth * thumbMaxHeight * sizeof(u16); // 128x128px 16bit thumbnail (32kb)

static u16 currentThumbWidth;
static u16 currentThumbHeight;
static u32 currentThumbID;
static u32 nextThumbID;
static u32 thumbTotalCount;


static AssetDrawContext getAssetDrawContext(SGPU_TEXTURE_ID textureId) {
    AssetDrawContext ctx;

    switch (textureId) {
        case UI_BORDER:
            ctx.targetScreen = settings3DS.GameScreen;
            ctx.displayMode  = settings3DS.GameBorder;
            ctx.opacity      = settings3DS.GameBorderOpacity;
            ctx.screenWidth  = settings3DS.GameScreenWidth;
            break;
        case UI_BEZEL:
            ctx.targetScreen = settings3DS.GameScreen;
            ctx.displayMode  = settings3DS.GameBezel;
            ctx.opacity      = OPACITY_STEPS;
            ctx.screenWidth  = settings3DS.GameScreenWidth;
            break;
        case UI_COVER:
            ctx.targetScreen = settings3DS.SecondScreen;
            ctx.displayMode  = settings3DS.SecondScreenContent;
            ctx.opacity      = settings3DS.SecondScreenOpacity;
            ctx.screenWidth  = settings3DS.SecondScreenWidth;
            break;
        
        default:
            ctx.targetScreen = settings3DS.GameScreen;
            ctx.displayMode  = Setting::AssetMode::None;
            ctx.opacity      = 0;
            ctx.screenWidth  = settings3DS.GameScreenWidth;
            break;
    }
    
    return ctx;
}

static void img3dsAllocVramTexture(const char *path, SGPU_TEXTURE_ID textureId) {
    FILE *file = fopen(path, "rb");
    if (!file) return;

    SGPUTexture *texture = &GPU3DS.textures[textureId];
    int idx = textureId - UI_TEXTURE_START;
    textureInfo[idx] = Tex3DS_TextureImportStdio(file, &texture->tex, NULL, true);
    fclose(file);

    if (textureInfo[idx]) {
        texture->id = textureId;
        GPU_TEXTURE_FILTER_PARAM filter = GPU_LINEAR;
        C3D_TexSetFilter(&texture->tex, filter, filter);
        
        texture->scale[3] = 1.0f / texture->tex.width;  // x
        texture->scale[2] = 1.0f / texture->tex.height; // y
        texture->scale[1] = 0; // z
        texture->scale[0] = 0; // w

        log3dsWrite("ui vram texture \"%s\" dim: %dx%d, size:%.2fkb, format: %s",
            utils3dsTextureIDToString(texture->id),
            texture->tex.width, texture->tex.height,
            (float)texture->tex.size / 1024,
            utils3dsTexColorToString(texture->tex.fmt)
        );

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
    }
}

bool img3dsAllocVramTextures() {
    memset(defaultAssets, 0, sizeof(defaultAssets));
    memset(externalAssets, 0, sizeof(externalAssets));

    // TODO: We could save our limited VRAM here and also use externalAssets
    img3dsAllocVramTexture("romfs:/gfx/border.t3x", UI_BORDER);
    img3dsAllocVramTexture("romfs:/gfx/bezel.t3x", UI_BEZEL);
    img3dsAllocVramTexture("romfs:/gfx/cover.t3x", UI_COVER);

    img3dsAllocVramTexture("romfs:/gfx/atlas.t3x", UI_ATLAS);
    
    // skip UI_ATLAS, we won't update this texture
    for(int i=0; i < UI_TEX_COUNT - 1; i++) {
        SGPU_TEXTURE_ID id = SGPU_TEXTURE_ID(i + UI_TEXTURE_START);
        SGPUTexture *texture = &GPU3DS.textures[id];

        int width  = texture->tex.width;
        int height = texture->tex.height;

        if (!width || !height) {
            log3dsWrite("[img3dsLoadTextures] texture not set %s", utils3dsTextureIDToString(id));

            return false;
        }

        if (!C3D_TexInitVRAM(
            &externalAssets[i].tex, 
            width,
            height,
            texture->tex.fmt
        )) {
            log3dsWrite("[img3dsLoadTextures] C3D_TexInit failed for idx %d (%dx%d)", i, width, height);
            return false;
        }
    }

    return true;
}


void img3dsUpdateDefaultAssets() {
    const struct {
        SGPU_TEXTURE_ID id;
        const char* folder;
    } overrides[] = {
        { UI_BORDER, "borders" },
        { UI_BEZEL,  "bezels" },
        { UI_COVER,  "covers" }
    };

    char overridePath[PATH_MAX];

    for (const auto& item : overrides) {
        snprintf(overridePath, sizeof(overridePath), "sdmc:/3ds/snes9x_3ds/%s/_default.png", item.folder);
        
        if (IsFileExists(overridePath)) {
            log3dsWrite("[img3ds] Loading update default asset: %s", overridePath);
            img3dsUpdateSubtexture(item.id, overridePath, true); 
        }
    }
}

//---------------------------------------------------------------
// external png handling
//---------------------------------------------------------------


// decodes PNG, uploads to VRAM and updates UiAsset metadata
static bool img3dsLoadPngToAsset(UiAsset* asset, int textureIdx, const char* path) {
    C3D_Tex* tex = &asset->tex;

    if (!tex->data) {
        log3dsWrite("[img3ds] VRAM not allocated for asset idx %d", textureIdx);
        return false;
    }

    s8 transferFormat = gpu3dsGetTransferFmt(tex->fmt);

    // currently CPU swizzling logic only supports RGB565 and RGBA8 for convenience
    if (transferFormat != GX_TRANSFER_FMT_RGB565 && transferFormat != GX_TRANSFER_FMT_RGBA8) {
        log3dsWrite("[img3ds] Unsupported format %d for %s", transferFormat, path);
        return false;
    }

    if (tex->size > MAX_IO_BUFFER_SIZE) {
        log3dsWrite("[img3ds] Texture too large for buffer: %d > %d", tex->size, MAX_IO_BUFFER_SIZE);
        return false;
    }

    int width, height;
    if (!decodePngFromFile(path, width, height)) {
        log3dsWrite("[img3ds] PNG decode failed: %s", path);
        return false;
    }

    const Tex3DS_SubTexture* subTex = Tex3DS_GetSubTexture(textureInfo[textureIdx], 0);
    if (!width || !height || width > subTex->width || height > subTex->height) {
        log3dsWrite("[img3ds] Invalid dimensions for %s (%dx%d has to be < %dx%d)", path, width, height, subTex->width, subTex->height);
        return false;
    }

    memset(g_texUploadBuffer, 0, tex->size);

    int tx = (int)(subTex->left * tex->width);
    int ty = (int)((1.0f - subTex->top) * tex->height); 
    u32* src = (u32*)g_fileBuffer;

    if (transferFormat == GX_TRANSFER_FMT_RGB565) {
        u16* dst = (u16*)g_texUploadBuffer + (ty * tex->width) + tx;
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
        u32* dst = (u32*)g_texUploadBuffer + (ty * tex->width) + tx;
        int dstStride = tex->width - width;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                *dst++ = __builtin_bswap32(*src++);
            }
            dst += dstStride;
        }
    }

    GSPGPU_FlushDataCache(g_texUploadBuffer, tex->size);

    C3D_SyncDisplayTransfer(
        (u32*)g_texUploadBuffer, GX_BUFFER_DIM(tex->width, tex->height),
        (u32*)tex->data, GX_BUFFER_DIM(tex->width, tex->height),
        GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_IN_FORMAT(transferFormat) | GX_TRANSFER_OUT_FORMAT(transferFormat)
    );

    snprintf(asset->path, sizeof(asset->path), "%s", path);
    asset->dim.width = width;
    asset->dim.height = height;
    asset->active = true;

    return true;
}

bool img3dsUpdateSubtexture(SGPU_TEXTURE_ID textureId, const char* imagePath, bool isDefault) {
    if (textureId < UI_TEXTURE_START) return false;

    int idx = textureId - UI_TEXTURE_START;
    
    UiAsset* asset = isDefault ? &defaultAssets[idx] : &externalAssets[idx];

    bool isActive = (GPU3DS.textures[textureId].tex.data == asset->tex.data);
    if (isActive && strncmp(asset->path, imagePath, PATH_MAX) == 0) {
        asset->active = true;

        return true; 
    }

    if (!img3dsLoadPngToAsset(asset, idx, imagePath)) {
        return false;
    }

    // state swap
    // point the active render index to our custom state
    GPU3DS.textures[textureId].tex = asset->tex;
    
    return true;
}

bool img3dsIsAssetCached(SGPU_TEXTURE_ID textureId, const char* imagePath) {
    if (textureId < UI_TEXTURE_START || !imagePath || imagePath[0] == '\0') return false;

    int idx = textureId - UI_TEXTURE_START;
    UiAsset* asset = &externalAssets[idx];

    return asset->active && strncmp(asset->path, imagePath, PATH_MAX) == 0;
}

void img3dsRestoreDefaultAsset(SGPU_TEXTURE_ID textureId) {
    if (textureId != UI_BORDER && textureId != UI_BEZEL && textureId != UI_COVER) {
        return;
    }

    int idx = textureId - UI_TEXTURE_START;
    UiAsset* asset = &externalAssets[idx];

    // reset metadata
    asset->path[0] = '\0';
    asset->active = false;

    if (defaultAssets[idx].tex.data != NULL) {
        GPU3DS.textures[textureId].tex = defaultAssets[idx].tex;
    }
}

void img3dsDrawSubTexture(SGPU_TEXTURE_ID textureId, const Tex3DS_SubTexture* subTexture,
    float sx0, float sy0, u16 width, u16 height, u32 overlayColor, float scaleX, float scaleY) 
{
    if (!subTexture) return;

    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    SGPUTexture *texture = &GPU3DS.textures[textureId];

    float sx1 = sx0 + (width * scaleX);
    float sy1 = sy0 + (height * scaleY);

    gpu3dAddSubTextureQuadVertexes(sx0, sy0, sx1, sy1, subTexture, width, height, texture->tex.width, texture->tex.height, 0, overlayColor);

	GPU3DS.currentRenderState.textureBind = textureId;
	GPU3DS.currentRenderState.textureEnv = overlayColor == 0 ? TEX_ENV_REPLACE_TEXTURE0 : TEX_ENV_BLEND_COLOR_TEXTURE0;

    gpu3dsDraw(list, NULL, list->count);
}

void img3dsSplashAddVerticalShadow(float x0, int width, int color1, int color2) {
    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    SQuadVertex *vertices = (SQuadVertex *) list->data + list->from + list->count;

    float x1 = x0 + width;
    float y0 = 0;
    float y1 = SCREEN_HEIGHT;

	vertices[0].Position = {x0, y0, 0, 1};
	vertices[1].Position = {x1, y0, 0, 1};
	vertices[2].Position = {x0, y1, 0, 1};

	vertices[3].Position = {x1, y1, 0, 1};
	vertices[4].Position = {x0, y1, 0, 1};
	vertices[5].Position = {x1, y0, 0, 1};

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

static void img3dsDrawSplashEye(SGPU_TEXTURE_ID textureId,
    const Tex3DS_SubTexture* bg2Left, const Tex3DS_SubTexture* bg2Right,
    const Tex3DS_SubTexture* bg1Center, const Tex3DS_SubTexture* logo,
    float xOffset, float &bg2Y, float &bg1Y, float logoPhase, float fade)
{
    u32 bg1Tint = 0x77;
    u32 bg2Tint = 0x99;

    // bg2: left + right side textures (slow parallax scroll)
    float bg2LeftX0 = -xOffset;
    float bg2RightX0 = settings3DS.GameScreenWidth - bg2Right->width - xOffset;

    if (bg2Y < -bg2Left->height)
        bg2Y += bg2Left->height;

    img3dsDrawSubTexture(textureId, bg2Left, bg2LeftX0, bg2Y, bg2Left->width, bg2Left->height, bg2Tint);
    img3dsDrawSubTexture(textureId, bg2Right, bg2RightX0, bg2Y, bg2Right->width, bg2Right->height, bg2Tint);

    if (bg2Y < (SCREEN_HEIGHT - bg2Left->height)) {
        float y1 = bg2Y + bg2Left->height;
        img3dsDrawSubTexture(textureId, bg2Left, bg2LeftX0, y1, bg2Left->width, bg2Left->height, bg2Tint);
        img3dsDrawSubTexture(textureId, bg2Right, bg2RightX0, y1, bg2Right->width, bg2Right->height, bg2Tint);
    }

    // shadows between bg2 and bg1
    float bg1CenterX0 = (settings3DS.GameScreenWidth - bg1Center->width) / 2.0f;
    int shadowWidth = 20;
    float shadow1X0 = bg1CenterX0 - shadowWidth + IOD_MAX_PIXELS - xOffset + 1;
    float shadow2X0 = bg1CenterX0 + bg1Center->width - IOD_MAX_PIXELS - xOffset - 1;

    img3dsSplashAddVerticalShadow(shadow1X0, shadowWidth, 0x000000dd, 0);
    img3dsSplashAddVerticalShadow(shadow2X0, shadowWidth, 0, 0x000000dd);

    GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_COLOR;
    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;

    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    gpu3dsDraw(list, NULL, list->count);

    // bg1: center texture (fast parallax scroll)
    if (bg1Y < -bg1Center->height)
        bg1Y += bg1Center->height;

    img3dsDrawSubTexture(textureId, bg1Center, bg1CenterX0, bg1Y, bg1Center->width, bg1Center->height, bg1Tint);

    if (bg1Y < (SCREEN_HEIGHT - bg1Center->height)) {
        float y1 = bg1Y + bg1Center->height;
        img3dsDrawSubTexture(textureId, bg1Center, bg1CenterX0, y1, bg1Center->width, bg1Center->height, bg1Tint);
    }

    // logo
    float logoX0 = (settings3DS.GameScreenWidth - logo->width) / 2.0f + xOffset;
    float logoY0 = (SCREEN_HEIGHT - logo->height) / 2.0f + sinf(logoPhase) * 5.0f;

    img3dsDrawSubTexture(textureId, logo, logoX0, logoY0, logo->width, logo->height);

    if (fade < 1.0f) {
        u32 color = (u32)(0xFF * (1.0f - fade));
        gpu3dsAddQuadRect(0, 0, settings3DS.GameScreenWidth, SCREEN_HEIGHT, 0, 0, 0, color);

        GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_COLOR;
        GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;

        gpu3dsDraw(list, NULL, list->count);
    }
}

void img3dsDrawSplash(SGPU_TEXTURE_ID textureId, bool isTopStereo, float xOffset, float fade) {
    const Tex3DS_Texture info = textureInfo[textureId - UI_TEXTURE_START];

    static float bg2Y = 0;
    static float bg1Y = 0;
    static float logoPhase = 0;
    static bool bg2Swapped = false;
    static bool initialized = false;

    if (!initialized) {
        bg2Swapped = utils3dsGetRandomInt(0, 1);
        bg2Y = -(float)utils3dsGetRandomInt(0, (int)Tex3DS_GetSubTexture(info, 0)->height);
        bg1Y = -(float)utils3dsGetRandomInt(0, (int)Tex3DS_GetSubTexture(info, 2)->height);
        initialized = true;
    }

    const Tex3DS_SubTexture* bg2Left = Tex3DS_GetSubTexture(info, bg2Swapped ? 1 : 0);
    const Tex3DS_SubTexture* bg2Right = Tex3DS_GetSubTexture(info, bg2Swapped ? 0 : 1);
    const Tex3DS_SubTexture* bg1Center = Tex3DS_GetSubTexture(info, 2);
    const Tex3DS_SubTexture* logo = Tex3DS_GetSubTexture(info, 3);

    bg2Y -= 0.25f;
    bg1Y -= 0.5f;
    logoPhase += 0.04f;
    if (logoPhase >= 2.0f * M_PI)
        logoPhase -= 2.0f * M_PI;

    GPU3DS.activeSide = GFX_LEFT;
    img3dsDrawSplashEye(textureId, bg2Left, bg2Right, bg1Center, logo, xOffset, bg2Y, bg1Y, logoPhase, fade);

    if (isTopStereo) {
        GPU3DS.activeSide = GFX_RIGHT;
        GPU3DS.appliedRenderState.target = TARGET_UNSET;

        img3dsDrawSplashEye(textureId, bg2Left, bg2Right, bg1Center, logo, -xOffset, bg2Y, bg1Y, logoPhase, fade);

        GPU3DS.activeSide = GFX_LEFT;
    }
}

bool img3dsDrawAsset(SGPU_TEXTURE_ID textureId, const AssetDrawContext& ctx, float scaleX, float scaleY, bool forceAlphaBlending, float xOffset) {
    int idx = textureId - UI_TEXTURE_START;
    bool assetIsInactive = ctx.displayMode == Setting::AssetMode::None
        || (ctx.displayMode == Setting::AssetMode::CustomOnly && !externalAssets[idx].active);

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
    float sx0 = (ctx.screenWidth - (scaleX * width)) / 2 + xOffset;
    float sy0 = (SCREEN_HEIGHT - (scaleY * height)) / 2;

    if (forceAlphaBlending) {
        GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;
    }

    img3dsDrawSubTexture(textureId, Tex3DS_GetSubTexture(textureInfo[idx], 0), sx0, sy0, width, height, overlayColor, scaleX, scaleY);

    return true;
}

void img3dsDrawBackground(SGPU_TEXTURE_ID textureId, bool paused, float xOffset) {
    const AssetDrawContext ctx = getAssetDrawContext(textureId);
    img3dsDrawAsset(textureId, ctx, 1.0f, 1.0f, false, xOffset);
}

void img3dsDrawGameOverlay(SGPU_TEXTURE_ID textureId, int sWidth, int sHeight) {
    const AssetDrawContext ctx = getAssetDrawContext(textureId);

    bool autoFitDisabled = !UI_BEZEL || !settings3DS.GameBezelAutoFit;
    float scaleX = (autoFitDisabled || sWidth == BEZEL_INNER_WIDTH) ? 1.0f : (float)sWidth * WIDTH_SCALE;
    float scaleY = (autoFitDisabled || sHeight >= SNES_HEIGHT_EXTENDED) ? 1.0f : (float)sHeight * HEIGHT_SCALE;

    img3dsDrawAsset(textureId, ctx, scaleX, scaleY, true, 0);
}

// software rendering
void img3dsDrawThumb() {
    if (currentThumbID != nextThumbID) {
        return;
    }

    u16* fb = (u16*) gfxGetFramebuffer(settings3DS.SecondScreen, GFX_LEFT, NULL, NULL);
    int screenX = settings3DS.SecondScreenWidth - currentThumbWidth;
    int screenY = SCREEN_HEIGHT - currentThumbHeight - 20;
    int bottomY = screenY + currentThumbHeight - 1;
    
    u16* dst = fb + (screenX * SCREEN_HEIGHT) + (SCREEN_HEIGHT - 1 - bottomY);
    u16* src = thumbPixelBuffer;

    int bpp = gpu3dsGetPixelSize(GPU_RGB565);
    for (int col = 0; col < currentThumbWidth; col++) {
        // copy one full vertical column at once
        // data being is already pre-swizzled so we can just do memcpy here
        memcpy(dst, src, currentThumbHeight * bpp);

        dst += SCREEN_HEIGHT;
        src += currentThumbHeight;
    }
}

void img3dsSetThumbMode() {
    if (thumbPixelBuffer == NULL || thumbIndexTable == NULL) return;

    if (thumbCacheFile) {
        fclose(thumbCacheFile);
    }

    // reset metadata + invalidate IDs so we don't draw stale data
    currentThumbWidth = 0;
    currentThumbHeight = 0;
    thumbTotalCount = 0;
    currentThumbID = 0;
    nextThumbID = 0;
    
    memset(thumbPixelBuffer, 0, thumbPixelBufferSize);

    const char* filename = NULL;    

    switch (settings3DS.GameThumbnailType) {
        case Setting::ThumbnailMode::Boxart: filename = "boxart"; break;
        case Setting::ThumbnailMode::Gameplay:  filename = "gameplay"; break;
        case Setting::ThumbnailMode::Title:  filename = "title";  break;
    }

    if (filename == NULL) return;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "sdmc:/3ds/snes9x_3ds/thumbnails/%s.cache", filename);

    thumbCacheFile = fopen(path, "rb");
    if (thumbCacheFile == NULL) return;

    ThumbCacheHeader header;
    if (fread(&header, sizeof(ThumbCacheHeader), 1, thumbCacheFile) != 1) {
        fclose(thumbCacheFile); thumbCacheFile = NULL; return;
    }

    if (memcmp(header.magic, "IMGZ", 4) != 0) {
        // invalid Format
        fclose(thumbCacheFile); 
        
        thumbCacheFile = NULL; 
        
        return;
    }

    if (header.width > thumbMaxWidth || header.height > thumbMaxHeight) {
        log3dsWrite("Invalid cache dimensions: %dx%d (max %dx%d)", header.width, header.height, thumbMaxWidth, thumbMaxHeight);
        fclose(thumbCacheFile); 
        thumbCacheFile = NULL; 
        return;
    }
    
    u32 requiredSize = header.width * header.height * gpu3dsGetPixelSize(GPU_RGB565);
    if (requiredSize > thumbPixelBufferSize) {
        fclose(thumbCacheFile); thumbCacheFile = NULL; return;
    }

    if (header.count > thumbMaxCount) {
        // we could technically read only the first `thumbMaxCount`, but safer to just fail
        fclose(thumbCacheFile); thumbCacheFile = NULL; return;
    }

    currentThumbWidth = header.width;
    currentThumbHeight = header.height;
    thumbTotalCount = header.count;

    if (thumbIndexTable) {
        fread(thumbIndexTable, sizeof(ThumbIndex), thumbTotalCount, thumbCacheFile);
    }

    log3dsWrite("thumbnail cache prepared (%d thumbnails, %dx%dpx)", thumbTotalCount, currentThumbWidth, currentThumbHeight);
}

bool img3dsLoadThumb(const char* romName) {
    if (!thumbCacheFile|| !romName || romName[0] == '\0') {
        return false;
    }
    
    char basename[NAME_MAX + 1];
    file3dsGetRelatedPath(romName, basename, sizeof(basename), NULL, NULL, true);
    nextThumbID = utils3dsHashString(basename);
    
    // buffer already holds this image
    if (nextThumbID == currentThumbID) {
        return true;
    }

    // linear search is fine for < 2000 items. 
    u32 fileOffset = 0;
    bool thumbFound = false;

    for (u32 i = 0; i < thumbTotalCount; i++) {
        if (thumbIndexTable[i].gameID == nextThumbID) {
            fileOffset = thumbIndexTable[i].offset;
            thumbFound = true;
            break;
        }
    }

    size_t sizeToRead = currentThumbWidth * currentThumbHeight * sizeof(u16);

    if (thumbFound) {
        fseek(thumbCacheFile, fileOffset, SEEK_SET);
        fread(thumbPixelBuffer, sizeToRead, 1, thumbCacheFile);

        currentThumbID = nextThumbID;

    } else {
        // clear thumb pixel buffer
        memset(thumbPixelBuffer, 0, sizeToRead);
        currentThumbID = 0;
    }

    return thumbFound;
}

bool img3dsSaveScreenRegion(const char* path,
    int width, int height, int x0, int y0, gfxScreen_t screen, bool isTopStereo) {
    if (!g_fileBuffer) return false;

    u8* fb = (u8*)gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);
    u8* dst = (u8*)g_fileBuffer;

    const int bpp = gpu3dsGetPixelSize(GPU_RGB8);
    const int stride = SCREEN_HEIGHT * bpp;

    for (int y = 0; y < height; y++) {
        int img_y = y0 + y;
        int col = SCREEN_HEIGHT - 1 - img_y;
        u8* src = fb + (x0 * stride) + (col * bpp);
        
        u8* dstRow = dst + (y * width * bpp);

        for (int x = 0; x < width; x++) {
            dstRow[0] = src[2];
            dstRow[1] = src[1];
            dstRow[2] = src[0];

            dstRow += bpp;
            src += stride;
        }
    }

    return savePng(path, width, height);
}

bool img3dsInitialize() {
	log3dsWrite("[impl3ds] allocate ui textures");
    if (!img3dsAllocVramTextures()) return false;
    
    log3dsWrite("[impl3ds] allocate thumb pixel buffer and index table (%.2fkb, %.2fkb)", 
        float(thumbPixelBufferSize) / 1024, 
        float(sizeof(ThumbIndex) * thumbMaxCount) / 1024);

    thumbPixelBuffer = (u16*)linearAlloc(thumbPixelBufferSize);
    thumbIndexTable = (ThumbIndex*)malloc(sizeof(ThumbIndex) * thumbMaxCount);

    bool success = thumbPixelBuffer && thumbIndexTable;

    if (success) {
        memset(thumbPixelBuffer, 0, thumbPixelBufferSize);
        memset(thumbIndexTable, 0, sizeof(ThumbIndex) * thumbMaxCount);

        img3dsUpdateDefaultAssets();
    }
    
    return success;
}

void img3dsFinalize() {
    log3dsWrite("destroy ui textures");
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

    log3dsWrite("dealloc thumb pixel buffer and index table");
    linearFree(thumbPixelBuffer);
    free(thumbIndexTable);

    if (thumbCacheFile) {
        fclose(thumbCacheFile);
    }
}
