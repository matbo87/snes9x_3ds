
#ifndef _3DSUI_NOTIF_H_
#define _3DSUI_NOTIF_H_

#include <3ds.h>

#define NOTIF_TEXT_WIDTH_MAX 256
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
        Paused, // custom style, persistent overlay
        Misc, // e.g. error messages
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
void notif3dsTick();
void notif3dsSyncTexture();
bool notif3dsIsVisible();
void notif3dsHide();
void notif3dsDraw(gfxScreen_t screen);
void notif3dsFinalize();

#endif
