
#ifndef _3DSUI_NOTIF_H_
#define _3DSUI_NOTIF_H_

#include <3ds.h>

#define NOTIF_MSG_WIDTH_MAX 256
#define NOTIF_FPS_WIDTH_MAX 64
#define NOTIF_TEXT_HEIGHT_MAX 16

#define NOTIF_DEFAULT_DURATION 1200
#define NOTIF_DEFAULT_ERROR "Error. Something went wrong."

namespace Notif {
    enum Event {
        None = -1,
        SaveState,
        LoadState,
        SlotChanged,
        ControllerSwapped,
        Screenshot,
        FastForward,
        Misc, // e.g. error messages
        Paused, // custom style, persistent overlay
        FPS, // persistent overlay (top-left), separate texture
        Count
    };

    enum Type {
        Success,
        Error,
        Info,
        Default,
    };
}

bool notif3dsInitialize();
void notif3dsTrigger(Notif::Event event, Notif::Type type, gfxScreen_t screen, double durationInMs = NOTIF_DEFAULT_DURATION, const char *miscMessage = NULL);
void notif3dsFpsUpdate(float fps, gfxScreen_t screen);
void notif3dsTick();
void notif3dsSync();
void notif3dsHide();
void notif3dsDraw(SGPU_TEXTURE_ID textureId, gfxScreen_t screen, float xOffset = 0.0f);

#endif
