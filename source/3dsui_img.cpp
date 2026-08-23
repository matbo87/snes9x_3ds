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
#include "3dspixel_utils.h"
#include "3dsimg_cache.h"
#include "3dsthemes.h"

#define UI_TEX_COUNT 5
#define BEZEL_INNER_WIDTH 320
#define BEZEL_INNER_HEIGHT 239
#define WIDTH_SCALE 1 / BEZEL_INNER_WIDTH
#define HEIGHT_SCALE 1 / BEZEL_INNER_HEIGHT

// min size that C3D_SyncDisplayTransfer accepts on real hardware? (8/16/32 caused freeze)
#define SCANLINE_TEX_DIM 64

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

// Thumbnail caches: current "TMB1", legacy "IMGZ".

Tex3DS_Texture textureInfo[UI_TEX_COUNT];

typedef struct {
    Tex3DS_SubTexture subTex;
    AssetDimensions defaultDim;
    AssetDimensions activeDim;
    char defaultSrc[PATH_MAX];    // path to default PNG (romfs or sdmc _default.png)
    char customPath[PATH_MAX];    // last attempted per-game custom PNG path
    bool customIsActive;          // true if per-game custom PNG is currently active
    bool customLoadFailed;        // true if last customPath load attempt failed
} AssetState;

// bezel, border, cover — metadata only, VRAM lives in GPU3DS.textures.
// The t3x assets (UI_SPLASH, UI_PAUSE) have no per-game override, so no state.
static AssetState assetState[UI_TEX_COUNT - 2];

static ImageCacheReader thumbReader;

static const u16 thumbMaxWidth = 128;
static const u16 thumbMaxHeight = 128;
static const size_t thumbMaxCount = 1024;
static const size_t thumbPixelBufferSize = thumbMaxWidth * thumbMaxHeight * sizeof(u16);


