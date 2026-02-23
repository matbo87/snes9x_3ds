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

static UINotification notif = {0};

static void ui3dsGetNotificationText(Notif::Event event, char* out, size_t bufferSize) {
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
            snprintf(out, bufferSize, "Fast Forward active");
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
    notif.borderSize  = 1;
    notif.paddingX    = notif.borderSize + 4;
    notif.paddingY    = notif.borderSize + 1;

    int margin = 8;
    u16 maxHeight = NOTIF_TEXT_HEIGHT_MAX + notif.paddingY * 2;

    notif.bx0 = margin;
    // bx1 is set in notif3dsDraw, because it' based on textWidth
    notif.by0 = SCREEN_HEIGHT - margin - maxHeight;
    notif.by1 = notif.by0 + maxHeight;
}


bool notif3dsInitialize() {
    SGPUTexture *texture = &GPU3DS.textures[UI_NOTIF];
    GPU_TEXCOLOR fmt = GPU_RGBA4;

    int width = gpu3dsGetNextPowerOf2(NOTIF_TEXT_WIDTH_MAX);
    int height = gpu3dsGetNextPowerOf2(NOTIF_TEXT_HEIGHT_MAX);

    if (!C3D_TexInitVRAM(&texture->tex, width, height, fmt)) {
        return false;
    }

    texture->id = UI_NOTIF;
    GPU_TEXTURE_FILTER_PARAM filter = GPU_NEAREST;
    C3D_TexSetFilter(&texture->tex, filter, filter);

    texture->scale[3] = 1.0f / texture->tex.width;  // x
    texture->scale[2] = 1.0f / texture->tex.height; // y
    texture->scale[1] = 0; // z
    texture->scale[0] = 0; // w

    C3D_Tex *tex = &texture->tex;

    log3dsWrite("ui texture \"%s\" dim: %dx%d, size:%.2fkb, format: %s",
        SGPUTextureIDToString(texture->id),
        tex->width, tex->height,
        (float)tex->size / 1024,
        SGPUTexColorToString(texture->tex.fmt)
    );

    return true;
}

void notif3dsTrigger(Notif::Event event, Notif::Type type, gfxScreen_t screen, double durationInMs, const char *miscMessage) {
    notif.event = event;
    notif.type = type;

    if (event == Notif::Misc) {
        snprintf(notif.text, sizeof(notif.text), "%s", miscMessage != NULL ? miscMessage : NOTIF_DEFAULT_ERROR);
    } else {
        ui3dsGetNotificationText(event, notif.text, sizeof(notif.text));
    }

    notif3dsApplyStyle(notif, screen);
    u64 durationTicks = (u64)(durationInMs * CPU_TICKS_PER_MSEC);
    notif.visibleUntil = svcGetSystemTick() + durationTicks;

    notif.dirty = true;
}

void notif3dsTick() {
    if (notif.event == Notif::None) return;

    // if the current time has passed our target time, hide it
    if (svcGetSystemTick() > notif.visibleUntil) {
        notif.event = Notif::None;
    }
}

void notif3dsSyncTexture() {
    if (notif.event == Notif::None || !notif.dirty) return;
    
    SGPUTexture *texture = &GPU3DS.textures[UI_NOTIF];
    C3D_Tex *tex = &texture->tex;
    u16 *dst = (u16 *)g_texUploadBuffer;

    memset(g_texUploadBuffer, 0, tex->size);
    
    // plant white pixel at the bottom-right corner to use for batch-rendering (text + background)
    dst[((tex->height - 1) * tex->width) + tex->width - 1] = 0xFFFF;
    
    notif.textWidth = ui3dsDrawStringToTexture(
        dst,
        notif.text,
        0, 0,
        tex->width, tex->height,
        notif.textColor
    );

    notif.dirty = false;

    GSPGPU_FlushDataCache(g_texUploadBuffer, tex->size);

    C3D_SyncDisplayTransfer(
        (u32 *)g_texUploadBuffer, GX_BUFFER_DIM(tex->width, tex->height),
        (u32 *)tex->data,         GX_BUFFER_DIM(tex->width, tex->height),
        GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_FLIP_VERT(1) |
        GX_TRANSFER_IN_FORMAT(tex->fmt) | GX_TRANSFER_OUT_FORMAT(tex->fmt)
    );
}

bool notif3dsIsVisible() {
    return notif.event != Notif::None;
}

void notif3dsHide() {
    notif.event = Notif::None;
    notif.visibleUntil = 0;
}

void notif3dsDraw(gfxScreen_t screen) {
    if (!notif3dsIsVisible()) return;

    SGPUTexture *texture = &GPU3DS.textures[UI_NOTIF];
    SVertexList *list = &GPU3DS.vertices[VBO_SCREEN];

    int x0, y0, x1, y1;
    int screenWidth = screen == GFX_TOP ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH;

    if (notif.event == Notif::Paused) {
        x0 = (screenWidth - notif.textWidth) / 2;
        y0 = notif.by0 + notif.paddingY;
        x1 = x0 + notif.textWidth;
        y1 = y0 + NOTIF_TEXT_HEIGHT_MAX;
    } else {
        notif.bx1 = notif.bx0 + notif.textWidth + notif.paddingX * 2;
        
        x0 = notif.bx0 + notif.paddingX;
        y0 = notif.by0 + notif.paddingY;
        x1 = x0 + notif.textWidth;
        y1 = y0 + NOTIF_TEXT_HEIGHT_MAX;
    }

    int wx = texture->tex.width - 1;
    int wy = texture->tex.height - 1;

    // notif background
    gpu3dsAddQuadRect(
        notif.bx0, notif.by0, notif.bx1, notif.by1, wx, wy, 0, 
        notif.backgroundColor, notif.borderColor, notif.borderSize
    );
    
    int tx0 = 0;
    int ty0 = 0;
    int tx1 = notif.textWidth;
    int ty1 = ty0 + NOTIF_TEXT_HEIGHT_MAX;

    // notif text
    gpu3dsAddSimpleQuadVertexes(
        x0, y0, x1, y1,
        tx0, ty0, tx1, ty1,
        0, 0xFFFFFFFF
    );

    SGPURenderState renderState = GPU3DS.currentRenderState;

    renderState.textureBind = UI_NOTIF;
    renderState.textureEnv = TEX_ENV_MODULATE_COLOR;
    renderState.alphaBlending = ALPHA_BLENDING_ENABLED;

    gpu3dsUpdateRenderStateIfChanged(&GPU3DS.currentRenderState, FLAG_TEXTURE_BIND | FLAG_TEXTURE_ENV | FLAG_ALPHA_BLENDING, &renderState);
    gpu3dsDraw(list, NULL, list->count);
}

void notif3dsFinalize() {
    gpu3dsDestroyTexture(&GPU3DS.textures[UI_NOTIF]);
}
