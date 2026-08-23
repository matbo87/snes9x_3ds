#include <cstdio>
#include <cstring>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#include "3dsutils.h"
#include "3dssettings.h"
#include "3dsglyphs.h"
#include "3dslog.h"
#include "3dsimpl_gpu.h"
#include "3dsui.h"
#include "3dsui_img.h"
#include "3dsui_notif.h"

typedef struct {
    u64 visibleUntil;
    u32 backgroundColor, borderColor, textColor;
    u16 bx0, by0, bx1, by1;
    u16 textWidth;
    u8 borderSize, paddingX, paddingY;

    char text[64];
    Notif::Event event;
    Notif::Type type;
    bool dirty;
} PlainNotification;

typedef struct {
    u64 visibleUntil;
    u32 backgroundColor;
    u16 textWidth, text2Width;
    char text[64];
    char text2[64];
    bool active;
    bool dirty;
    bool hasThumb;
} RichNotification;

static PlainNotification notifMsg = {0};
static PlainNotification notifFps = {0};
static RichNotification notifRich = {0};

static const int NOTIF_MARGIN = 8;

static const int RICH_MARGIN = 8;
static const int RICH_PAD = 8;
static const int RICH_THUMB = 48;
static const int RICH_CHROME_W = RICH_THUMB + RICH_PAD * 2;
static const int RICH_FULL_WIDTH_SNAP_DIVISOR = 8;
static const int RICH_MIN_TEXT_W = 112;
static const int RICH_TITLE_FONT_HEIGHT = 18;
static const int RICH_TITLE_Y = 0;
static const int RICH_DESC_Y = RICH_TITLE_Y + RICH_TITLE_FONT_HEIGHT;
static_assert(RICH_DESC_Y + FONT_HEIGHT < NOTIF_RICH_HEIGHT_MAX,
              "rich toast text bands must leave the last texture row for the white texel");

// Give each notification texture its own upload space so uploads cannot overwrite each other.
static const SGPU_TEXTURE_ID notifTextureIds[] = {
    UI_NOTIF_MSG, UI_NOTIF_FPS, UI_NOTIF_RICH, UI_THUMB
};
static const int NOTIF_TEXTURE_COUNT = sizeof(notifTextureIds) / sizeof(notifTextureIds[0]);

static u8 *notifUploadBuffer = NULL;
static u8 *notifUploadSlice[NOTIF_TEXTURE_COUNT] = {0};

static u8 *notif3dsGetUploadSlice(SGPU_TEXTURE_ID id) {
    for (int i = 0; i < NOTIF_TEXTURE_COUNT; i++)
        if (notifTextureIds[i] == id)
            return notifUploadSlice[i];
    return NULL;
}

// Call after all notification textures are initialized.
static bool notif3dsInitUploadBuffer() {
    size_t total = 0;
    for (int i = 0; i < NOTIF_TEXTURE_COUNT; i++)
        total += GPU3DS.textures[notifTextureIds[i]].tex.size;

    notifUploadBuffer = (u8 *)linearAlloc(total);
    if (!notifUploadBuffer)
        return false;

    size_t offset = 0;
    for (int i = 0; i < NOTIF_TEXTURE_COUNT; i++) {
        notifUploadSlice[i] = notifUploadBuffer + offset;
        offset += GPU3DS.textures[notifTextureIds[i]].tex.size;
    }

    log3dsWrite("allocate notif upload buffer (%.2fkb)", (float)total / 1024);
    return true;
}

static bool notif3dsInitTexture(SGPU_TEXTURE_ID id, int maxWidth, int maxHeight,
                                GPU_TEXCOLOR fmt = GPU_RGBA4, bool linear = false) {
    SGPUTexture *texture = &GPU3DS.textures[id];

    int width = gpu3dsGetNextPowerOf2(maxWidth);
    int height = gpu3dsGetNextPowerOf2(maxHeight);

    if (!C3D_TexInitVRAM(&texture->tex, width, height, fmt)) {
        return false;
    }

    texture->id = id;

    GPU_TEXTURE_FILTER_PARAM filter = linear ? GPU_LINEAR : GPU_NEAREST;
    C3D_TexSetFilter(&texture->tex, filter, filter);

    texture->scale[3] = 1.0f / texture->tex.width;
    texture->scale[2] = 1.0f / texture->tex.height;
    texture->scale[1] = 0;
    texture->scale[0] = 0;

    C3D_Tex *tex = &texture->tex;
    log3dsWrite("ui texture \"%s\" dim: %dx%d, size:%.2fkb, format: %s",
        utils3dsTextureIDToString(id),
        tex->width, tex->height,
        (float)tex->size / 1024,
        utils3dsTexColorToString(tex->fmt)
    );

    return true;
}

