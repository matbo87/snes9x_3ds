#include <cstdio>
#include <cstring>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>

#include "3dssettings.h"
#include "3dslog.h"
#include "3dsimpl_gpu.h"
#include "3dsui.h"
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
} UINotification;

static UINotification notifMsg = {0};
static UINotification notifFps = {0};

static bool notif3dsInitTexture(SGPU_TEXTURE_ID id, int maxWidth, int maxHeight) {
    SGPUTexture *texture = &GPU3DS.textures[id];
    GPU_TEXCOLOR fmt = GPU_RGBA4;

    int width = gpu3dsGetNextPowerOf2(maxWidth);
    int height = gpu3dsGetNextPowerOf2(maxHeight);

    if (!C3D_TexInitVRAM(&texture->tex, width, height, fmt)) {
        return false;
    }

    texture->id = id;

    // we want pixel-perfect text, so no linear interpolation
    C3D_TexSetFilter(&texture->tex, GPU_NEAREST, GPU_NEAREST);

    texture->scale[3] = 1.0f / texture->tex.width;
    texture->scale[2] = 1.0f / texture->tex.height;
    texture->scale[1] = 0;
    texture->scale[0] = 0;

    C3D_Tex *tex = &texture->tex;
    log3dsWrite("ui texture \"%s\" dim: %dx%d, size:%.2fkb, format: %s",
        SGPUTextureIDToString(id),
        tex->width, tex->height,
        (float)tex->size / 1024,
        SGPUTexColorToString(tex->fmt)
    );

    return true;
}

static u16 notif3dsSyncTexture(SGPU_TEXTURE_ID id, const char *text, u32 color) {
    SGPUTexture *texture = &GPU3DS.textures[id];
    C3D_Tex *tex = &texture->tex;
    u16 *dst = (u16 *)g_texUploadBuffer;

    memset(g_texUploadBuffer, 0, tex->size);

    // 2x2 white area at bottom-right for batch-rendering (text + background)
    // ensures correct sampling with both GPU_NEAREST and GPU_LINEAR
    int w = tex->width;
    int h = tex->height;
    dst[(h - 1) * w + (w - 1)] = 0xFFFF;
    dst[(h - 1) * w + (w - 2)] = 0xFFFF;
    dst[(h - 2) * w + (w - 1)] = 0xFFFF;
    dst[(h - 2) * w + (w - 2)] = 0xFFFF;

    u16 textWidth = ui3dsDrawStringToTexture(
        dst, text,
        0, 0, tex->width, tex->height,
        color
    );

    GSPGPU_FlushDataCache(g_texUploadBuffer, tex->size);

    C3D_SyncDisplayTransfer(
        (u32 *)g_texUploadBuffer, GX_BUFFER_DIM(tex->width, tex->height),
        (u32 *)tex->data,         GX_BUFFER_DIM(tex->width, tex->height),
        GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_FLIP_VERT(1) |
        GX_TRANSFER_IN_FORMAT(tex->fmt) | GX_TRANSFER_OUT_FORMAT(tex->fmt)
    );

    return textWidth;
}

static void notif3dsGetNotificationText(Notif::Event event, char* out, size_t bufferSize) {
    switch (event) {
        case Notif::SaveState:
            snprintf(out, bufferSize, "Saved to Slot #%d", settings3DS.CurrentSaveSlot);
            break;
        case Notif::LoadState:
            snprintf(out, bufferSize, "Loaded Slot #%d", settings3DS.CurrentSaveSlot);
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
            snprintf(out, bufferSize, "Fast Forward enabled");
            break;
        case Notif::Paused:
            snprintf(out, bufferSize, "\x13\x14\x15\x16\x16 \x0e\x0f\x10\x11\x12 \x17\x18 \x14\x15\x16\x19\x1a\x15");
            break;
        case Notif::Misc:
            snprintf(out, bufferSize, NOTIF_DEFAULT_ERROR);
            break;
        default:
            out[0] = '\0';
            break;
    }
}

static void notif3dsApplyStyle(UINotification &notif, gfxScreen_t screen) {
    notif.textColor = 0xFFFFFFFF;
    u32 alpha = (u32)(0.85f * 255.0f);

    switch (notif.type) {
        case Notif::Type::Success:
            notif.backgroundColor = 0x13753A00 | alpha;
            break;
        case Notif::Type::Error:
            notif.backgroundColor = 0xDB3B2100 | alpha;
            break;

        case Notif::Type::Info:
            notif.backgroundColor = 0x1F79D100 | alpha;
            break;
        default:
            notif.backgroundColor = 0xAA;
            break;
    }

    if (notif.event == Notif::Paused) {
        notif.borderSize = 0;
        notif.borderColor = 0;
        notif.paddingX = 0;
        notif.paddingY = notif.borderSize + 16;

        notif.bx0 = 0;
		notif.by0 = (SCREEN_HEIGHT - NOTIF_TEXT_HEIGHT_MAX) / 2;
        notif.bx1 = notif.bx0 + (screen == GFX_TOP ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH);
        notif.by1 = notif.by0 + NOTIF_TEXT_HEIGHT_MAX + notif.paddingY * 2;

        return;
    }

    notif.borderColor = 0xFFFFFFFF;
    notif.borderSize  = notif.event != Notif::FPS ? 1 : 0;
    notif.paddingX    = notif.borderSize + 4;
    notif.paddingY    = notif.borderSize + 1;

    int margin = 8;
    u16 maxHeight = NOTIF_TEXT_HEIGHT_MAX + notif.paddingY * 2;

    notif.bx0 = margin;
    // bx1 is set in notif3dsDraw, because it' based on textWidth
    notif.by0 = notif.event != Notif::FPS ? (SCREEN_HEIGHT - margin - maxHeight) : margin; 
    notif.by1 = notif.by0 + maxHeight;
}


