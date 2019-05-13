
#ifndef _3DSUI_H_
#define _3DSUI_H_

void ui3dsUpdateScreenSettings(gfxScreen_t gameScreen);
void ui3dsInitialize();
void ui3dsSetFont(int fontIndex);

void ui3dsSetViewport(int x1, int y1, int x2, int y2);
void ui3dsPushViewport(int x1, int y1, int x2, int y2);
void ui3dsPopViewport();
void ui3dsSetTranslate(int tx, int ty);

void ui3dsSetColor(int newForeColor, int newBackColor);
int ui3dsApplyAlphaToColor(int color, float alpha, bool rgb8 = false);

void ui3dsDrawRect(int x0, int y0, int x1, int y1);
void ui3dsDrawRect(int x0, int y0, int x1, int y1, int color, float alpha = 1.0f);
void ui3dsDraw32BitRect(uint32 * fb, int x0, int y0, int x1, int y1, int color, float alpha = 1.0f);

void ui3dsDrawStringWithWrapping(int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);
void ui3dsDrawStringWithNoWrapping(int x0, int y0, int x1, int y1, int color, int horizontalAlignment, const char *buffer);

void ui3dsCopyFromFrameBuffer(uint16 *destBuffer);
void ui3dsBlitToFrameBuffer(uint16 *srcBuffer, float alpha = 1.0f);
void ui3dsUpdateScreenBuffer(gfxScreen_t targetScreen, bool isDialog = false);
void ui3dsRenderScreenImage(gfxScreen_t targetScreen, const char *imgFilePath, bool imageFileUpdated);
void ui3dsResetScreenImage();
bool ui3dsScreenImageRendered();

#define HALIGN_LEFT     -1
#define HALIGN_CENTER   0
#define HALIGN_RIGHT    1

#define FONT_HEIGHT     13
#define PADDING         10

// bounds for second screen image / dialog
#define B_TOP           0
#define B_BOTTOM        1
#define B_RIGHT         2
#define B_LEFT          3
#define B_HCENTER       4
#define B_VCENTER       5
#define B_DTOP          6
#define B_DBOTTOM       7
#define B_DRIGHT        8
#define B_DLEFT         9

extern int bounds[10];

typedef enum
{
    HIDDEN = 0,
	VISIBLE = 1,
	WAIT = 2,
 } dialog_state;

typedef struct
{
	int             Padding = 10;
	int             MaxLines = 2;
	int             Height = 13 * MaxLines + Padding * 2;
    int             Color = 0x4CAF50;
    float           Alpha = 0.8f;
    dialog_state    State = HIDDEN;
} SecondScreenDialog;

extern SecondScreenDialog secondScreenDialog;

#endif