// Text uploads need a vertical flip; staged badges are already upright.
static void notif3dsUpload(SGPU_TEXTURE_ID id, bool flipVert) {
    C3D_Tex *tex = &GPU3DS.textures[id].tex;
    u8 *slice = notif3dsGetUploadSlice(id);

    GSPGPU_FlushDataCache(slice, tex->size);

    C3D_SyncDisplayTransfer(
        (u32 *)slice,     GX_BUFFER_DIM(tex->width, tex->height),
        (u32 *)tex->data, GX_BUFFER_DIM(tex->width, tex->height),
        GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_FLIP_VERT(flipVert ? 1 : 0) |
        GX_TRANSFER_IN_FORMAT(tex->fmt) | GX_TRANSFER_OUT_FORMAT(tex->fmt)
    );
}

static u16 notif3dsSyncTexture(SGPU_TEXTURE_ID id, const char *text, u32 color) {
    C3D_Tex *tex = &GPU3DS.textures[id].tex;
    u16 *dst = (u16 *)notif3dsGetUploadSlice(id);
    int w = tex->width;

    memset(dst, 0, tex->size);
    // single texel is enough under GPU_NEAREST
    dst[(tex->height - 1) * w + (w - 1)] = 0xFFFF;

    u16 textWidth = ui3dsDrawStringToTexture(
        dst, text,
        0, 0, tex->width, tex->height,
        color
    );

    notif3dsUpload(id, true);

    return textWidth;
}

static void notif3dsSyncRichText() {
    C3D_Tex *tex = &GPU3DS.textures[UI_NOTIF_RICH].tex;
    u16 *dst = (u16 *)notif3dsGetUploadSlice(UI_NOTIF_RICH);
    int w = tex->width;

    memset(dst, 0, tex->size);

    // single texel is enough under GPU_NEAREST
    dst[(tex->height - 1) * w + (w - 1)] = 0xFFFF;

    int fpsBoxW = settings3DS.ShowFPS ? (notifFps.textWidth + notifFps.paddingX * 2) : 0;
    int budget = settings3DS.GameScreenWidth - RICH_MARGIN * 2 - RICH_CHROME_W - fpsBoxW;
    int maxTextW = budget < w ? budget : w;
    if (maxTextW < 0) maxTextW = 0;

    char title[64], desc[64];
    ui3dsEllipsize(notifRich.text,  title, sizeof(title), maxTextW, RICH_TITLE_FONT_HEIGHT);
    ui3dsEllipsize(notifRich.text2, desc,  sizeof(desc),  maxTextW);

    notifRich.textWidth  = ui3dsDrawStringToTexture(dst, title, 0, RICH_TITLE_Y, w, tex->height, 0xFFFFFFFF, RICH_TITLE_FONT_HEIGHT);
    notifRich.text2Width = ui3dsDrawStringToTexture(dst, desc,  0, RICH_DESC_Y,  w, tex->height, 0xFFFFFFFF);

    notif3dsUpload(UI_NOTIF_RICH, true);
}

static bool notif3dsPrepareBadgeUpload(const u16 *src, int srcW, int srcH) {
    if (!src || srcW != NOTIF_THUMB_DIM || srcH != NOTIF_THUMB_DIM)
        return false;

    C3D_Tex *tex = &GPU3DS.textures[UI_THUMB].tex;
    u16 *dst = (u16 *)notif3dsGetUploadSlice(UI_THUMB);

    memset(dst, 0, tex->size);
    img3dsUnswizzleRgb565(dst, tex->width, src, srcW, srcH);

    return true;
}