static AssetDrawContext getAssetDrawContext(SGPU_TEXTURE_ID textureId) {
    AssetDrawContext ctx;

    switch (textureId) {
        case UI_OVERLAY:
            ctx.targetScreen = settings3DS.GameScreen;
            ctx.displayMode  = settings3DS.GameOverlay;
            ctx.opacity      = OPACITY_STEPS;
            ctx.screenWidth  = settings3DS.GameScreenWidth;
            break;
        case UI_BG_GAME:
            ctx.targetScreen = settings3DS.GameScreen;
            ctx.displayMode  = settings3DS.GameScreenBg;
            ctx.opacity      = settings3DS.GameScreenBgOpacity;
            ctx.screenWidth  = settings3DS.GameScreenWidth;
            break;
        case UI_BG_SECOND:
            ctx.targetScreen = settings3DS.SecondScreen;
            ctx.displayMode  = settings3DS.SecondScreenBg;
            ctx.opacity      = settings3DS.SecondScreenBgOpacity;
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

static void img3dsInitTexture(SGPUTexture *texture, SGPU_TEXTURE_ID textureId) {
    texture->id = textureId;
    C3D_TexSetFilter(&texture->tex, GPU_LINEAR, GPU_LINEAR);
    texture->scale[3] = 1.0f / texture->tex.width;
    texture->scale[2] = 1.0f / texture->tex.height;
    texture->scale[1] = 0;
    texture->scale[0] = 0;

    log3dsWrite("ui vram texture \"%s\" dim: %dx%d, size:%.2fkb, format: %s",
        utils3dsTextureIDToString(texture->id),
        texture->tex.width, texture->tex.height,
        (float)texture->tex.size / 1024,
        utils3dsTexColorToString(texture->tex.fmt)
    );
}

bool img3dsAllocVramTextures() {
    memset(assetState, 0, sizeof(assetState));

    const struct {
        SGPU_TEXTURE_ID id;
        u16 width;
        u16 height;
        GPU_TEXCOLOR format;
        const char* defaultPng;
    } assetConfigs[] = {
        { UI_OVERLAY,   512, 256, GPU_RGBA8,  "romfs:/gfx/overlay.png" },
        { UI_BG_GAME,   512, 256, GPU_RGB565, "romfs:/gfx/background_game_screen.png" },
        { UI_BG_SECOND, 512, 256, GPU_RGB565, "romfs:/gfx/background_second_screen.png" }
    };

    for (const auto& cfg : assetConfigs) {
        int idx = cfg.id - UI_TEXTURE_START;
        SGPUTexture *texture = &GPU3DS.textures[cfg.id];

        if (!C3D_TexInitVRAM(&texture->tex, cfg.width, cfg.height, cfg.format)) {
            log3dsWrite("[img3dsAllocVramTextures] C3D_TexInitVRAM failed for %s",
                utils3dsTextureIDToString(cfg.id));
            return false;
        }

        img3dsInitTexture(texture, cfg.id);

        assetState[idx].subTex.width  = cfg.width;
        assetState[idx].subTex.height = cfg.height;
        assetState[idx].subTex.left   = 0.0f;
        assetState[idx].subTex.top    = 1.0f;
        assetState[idx].subTex.right  = 1.0f;
        assetState[idx].subTex.bottom = 0.0f;

        assetState[idx].defaultDim = { cfg.width, cfg.height };
        assetState[idx].activeDim  = assetState[idx].defaultDim;
        snprintf(assetState[idx].defaultSrc, sizeof(assetState[idx].defaultSrc), "%s", cfg.defaultPng);
    }

    const struct { SGPU_TEXTURE_ID id; const char* path; } t3xAssets[] = {
        { UI_SPLASH, "romfs:/gfx/splash.t3x" },
        { UI_PAUSE,  "romfs:/gfx/pause.t3x"  }
    };

    for (const auto& asset : t3xAssets) {
        int idx = asset.id - UI_TEXTURE_START;
        SGPUTexture *texture = &GPU3DS.textures[asset.id];

        FILE *file = fopen(asset.path, "rb");
        if (!file) return false;

        textureInfo[idx] = Tex3DS_TextureImportStdio(file, &texture->tex, NULL, false);
        fclose(file);

        if (!textureInfo[idx]) return false;

        img3dsInitTexture(texture, asset.id);
    }

    {
        SGPUTexture *texture = &GPU3DS.textures[UI_SCANLINE];

        if (!C3D_TexInitVRAM(&texture->tex, SCANLINE_TEX_DIM, SCANLINE_TEX_DIM, GPU_RGBA4)) {
            return false;
        }

        texture->id = UI_SCANLINE;
        C3D_TexSetFilter(&texture->tex, GPU_NEAREST, GPU_NEAREST);
        C3D_TexSetWrap(&texture->tex, GPU_REPEAT, GPU_REPEAT);

        texture->scale[3] = 1.0f / texture->tex.width;
        texture->scale[2] = 1.0f / texture->tex.height;
        texture->scale[1] = 0;
        texture->scale[0] = 0;
    }

    return true;
}

// check for _default.png overrides on sdmc and update defaultSrc if found
static void img3dsSetDefaultSources() {
    const struct {
        SGPU_TEXTURE_ID id;
        const char* folder;
    } overrides[] = {
        { UI_OVERLAY,   "overlays" },
        { UI_BG_GAME,   "backgrounds/game_screen" },
        { UI_BG_SECOND, "backgrounds/second_screen" }
    };

    char overridePath[PATH_MAX];

    for (const auto& item : overrides) {
        snprintf(overridePath, sizeof(overridePath), "sdmc:/3ds/snes9x_3ds/%s/_default.png", item.folder);

        if (IsFileExists(overridePath)) {
            int idx = item.id - UI_TEXTURE_START;
            log3dsWrite("[img3ds] Using default override: %s", overridePath);
            snprintf(assetState[idx].defaultSrc, sizeof(assetState[idx].defaultSrc), "%s", overridePath);
        }
    }
}

// decodes PNG and uploads to the texture's existing VRAM allocation
static bool img3dsLoadPngToVram(SGPU_TEXTURE_ID textureId, const char* path) {
    C3D_Tex* tex = &GPU3DS.textures[textureId].tex;

    if (!tex->data) {
        log3dsWrite("[img3ds] VRAM not allocated for texture %d", textureId);
        return false;
    }

    s8 transferFormat = gpu3dsGetTransferFmt(tex->fmt);

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

    int idx = textureId - UI_TEXTURE_START;
    const Tex3DS_SubTexture* subTex = &assetState[idx].subTex;
    
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
                *dst++ = rgba8ToRgb565(*src++);
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

    assetState[idx].activeDim.width = width;
    assetState[idx].activeDim.height = height;

    return true;
}

bool img3dsLoadAsset(SGPU_TEXTURE_ID textureId, const char* path) {
    if (textureId < UI_TEXTURE_START) return false;

    int idx = textureId - UI_TEXTURE_START;
    bool isCustom = path && path[0] != '\0';
    const char* loadPath = isCustom ? path : assetState[idx].defaultSrc;
    bool sameCustomPath = isCustom && strncmp(assetState[idx].customPath, path, PATH_MAX) == 0;

    // custom requested and already showing this exact custom asset
    if (isCustom && assetState[idx].customIsActive && sameCustomPath) {
        return true;
    }

    // custom requested but this exact path already failed, keep default without probing SD again
    if (isCustom && !assetState[idx].customIsActive && assetState[idx].customLoadFailed && sameCustomPath) {
        return false;
    }

    // default requested and already showing default
    if (!isCustom && !assetState[idx].customIsActive) {
        return false;
    }

    bool wasCustomActive = assetState[idx].customIsActive;

    if (!img3dsLoadPngToVram(textureId, loadPath)) {
        if (isCustom) {
            snprintf(assetState[idx].customPath, sizeof(assetState[idx].customPath), "%s", path);
            assetState[idx].customIsActive = wasCustomActive;
            assetState[idx].customLoadFailed = true;
        }
        return false;
    }

    if (isCustom) {
        snprintf(assetState[idx].customPath, sizeof(assetState[idx].customPath), "%s", path);
        assetState[idx].customLoadFailed = false;
    }
    assetState[idx].customIsActive = isCustom;

    return isCustom;
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

void img3dsDrawPause(SGPU_TEXTURE_ID textureId, float xOffset) {
    const Tex3DS_SubTexture* text = Tex3DS_GetSubTexture(textureInfo[textureId - UI_TEXTURE_START], 0);
    if (!text) return;

    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];
    SGPUTexture *texture = &GPU3DS.textures[textureId];

    float x0 = floorf((settings3DS.GameScreenWidth - text->width) / 2.0f + 0.5f) - xOffset;
    float y0 = floorf((SCREEN_HEIGHT - text->height) / 2.0f + 0.5f);

    u32 tint = (Themes[(int)settings3DS.Theme].headerItemTextColor << 8) | 0xFF;

    gpu3dAddSubTextureQuadVertexes(x0, y0, x0 + text->width, y0 + text->height,
        text, text->width, text->height, texture->tex.width, texture->tex.height, 0, tint);

    GPU3DS.currentRenderState.textureBind = textureId;
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_MODULATE_COLOR;
    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;
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
    float xOffset, float sliderT, float &bg2Y, float &bg1Y, float logoPhase, float fade)
{
    u32 bg1Tint = 0x77;
    u32 bg2Tint = 0x99 + (u32)(sliderT * (0xC8 - 0x99)); // dim bg2 as the 3D slider rises
    float bg1CenterX0 = floorf((settings3DS.GameScreenWidth - bg1Center->width) / 2.0f + 0.5f);
    float bg2Scale = 1.0f - sliderT * 0.06f; // bg2 shrinks as the 3D slider rises
    float bg2H = bg2Left->height * bg2Scale;

    float screenCenterX = settings3DS.GameScreenWidth / 2.0f; // scale bg2 toward screen center
    float bg2HiddenByBg1 = 17.0f; // px hidden under bg1 at rest
    float bg2LeftRest = bg1CenterX0 + bg2HiddenByBg1 - bg2Left->width;
    float bg2RightRest = 2.0f * screenCenterX - (bg2LeftRest + bg2Left->width); // Mirror the right panel so both bg2 halves stay symmetric.
    float bg2LeftX0 = screenCenterX + (bg2LeftRest - screenCenterX) * bg2Scale - xOffset;
    float bg2RightX0 = screenCenterX + (bg2RightRest - screenCenterX) * bg2Scale - xOffset;

    if (bg2Y < -bg2H)
        bg2Y += bg2H;

    // tile the (scaled) panels vertically to fill the screen
    for (float y = bg2Y; y < SCREEN_HEIGHT; y += bg2H) {
        img3dsDrawSubTexture(textureId, bg2Left, bg2LeftX0, y, bg2Left->width, bg2Left->height, bg2Tint, bg2Scale, bg2Scale);
        img3dsDrawSubTexture(textureId, bg2Right, bg2RightX0, y, bg2Right->width, bg2Right->height, bg2Tint, bg2Scale, bg2Scale);
    }

    // Drop-shadow of bg1 onto bg2
    float shadowOverscan = fabsf(xOffset);
    int shadowSpread = 32 + (int)shadowOverscan;
    float bg1LeftEdge = bg1CenterX0;
    float bg1RightEdge = bg1CenterX0 + bg1Center->width;

    float shadow1X0 = bg1LeftEdge - xOffset + shadowOverscan + 1 - shadowSpread;
    float shadow2X0 = bg1RightEdge - xOffset - shadowOverscan - 1;
    u32 shadowColor = (u32)(0xCC + sliderT * (0xFF - 0xCC));

    img3dsSplashAddVerticalShadow(shadow1X0, shadowSpread, shadowColor, 0);
    img3dsSplashAddVerticalShadow(shadow2X0, shadowSpread, 0, shadowColor);

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

    // logo grows as the 3D slider rises
    float logoScale = 0.92f + sliderT * 0.08f;
    float logoW = logo->width * logoScale;
    float logoH = logo->height * logoScale;
    float logoX0 = (settings3DS.GameScreenWidth - logoW) / 2.0f + xOffset;
    float logoY0 = (SCREEN_HEIGHT - logoH) / 2.0f + sinf(logoPhase) * 5.0f;

    img3dsDrawSubTexture(textureId, logo, logoX0, logoY0, logo->width, logo->height, 0, logoScale, logoScale);

    if (fade < 1.0f) {
        u32 color = (u32)(0xFF * (1.0f - fade));
        gpu3dsAddQuadRect(0, 0, settings3DS.GameScreenWidth, SCREEN_HEIGHT, 0, 0, 0, color);

        GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_COLOR;
        GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;

        gpu3dsDraw(list, NULL, list->count);
    }
}

void img3dsDrawSplash(SGPU_TEXTURE_ID textureId, bool renderRightEye, float xOffset, float fade) {
    const Tex3DS_Texture info = textureInfo[textureId - UI_TEXTURE_START];

    static float bg2Y = 0;
    static float bg1Y = 0;
    static float logoPhase = 0;
    static bool initialized = false;
    static u64 lastAnimMs = 0;

    // Indices follow gfx/splash.t3s
    const Tex3DS_SubTexture* bg2Left = Tex3DS_GetSubTexture(info, 0);
    const Tex3DS_SubTexture* bg2Right = Tex3DS_GetSubTexture(info, 1);
    const Tex3DS_SubTexture* bg1Center = Tex3DS_GetSubTexture(info, 2);
    const Tex3DS_SubTexture* logo = Tex3DS_GetSubTexture(info, 3);

    if (!initialized) {
        bg2Y = -(float)utils3dsGetRandomInt(0, (int)bg2Left->height);
        bg1Y = -(float)utils3dsGetRandomInt(0, (int)bg1Center->height);
        lastAnimMs = osGetTime();
        initialized = true;
    }

    // Keep splash animation time-based so render-pass count (e.g. 3D on/off)
    // doesn't change perceived animation speed.
    u64 nowMs = osGetTime();
    float deltaSec = (nowMs >= lastAnimMs) ? (float)(nowMs - lastAnimMs) / 1000.0f : 0.0f;
    lastAnimMs = nowMs;

    if (deltaSec > 0.25f) {
        deltaSec = 0.25f;
    }

    float step = deltaSec * 60.0f; // // convert seconds to 60 FPS frame steps

    // sliderT = physical 3D-slider position (0..1), independent of the Intensity3D setting.
    float sliderT = fabsf(xOffset) / gpu3dsGetIODBase();
    if (sliderT > 1.0f) sliderT = 1.0f;
    float bg2Slow = 1.0f - sliderT * 0.5f; // slow bg2 further as the 3D slider rises

    bg2Y -= 0.25f * step * bg2Slow;
    bg1Y -= 0.5f * step;
    logoPhase += 0.04f * step;
    if (logoPhase >= 2.0f * M_PI)
        logoPhase -= 2.0f * M_PI;

    GPU3DS.activeSide = GFX_LEFT;
    img3dsDrawSplashEye(textureId, bg2Left, bg2Right, bg1Center, logo, xOffset, sliderT, bg2Y, bg1Y, logoPhase, fade);

    if (renderRightEye) {
        GPU3DS.activeSide = GFX_RIGHT;
        GPU3DS.appliedRenderState.target = TARGET_UNSET;

        img3dsDrawSplashEye(textureId, bg2Left, bg2Right, bg1Center, logo, -xOffset, sliderT, bg2Y, bg1Y, logoPhase, fade);

        GPU3DS.activeSide = GFX_LEFT;
    }
}

bool img3dsDrawAsset(SGPU_TEXTURE_ID textureId, const AssetDrawContext& ctx, float scaleX, float scaleY, bool forceAlphaBlending, float xOffset) {
    int idx = textureId - UI_TEXTURE_START;
    bool assetIsInactive = ctx.displayMode == Setting::AssetMode::None
        || (ctx.displayMode == Setting::AssetMode::CustomOnly && !assetState[idx].customIsActive);

    if (assetIsInactive) {
        return false;
    }

    float overlayAlpha = 1.0f - ((float)(ctx.opacity) / OPACITY_STEPS);
    u32 overlayColor = overlayAlpha <= 0 ? 0 : (u32)(overlayAlpha * 255.0f);

    int width = assetState[idx].activeDim.width;
    int height = assetState[idx].activeDim.height;

    // centered
    float sx0 = (ctx.screenWidth - (scaleX * width)) / 2 + xOffset;
    float sy0 = (SCREEN_HEIGHT - (scaleY * height)) / 2;

    // snap to integer coords
    if (!xOffset && scaleX == 1.0f && scaleY == 1.0f) 
    {
        sx0 = (int)sx0;
        sy0 = (int)sy0;
    }

    if (forceAlphaBlending) {
        GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;
    }

    img3dsDrawSubTexture(textureId, &assetState[idx].subTex, sx0, sy0, width, height, overlayColor, scaleX, scaleY);

    return true;
}

void img3dsDrawBackground(SGPU_TEXTURE_ID textureId, bool paused, float xOffset) {
    AssetDrawContext ctx = getAssetDrawContext(textureId);

    // sliderT = physical 3D-slider position (0..1), independent of the Intensity3D setting.
    float sliderT = fabsf(xOffset) / gpu3dsGetIODBase();
    if (sliderT > 1.0f) sliderT = 1.0f;

    // Scale max shrink (0.06) by vertical overscan: full at 256px, none at <=240px.
    const AssetDimensions& dim = assetState[textureId - UI_TEXTURE_START].activeDim;
    float overscanT = (float)(dim.height - SCREEN_HEIGHT) / (256 - SCREEN_HEIGHT);
    if (overscanT < 0.0f) overscanT = 0.0f;
    if (overscanT > 1.0f) overscanT = 1.0f;

    float scale = 1.0f - sliderT * overscanT * 0.06f; // background shrinks as the 3D slider rises
    int dimmed = (int)(ctx.opacity * (1.0f - (sliderT * 0.65f)) + 0.5f);
    ctx.opacity = (ctx.opacity > 0 && dimmed < 1) ? 1 : dimmed;

    img3dsDrawAsset(textureId, ctx, scale, scale, false, xOffset);
}

void img3dsDrawGameOverlay(SGPU_TEXTURE_ID textureId, int sWidth, int sHeight) {
    const AssetDrawContext ctx = getAssetDrawContext(textureId);
    float scaleX = (!settings3DS.GameOverlayAutoFit || sWidth == BEZEL_INNER_WIDTH) ? 1.0f : (float)sWidth * WIDTH_SCALE;
    float scaleY = (!settings3DS.GameOverlayAutoFit || sHeight >= SNES_HEIGHT_EXTENDED) ? 1.0f : (float)sHeight * HEIGHT_SCALE;

    img3dsDrawAsset(textureId, ctx, scaleX, scaleY, true, 0);
}

void img3dsUpdateScanlineTexture() {
    if (settings3DS.ScanlineIntensity == 0) return;

    C3D_Tex *tex = &GPU3DS.textures[UI_SCANLINE].tex;
    u16 *dst = (u16 *)g_texUploadBuffer;
    s8 transferFormat = gpu3dsGetTransferFmt(tex->fmt);
    memset(g_texUploadBuffer, 0, tex->size);

    // RGBA4: 1..8 => 93%..47% brightness
    u16 dark = (u16)(settings3DS.ScanlineIntensity & 0xF);

    for (int y = 1; y < SCANLINE_TEX_DIM; y += 2)
        for (int x = 0; x < SCANLINE_TEX_DIM; x++)
            dst[y * tex->width + x] = dark;

    GSPGPU_FlushDataCache(g_texUploadBuffer, tex->size);

    C3D_SyncDisplayTransfer(
        (u32 *)g_texUploadBuffer, GX_BUFFER_DIM(tex->width, tex->height),
        (u32 *)tex->data,         GX_BUFFER_DIM(tex->width, tex->height),
        GX_TRANSFER_OUT_TILED(1) |
        GX_TRANSFER_IN_FORMAT(transferFormat) | GX_TRANSFER_OUT_FORMAT(transferFormat)
    );
}

void img3dsDrawScanlines(float sx0, float sy0, float sx1, float sy1, int sWidth, int cHeight) {
    if (settings3DS.ScanlineIntensity == 0) return;

    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];

    gpu3dsAddSimpleQuadVertexes(sx0, sy0, sx1, sy1, 0, 0, (float)sWidth, (float)cHeight, 0, 0);

    GPU3DS.currentRenderState.textureBind   = UI_SCANLINE;
    GPU3DS.currentRenderState.textureEnv    = TEX_ENV_REPLACE_TEXTURE0;
    GPU3DS.currentRenderState.alphaBlending  = ALPHA_BLENDING_ENABLED;

    gpu3dsDraw(list, NULL, list->count);

    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_DISABLED;
}

// software rendering
void img3dsDrawThumb(int offsetRight, int offsetBottom) {
    if (!thumbReader.currentValid) {
        return;
    }
    int x = settings3DS.SecondScreenWidth - thumbReader.currentWidth - offsetRight;
    int y = SCREEN_HEIGHT - thumbReader.currentHeight - offsetBottom;
    img3dsDrawSwizzledRgb565(thumbReader.pixels, thumbReader.currentWidth, thumbReader.currentHeight, x, y);
}

void img3dsDrawSwizzledRgb565(const u16* src, int width, int height, int x, int y) {
    if (!src || width <= 0 || height <= 0) return;

    u16* fb = (u16*) gfxGetFramebuffer(settings3DS.SecondScreen, GFX_LEFT, NULL, NULL);
    int bottomY = y + height - 1;
    u16* dst = fb + (x * SCREEN_HEIGHT) + (SCREEN_HEIGHT - 1 - bottomY);

    int bpp = gpu3dsGetPixelSize(GPU_RGB565);
    for (int col = 0; col < width; col++) {
        memcpy(dst, src, height * bpp);
        dst += SCREEN_HEIGHT;
        src += height;
    }
}

// Column-major, top row last.
void img3dsSwizzleRgba8ToRgb565(u16* dst, const u32* src, int width, int height) {
    if (!dst || !src || width <= 0 || height <= 0) return;

    for (int y = 0; y < height; y++) {
        int row = height - 1 - y;
        for (int x = 0; x < width; x++)
            dst[x * height + row] = rgba8ToRgb565(src[y * width + x]);
    }
}

void img3dsUnswizzleRgb565(u16* dst, int dstStride, const u16* src, int width, int height) {
    if (!dst || !src || width <= 0 || height <= 0) return;

    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            dst[y * dstStride + x] = src[x * height + y];
}

int img3dsGetThumbHeight() {
    return thumbReader.currentHeight;
}

int img3dsGetThumbWidth() {
    return thumbReader.currentWidth;
}

void img3dsOpenThumbnailCache() {
    if (thumbReader.pixels == NULL || thumbReader.index == NULL) return;

    imgCacheClose(&thumbReader);

    const char* filename = NULL;

    switch (settings3DS.GameThumbnailType) {
        case Setting::ThumbnailMode::None: break;
        case Setting::ThumbnailMode::Boxart: filename = "boxart"; break;
        case Setting::ThumbnailMode::Gameplay:  filename = "gameplay"; break;
        case Setting::ThumbnailMode::Title:  filename = "title";  break;
        default: break;
    }

    if (filename == NULL) return;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "sdmc:/3ds/snes9x_3ds/thumbnails/%s.cache", filename);

    FILE* f = fopen(path, "rb");
    if (f == NULL) return;

    ImageCacheHeader header;
    u32 count = imgCacheReadIndex(f, thumbReader.index, thumbReader.maxCount,
                                  thumbMaxWidth, thumbMaxHeight, &header);
    if (count == 0 ||
        (memcmp(header.magic, "TMB1", 4) != 0 && memcmp(header.magic, "IMGZ", 4) != 0)) {
        fclose(f); return;
    }

    thumbReader.file  = f;      // take ownership only on success
    thumbReader.count = count;

    log3dsWrite("thumbnail cache prepared (%d thumbnails, %.4s, max %dx%dpx)",
                count, header.magic, header.width, header.height);
}

