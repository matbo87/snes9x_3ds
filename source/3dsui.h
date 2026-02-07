
#ifndef _3DSUI_H_
#define _3DSUI_H_

#include <string>
#include <cstdint>
#include <3ds.h>

typedef struct Bounds {
    int left;
    int top;
    int right;
    int bottom;
};

enum class Position {
    TL,
    TC,
    TR,
    ML,
    MC,
    MR,
    BL,
    BC,
    BR,
};

typedef enum {
    NOTIFICATION_DEFAULT,
    NOTIFICATION_SUCCESS,
    NOTIFICATION_ERROR,
} UI_NotificationType;

void ui3dsUpdateScreenSettings(gfxScreen_t gameScreen);
void ui3dsInitialize();
void ui3dsSetFont(int fontIndex);

void ui3dsSetViewport(int x1, int y1, int x2, int y2);
void ui3dsPushViewport(int x1, int y1, int x2, int y2);
void ui3dsPopViewport();
void ui3dsSetTranslate(int tx, int ty);

int ui3dsApplyAlphaToColor(int color, float alpha, bool rgb8 = false);

void ui3dsDrawRect(int x0, int y0, int x1, int y1, int color, float alpha = 1.0f);
void ui3dsDrawCheckerboard(int x0, int y0, int x1, int y1, int color1, int color2);
int ui3dsOverlayBlendColor(int backgroundColor, int foregroundColor);

void ui3dsDrawStringWithWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);
int ui3dsDrawStringWithNoWrapping(gfxScreen_t targetScreen, int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);

int ui3dsGetScreenWidth(gfxScreen_t targetScreen);
Bounds ui3dsGetBounds(int screenWidth, int width, int height, Position position, int offsetX, int offsetY);

void ui3dsDrawPauseText();

void ui3dsTriggerNotification(const char* text, UI_NotificationType type = NOTIFICATION_DEFAULT, double durationInMs = 1200);
void ui3dsUpdateNotification(bool isEnabled);
void ui3dsDrawNotificationOverlay();
void ui3dsDrawNotificationText();
bool ui3dsNotificationIsVisible();

#define HALIGN_LEFT     -1
#define HALIGN_CENTER   0
#define HALIGN_RIGHT    1

#define FONT_HEIGHT     13
#define PADDING         10

extern int bounds[10];

#endif