static void notif3dsGetNotificationText(Notif::Event event, char* out, size_t bufferSize) {
    switch (event) {
        case Notif::SaveState:
            snprintf(out, bufferSize, "Saved to Slot #%d", settings3DS.CurrentSaveSlot);
            break;
        case Notif::LoadState:
            snprintf(out, bufferSize, "Loaded Slot #%d", settings3DS.CurrentSaveSlot);
            break;
        case Notif::SavingState:
            snprintf(out, bufferSize, "Saving Slot #%d...", settings3DS.CurrentSaveSlot);
            break;
        case Notif::SlotChanged:
            snprintf(out, bufferSize, "Current Slot: #%d", settings3DS.CurrentSaveSlot);
            break;
        case Notif::ControllerSwapped:
            snprintf(out, bufferSize, "Controllers Swapped. Player #%d active.", Settings.SwapJoypads ? 2 : 1);
            break;
        case Notif::Screenshot:
            snprintf(out, bufferSize, "Screenshot saved to %s/screenshots/", settings3DS.RootDir);
            break;
        case Notif::FastForward:
            snprintf(out, bufferSize, "Fast Forward %s", settings3DS.TurboMode ? "enabled" : "disabled");
            break;
        case Notif::BrokenAudioLoad:
            snprintf(out, bufferSize, "Loaded - savestate may have broken audio");
            break;
        case Notif::Misc:
            snprintf(out, bufferSize, NOTIF_DEFAULT_ERROR);
            break;
        default:
            out[0] = '\0';
            break;
    }
}

static void notif3dsApplyStyle(PlainNotification &notif) {
    notif.textColor = 0xFFFFFFFF;
    u32 alpha = (u32)(0.85f * 255.0f);

    switch (notif.type) {
        case Notif::Type::Success:
            notif.backgroundColor = 0x13753A00 | alpha;
            break;
        case Notif::Type::Error:
            notif.backgroundColor = 0xDB3B2100 | alpha;
            break;
        case Notif::Type::Warning:
            notif.backgroundColor = 0xFF990000 | alpha;
            break;
        case Notif::Type::Info:
            notif.backgroundColor = 0x1F79D100 | alpha;
            break;
        default:
            notif.backgroundColor = 0xAA;
            break;
    }

    notif.borderColor = 0xFFFFFFFF;
    notif.borderSize  = notif.event != Notif::FPS ? 1 : 0;
    notif.paddingX    = notif.borderSize + 4;
    notif.paddingY    = notif.borderSize + 1;

    u16 maxHeight = NOTIF_TEXT_HEIGHT_MAX + notif.paddingY * 2;

    // bx0/bx1 are set in notif3dsDraw: both depend on textWidth
    notif.by0 = notif.event != Notif::FPS ? (SCREEN_HEIGHT - NOTIF_MARGIN - maxHeight) : NOTIF_MARGIN;
    notif.by1 = notif.by0 + maxHeight;
}


bool notif3dsInitialize() {
    if (!notif3dsInitTexture(UI_NOTIF_MSG, NOTIF_MSG_WIDTH_MAX, NOTIF_TEXT_HEIGHT_MAX))
        return false;

    if (!notif3dsInitTexture(UI_NOTIF_FPS, NOTIF_FPS_WIDTH_MAX, NOTIF_TEXT_HEIGHT_MAX))
        return false;

    if (!notif3dsInitTexture(UI_NOTIF_RICH, NOTIF_RICH_WIDTH_MAX, NOTIF_RICH_HEIGHT_MAX))
        return false;

    if (!notif3dsInitTexture(UI_THUMB, NOTIF_THUMB_DIM, NOTIF_THUMB_DIM, GPU_RGB565, true))
        return false;

    return notif3dsInitUploadBuffer();
}

void notif3dsFinalize() {
    if (notifUploadBuffer) {
        log3dsWrite("dealloc notif upload buffer");
        linearFree(notifUploadBuffer);
        notifUploadBuffer = NULL;
    }
}

void notif3dsTrigger(Notif::Event event, Notif::Type type, double durationInMs, const char *miscMessage) {
    notifMsg.event = event;
    notifMsg.type = type;

    if (event == Notif::Misc || event == Notif::RetroAchievement) {
        snprintf(notifMsg.text, sizeof(notifMsg.text), "%s", miscMessage != NULL ? miscMessage : NOTIF_DEFAULT_ERROR);
    } else {
        notif3dsGetNotificationText(event, notifMsg.text, sizeof(notifMsg.text));
    }

    notif3dsApplyStyle(notifMsg);

    notifMsg.visibleUntil = svcGetSystemTick() + (u64)(durationInMs * CPU_TICKS_PER_MSEC);

    notifMsg.dirty = true;
}