bool img3dsLoadThumb(const char* romName) {
    if (!thumbReader.file || !romName || romName[0] == '\0') {
        return false;
    }

    char basename[NAME_MAX + 1];
    file3dsGetRelatedPath(romName, basename, sizeof(basename), NULL, NULL, true);
    u32 id = utils3dsHashString(basename);

    return imgCacheLoad(&thumbReader, id);
}

bool img3dsLoadStateScreenshot(const char* path) {
    if (!thumbReader.pixels || !path || path[0] == '\0') {
        return false;
    }

    u32 id = utils3dsHashString(path);

    if (id == thumbReader.currentKey) {
        return thumbReader.currentValid;
    }

    int width, height;
    if (!decodePngFromFile(path, width, height)) {
        return false; // missing file or decode error
    }

    if (width <= 0 || height <= 0 || width > thumbMaxWidth || height > thumbMaxHeight) {
        return false;
    }

    img3dsSwizzleRgba8ToRgb565(thumbReader.pixels, (const u32*)g_fileBuffer, width, height);

    imgCacheSetCurrent(&thumbReader, id, (u16)width, (u16)height);
    return true;
}

// Drop the cached key so the next img3dsLoadThumb/img3dsLoadStateScreenshot re-decodes
// from disk, because overwriting a savestate screenshot keeps the same hash cache key.
void img3dsInvalidateStateScreenshot() {
    imgCacheInvalidate(&thumbReader);
}

