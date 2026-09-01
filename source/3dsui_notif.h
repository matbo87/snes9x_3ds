
#ifndef _3DSUI_NOTIF_H_
#define _3DSUI_NOTIF_H_

#include <3ds.h>

#define NOTIF_MSG_WIDTH_MAX 256
#define NOTIF_FPS_WIDTH_MAX 64
#define NOTIF_TEXT_HEIGHT_MAX 16

#define NOTIF_RICH_WIDTH_MAX 512
#define NOTIF_RICH_HEIGHT_MAX 32
#define NOTIF_BADGE_DIM 64

// Separate textures avoid whole-texture transfers across independent badge lifetimes.
#define NOTIF_RA_CHALLENGES 2
#define NOTIF_INDICATOR_WIDTH_MAX 128
#define NOTIF_INDICATOR_HEIGHT_MAX 32

#define NOTIF_DEFAULT_DURATION 1200
#define NOTIF_DEFAULT_ERROR "Error. Something went wrong."

namespace Notif {
    enum Event {
        None = -1,
        SaveState,
        LoadState,
        SavingState,   // in-progress, shown before the blocking save
        SlotChanged,
        ControllerSwapped,
        Screenshot,
        FastForward,
        BrokenAudioLoad,
        RetroAchievement,
        Misc, // e.g. error messages
        FPS, // persistent overlay (top-left), separate texture
        Count
    };

    enum Type {
        Success,
        Error,
        Warning,
        Info,
        Default,
    };
}

bool notif3dsInitialize();
void notif3dsFinalize();
void notif3dsTrigger(Notif::Event event, Notif::Type type, double durationInMs = NOTIF_DEFAULT_DURATION, const char *miscMessage = NULL);
void notif3dsFpsUpdate(float fps);
void notif3dsTick();
void notif3dsSync();
void notif3dsHide();
void notif3dsDraw(SGPU_TEXTURE_ID textureId, float xOffset = 0.0f);

// RA rich toast: Title/desc must already be glyph-encoded.
void notif3dsTriggerRich(const char *title, const char *desc,
                         double durationInMs, const u16 *badgePixels, int badgeW, int badgeH);
void notif3dsDrawRich(float xOffset = 0.0f);
void notif3dsHideRich();
bool notif3dsRichVisible();

void notif3dsSetProgressBadge(const u16 *badgePixels, int badgeW, int badgeH);
void notif3dsSetChallengeBadge(int index, const u16 *badgePixels, int badgeW, int badgeH);
void notif3dsSetIndicators(int challengeCount, int challengeOverflow,
                           u32 progressId, const char *progressText);
void notif3dsDrawIndicators(float xOffset = 0.0f);

#endif