void notif3dsTriggerRich(const char *title, const char *desc,
                         double durationInMs, const u16 *badgePixels, int badgeW, int badgeH) {

    snprintf(notifRich.text,  sizeof(notifRich.text),  "%s", title ? title : "");
    snprintf(notifRich.text2, sizeof(notifRich.text2), "%s", desc  ? desc  : "");

    notifRich.active = true;
    notifRich.backgroundColor = 0x000000C0;

    notifRich.hasThumb = notif3dsPrepareBadgeUpload(badgePixels, badgeW, badgeH);

    notifRich.visibleUntil = svcGetSystemTick() + (u64)(durationInMs * CPU_TICKS_PER_MSEC);
    notifRich.dirty = true;
}

bool notif3dsRichVisible() {
    return notifRich.active && svcGetSystemTick() <= notifRich.visibleUntil;
}

void notif3dsHideRich() {
    notifRich.active = false;
    notifRich.visibleUntil = 0;
    notifRich.hasThumb = false;
}

void notif3dsFpsUpdate(float fps) {
    char newText[64];
    snprintf(newText, sizeof(newText), "%.1f", fps);

    if (strcmp(newText, notifFps.text) != 0) {
        snprintf(notifFps.text, sizeof(notifFps.text), "%s", newText);

        notifFps.event = Notif::FPS;
        notifFps.type = Notif::Type::Default;
        notif3dsApplyStyle(notifFps);

        notifFps.dirty = true;
    }
}

void notif3dsTick() {
    u64 now = svcGetSystemTick();

    if (notifMsg.event != Notif::None && now > notifMsg.visibleUntil)
        notifMsg.event = Notif::None;

    if (notifRich.active && now > notifRich.visibleUntil)
        notifRich.active = false;
}

void notif3dsSync() {
    if (notifMsg.event != Notif::None && notifMsg.dirty) {
        notifMsg.textWidth = notif3dsSyncTexture(UI_NOTIF_MSG, notifMsg.text, notifMsg.textColor);
        notifMsg.dirty = false;
    }

    if (settings3DS.ShowFPS && notifFps.dirty) {
        notifFps.textWidth = notif3dsSyncTexture(UI_NOTIF_FPS, notifFps.text, 0xFFFFFFFF);
        notifFps.dirty = false;
    }

    if (notifRich.active && notifRich.dirty) {
        if (notifRich.hasThumb)
            notif3dsUpload(UI_THUMB, false);
        notif3dsSyncRichText();
        notifRich.dirty = false;
    }
}

bool notif3dsIsVisible(SGPU_TEXTURE_ID textureId) {
    // The pause menu owns the game screen; keep gameplay overlays out of its frame.
    bool paused = GPU3DS.emulatorState == EMUSTATE_PAUSEMENU;

    if (textureId == UI_NOTIF_FPS) {
        return settings3DS.ShowFPS && !paused;
    }
    if (textureId == UI_NOTIF_RICH) {
        return notifRich.active && !paused;
    }

    return notifMsg.event != Notif::None;
}

void notif3dsHide() {
    notifMsg.event = Notif::None;
    notifMsg.visibleUntil = 0;
}