bool img3dsSaveScreenRegion(const char* path,
    int width, int height, int x0, int y0, gfxScreen_t screen, bool isWide) {
    if (!g_fileBuffer) return false;

    u8* fb = (u8*)gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL);
    u8* dst = (u8*)g_fileBuffer;

    const int bpp = gpu3dsGetPixelSize(GPU_RGB8);
    const int stride = SCREEN_HEIGHT * bpp;

    // In wide mode the physical top framebuffer is 800px.
    // Read the doubled region and average column pairs back down,
    // so the saved image keeps its normal dimensions
    const int xStep = isWide ? 2 : 1;

    for (int y = 0; y < height; y++) {
        int img_y = y0 + y;
        int col = SCREEN_HEIGHT - 1 - img_y;
        u8* src = fb + (x0 * xStep * stride) + (col * bpp);

        u8* dstRow = dst + (y * width * bpp);

        for (int x = 0; x < width; x++) {
            if (xStep == 2) {
                u8* src2 = src + stride;
                dstRow[0] = (src[2] + src2[2]) >> 1;
                dstRow[1] = (src[1] + src2[1]) >> 1;
                dstRow[2] = (src[0] + src2[0]) >> 1;
            } else {
                dstRow[0] = src[2];
                dstRow[1] = src[1];
                dstRow[2] = src[0];
            }

            dstRow += bpp;
            src += stride * xStep;
        }
    }

    return savePng(path, width, height);
}