bool notif3dsInitialize() {
    if (!notif3dsInitTexture(UI_NOTIF_MSG, NOTIF_MSG_WIDTH_MAX, NOTIF_TEXT_HEIGHT_MAX))
        return false;

    if (!notif3dsInitTexture(UI_NOTIF_FPS, NOTIF_FPS_WIDTH_MAX, NOTIF_TEXT_HEIGHT_MAX))
        return false;
        
    return true;
}

void notif3dsTrigger(Notif::Event event, Notif::Type type, gfxScreen_t screen, double durationInMs, const char *miscMessage) {
    notifMsg.event = event;
    notifMsg.type = type;

    if (event == Notif::Misc) {
        snprintf(notifMsg.text, sizeof(notifMsg.text), "%s", miscMessage != NULL ? miscMessage : NOTIF_DEFAULT_ERROR);
    } else {
        notif3dsGetNotificationText(event, notifMsg.text, sizeof(notifMsg.text));
    }

    notif3dsApplyStyle(notifMsg, screen);
    u64 durationTicks = (u64)(durationInMs * CPU_TICKS_PER_MSEC);
    notifMsg.visibleUntil = svcGetSystemTick() + durationTicks;

    notifMsg.dirty = true;
}

// basically like notif3dsTrigger but only for the FPS overlay, 
// which has a different style and is updated every frame when enabled
void notif3dsFpsUpdate(float fps, gfxScreen_t screen) {
    char newText[64];
    snprintf(newText, sizeof(newText), "%.1f", fps);

    if (strcmp(newText, notifFps.text) != 0) {
        snprintf(notifFps.text, sizeof(notifFps.text), "%s", newText);

        notifFps.event = Notif::FPS;
        notifFps.type = Notif::Type::Default;
        notif3dsApplyStyle(notifFps, screen);
        
        notifFps.dirty = true;
    }
}

void notif3dsTick() {
    if (notifMsg.event == Notif::None) return;

    // if the current time has passed our target time, hide it
    if (svcGetSystemTick() > notifMsg.visibleUntil) {
        notifMsg.event = Notif::None;
    }
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
}

bool notif3dsIsVisible(SGPU_TEXTURE_ID textureId) {
    if (textureId == UI_NOTIF_FPS) {
        return settings3DS.ShowFPS;
    }
    
    return notifMsg.event != Notif::None;
}

void notif3dsHide() {
    notifMsg.event = Notif::None;
    notifMsg.visibleUntil = 0;
}

void notif3dsDraw(SGPU_TEXTURE_ID textureId, gfxScreen_t screen, float xOffset) {
    if (!notif3dsIsVisible(textureId)) return;

    SGPUTexture *texture = &GPU3DS.textures[textureId];
    UINotification &notif = textureId == UI_NOTIF_MSG ? notifMsg : notifFps;

    float x0, x1;
    int y0, y1;
    int screenWidth = screen == GFX_TOP ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH;

    if (notif.event == Notif::Paused) {
        x0 = (screenWidth - notif.textWidth) / 2 + xOffset;
        y0 = notif.by0 + notif.paddingY;
        x1 = x0 + notif.textWidth;
        y1 = y0 + NOTIF_TEXT_HEIGHT_MAX;
    } else {
        notif.bx1 = notif.bx0 + notif.textWidth + notif.paddingX * 2;

        x0 = notif.bx0 + notif.paddingX + xOffset;
        y0 = notif.by0 + notif.paddingY;
        x1 = x0 + notif.textWidth;
        y1 = y0 + NOTIF_TEXT_HEIGHT_MAX;
    }

    int wx = texture->tex.width - 1;
    int wy = texture->tex.height - 1;

    // extend full-width backgrounds to prevent exposed edges when shifted
    float abs_xOffset = xOffset < 0 ? -xOffset : xOffset;
    float bx0 = notif.bx0 + xOffset - (notif.event == Notif::Paused ? abs_xOffset : 0);
    float bx1 = notif.bx1 + xOffset + (notif.event == Notif::Paused ? abs_xOffset : 0);

    gpu3dsAddQuadRect(
        bx0, notif.by0, bx1, notif.by1, wx, wy, 0,
        notif.backgroundColor, notif.borderColor, notif.borderSize
    );

    // notif text
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

void notif3dsFinalize() {
    gpu3dsDestroyTexture(&GPU3DS.textures[UI_NOTIF_MSG]);
    gpu3dsDestroyTexture(&GPU3DS.textures[UI_NOTIF_FPS]);
}