void notif3dsDrawRich(float xOffset) {
    if (!notif3dsIsVisible(UI_NOTIF_RICH)) return;

    C3D_Tex *richTex = &GPU3DS.textures[UI_NOTIF_RICH].tex;
    int wx = richTex->width - 1;
    int wy = richTex->height - 1;

    int titleW = notifRich.textWidth;
    int textW = titleW > notifRich.text2Width ? titleW : notifRich.text2Width;
    if (textW < RICH_MIN_TEXT_W) textW = RICH_MIN_TEXT_W;

    int boxW = RICH_CHROME_W + textW;
    int maxBoxW = settings3DS.GameScreenWidth - RICH_MARGIN * 2;
    int snapGap = maxBoxW / RICH_FULL_WIDTH_SNAP_DIVISOR;
    if (boxW > maxBoxW - snapGap) boxW = maxBoxW;
    int boxH = RICH_THUMB;

    const int boxX = RICH_MARGIN, boxY = RICH_MARGIN;

    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];

    int thumbX = boxX;
    int thumbY = boxY;
    int textBoxX = thumbX + RICH_THUMB;

    if (notifRich.hasThumb) {
        gpu3dsAddSimpleQuadVertexes(thumbX + xOffset, thumbY, thumbX + xOffset + RICH_THUMB, thumbY + RICH_THUMB,
                                    0, 0, NOTIF_THUMB_DIM, NOTIF_THUMB_DIM, 0, 0xFFFFFFFF);
        GPU3DS.currentRenderState.textureBind = UI_THUMB;
        GPU3DS.currentRenderState.textureEnv = TEX_ENV_REPLACE_TEXTURE0;
        GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;
        gpu3dsDraw(list, NULL, list->count);
    }

    // Draw background, placeholder, and text from UI_NOTIF_RICH in one batch.
    gpu3dsAddQuadRect(textBoxX + xOffset, boxY, boxX + xOffset + boxW, boxY + boxH,
                      wx, wy, 0, notifRich.backgroundColor, 0, 0);

    if (!notifRich.hasThumb) {
        gpu3dsAddQuadRect(thumbX + xOffset, thumbY, thumbX + xOffset + RICH_THUMB, thumbY + RICH_THUMB,
            wx, wy, 0, 0xBBBBBBFF, 0, 0);
    }

    int textX = textBoxX + RICH_PAD;
    int titleY = boxY + 4;
    int descY = titleY + RICH_TITLE_FONT_HEIGHT + 4;

    gpu3dsAddSimpleQuadVertexes(textX + xOffset, titleY, textX + xOffset + titleW, titleY + RICH_TITLE_FONT_HEIGHT,
                                0, RICH_TITLE_Y, titleW, RICH_TITLE_Y + RICH_TITLE_FONT_HEIGHT, 0, 0xFFFFFFFF);
    gpu3dsAddSimpleQuadVertexes(textX + xOffset, descY, textX + xOffset + notifRich.text2Width, descY + FONT_HEIGHT,
                                0, RICH_DESC_Y, notifRich.text2Width, RICH_DESC_Y + FONT_HEIGHT, 0, 0xDDDDDDFF);

    GPU3DS.currentRenderState.textureBind = UI_NOTIF_RICH;
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_MODULATE_COLOR;
    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;
    gpu3dsDraw(list, NULL, list->count);
}

void notif3dsDraw(SGPU_TEXTURE_ID textureId, float xOffset) {
    if (!notif3dsIsVisible(textureId)) return;

    SGPUTexture *texture = &GPU3DS.textures[textureId];
    PlainNotification &notif = textureId == UI_NOTIF_MSG ? notifMsg : notifFps;


    int boxW = notif.textWidth + notif.paddingX * 2;
    notif.bx0 = textureId == UI_NOTIF_FPS ? settings3DS.GameScreenWidth - NOTIF_MARGIN - boxW
                                          : NOTIF_MARGIN;
    notif.bx1 = notif.bx0 + boxW;

    float x0 = notif.bx0 + notif.paddingX + xOffset;
    int   y0 = notif.by0 + notif.paddingY;
    float x1 = x0 + notif.textWidth;
    int   y1 = y0 + NOTIF_TEXT_HEIGHT_MAX;

    int wx = texture->tex.width - 1;
    int wy = texture->tex.height - 1;

    gpu3dsAddQuadRect(
        notif.bx0 + xOffset, notif.by0, notif.bx1 + xOffset, notif.by1, wx, wy, 0,
        notif.backgroundColor, notif.borderColor, notif.borderSize
    );

    gpu3dsAddSimpleQuadVertexes(
        x0, y0, x1, y1,
        0, 0, notif.textWidth, NOTIF_TEXT_HEIGHT_MAX,
        0, 0xFFFFFFFF
    );

    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];

    GPU3DS.currentRenderState.textureBind = textureId;
    GPU3DS.currentRenderState.textureEnv = TEX_ENV_MODULATE_COLOR;
    GPU3DS.currentRenderState.alphaBlending = ALPHA_BLENDING_ENABLED;

    gpu3dsDraw(list, NULL, list->count);
}
