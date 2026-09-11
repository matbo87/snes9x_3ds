#ifndef _3DSGLYPHS_H_
#define _3DSGLYPHS_H_

#include <3ds.h>
#include <stddef.h>

static const char UI_TEXT_SECTION_SEPARATOR = '\v';

// Reserved CP1252 slots.
// Original characters are dropped
// or mapped to a lookalike in glyph3dsEncodeUtf8.
typedef enum UiIcon {
    // 0 (NUL)
    UI_ICON_FOLDER = 1,
    UI_ICON_TROPHY = 2,
    UI_ICON_STACK = 3,
    UI_ICON_FLAG_CHECKERED = 4,
    UI_ICON_LOCK = 5,
    UI_ICON_CLOCK = 6,
    UI_ICON_INFO = 7,
    UI_ICON_CHART = 8,
    UI_ICON_CROWN = 9,      // \t
    // 10 (\n -> UI line break)
    // 11 (\v -> UI_TEXT_SECTION_SEPARATOR)
    UI_ICON_PEOPLE = 12,
    UI_ICON_STAR = 13,      // \r
    UI_ICON_HEART = 14,
    UI_ICON_TIMER = 15,
    UI_ICON_BOMB = 16,
    UI_ICON_KEY = 17,
    UI_ICON_REPEAT = 18,
    UI_ICON_COMBAT = 19,
    UI_ICON_FLAG = 20,
    UI_ICON_PERSON = 21,
    UI_ICON_PIN = 22,
    UI_ICON_SKULL = 23,
    UI_ICON_SAVE = 24,
    UI_ICON_EXIT = 25,
    UI_ICON_MONEY = 26,
    UI_ICON_ROCKET = 27,
    UI_ICON_MEDAL = 28,
    UI_ICON_SPEECH_BUBBLE = 29,
    
    UI_ICON_UNUSED_30 = 30,
    UI_ICON_UNUSED_31 = 31,

    UI_ICON_FILE_ERROR = 127,           // DEL
    UI_ICON_INVALID = 129,
    UI_ICON_CHECKMARK = 131,            // ƒ
    UI_ICON_RADIO_BTN = 136,            // ˆ
    UI_ICON_RADIO_BTN_SELECTED = 137,   // ‰
    UI_ICON_SORT = 141,
    UI_ICON_CHEVRON_UP = 143,
    UI_ICON_CHEVRON_DOWN = 144,
    UI_ICON_BULLET_5 = 149,             // •
    UI_ICON_PROGRESS = 152,             // ˜
    UI_ICON_BULLET_6 = 157,
    UI_ICON_PILL = 160,                 // NBSP
    UI_ICON_BAR_FILLED = 164,           // ¤
    UI_ICON_BAR_EMPTY = 166,            // ¦
    UI_ICON_BUTTON_A = 168,             // ¨
    UI_ICON_BUTTON_B = 172,             // ¬
    UI_ICON_BUTTON_X = 173,
    UI_ICON_BUTTON_Y = 175,             // ¯

    UI_ICON_CIRCLE_MARK = 180,          // ´
    UI_ICON_UNUSED_184 = 184,           // ¸
} UiIcon;

void glyph3dsEncodeUtf8(char *dst, size_t dstSize, const char *src);

#endif