bool img3dsInitialize() {
	log3dsWrite("[impl3ds] allocate ui textures");
    if (!img3dsAllocVramTextures()) return false;
    
    log3dsWrite("[impl3ds] allocate thumb pixel buffer and index table (%.2fkb, %.2fkb)",
        float(thumbPixelBufferSize) / 1024,
        float(sizeof(ImageCacheEntry) * thumbMaxCount) / 1024);

    bool success = imgCacheAlloc(&thumbReader, thumbMaxCount, thumbPixelBufferSize);

    if (success) {
        img3dsSetDefaultSources();

        // load default PNGs into VRAM
        for (int i = 0; i < UI_TEX_COUNT - 1; i++) {
            if (img3dsLoadPngToVram(SGPU_TEXTURE_ID(i + UI_TEXTURE_START), assetState[i].defaultSrc)) {
                assetState[i].defaultDim = assetState[i].activeDim;
            }
        }
        
        img3dsUpdateScanlineTexture();
    }
    
    return success;
}

void img3dsFinalize() {
    int splashIdx = UI_SPLASH - UI_TEXTURE_START;
    if (textureInfo[splashIdx]) {
        Tex3DS_TextureFree(textureInfo[splashIdx]);
        textureInfo[splashIdx] = NULL;
    }

    log3dsWrite("dealloc thumb pixel buffer and index table");
    imgCacheFree(&thumbReader);
}
