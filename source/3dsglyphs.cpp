#include "3dsglyphs.h"

// Unicode -> CP1252 byte. Latin-1 is handled by fast paths in the encoder.
// 0 means unmapped.
static u8 ui3dsUnicodeToCp1252(u32 cp)
{
    switch (cp) {
        // CP1252 0x80-0x9F block
        case 0x20AC: return 0x80; // €
        case 0x201A: return 0x82; // ‚
        case 0x201E: return 0x84; // „
        case 0x2026: return 0x85; // …
        case 0x2020: return 0x86; // †
        case 0x2021: return 0x87; // ‡
        case 0x0160: return 0x8A; // Š
        case 0x2039: return 0x8B; // ‹
        case 0x0152: return 0x8C; // Œ
        case 0x017D: return 0x8E; // Ž
        case 0x2018: return 0x91; // ‘
        case 0x2019: return 0x92; // ’
        case 0x201C: return 0x93; // “
        case 0x201D: return 0x94; // ”
        case 0x2022: return 0x95; // •
        case 0x2013: return 0x96; // –
        case 0x2014: return 0x97; // —
        case 0x2122: return 0x99; // ™
        case 0x0161: return 0x9A; // š
        case 0x203A: return 0x9B; // ›
        case 0x0153: return 0x9C; // œ
        case 0x017E: return 0x9E; // ž
        case 0x0178: return 0x9F; // Ÿ

        // Reuse near-identical CP1252 glyph
        case 0x0192: return 'f';  // ƒ
        case 0x02C6: return '^';  // ˆ
        case 0x02DC: return '~';  // ˜
        case 0x029F: return 'L';  // ʟ
        case 0x1D20: return 'V';  // ᴠ
        case 0x2212: return '-';  // −
        case 0x2010: return '-';  // ‐
        case 0x2011: return '-';  // ‑
        case 0x2032: return '\''; // ′
        case 0x2033: return '"';  // ″
        case 0x2044: return '/';  // ⁄
        case 0x2219: return 0x95; // ∙ -> •
        case 0x22C5: return 0xB7; // ⋅ -> ·
        case 0x2E31: return 0xB7; // ⸱ -> ·
    }
    return 0;
}

static u8 ui3dsUnicodeToIcon(u32 cp)
{
    switch (cp) {
        case 0x2B50:  // ⭐
        case 0x2734:  // ✴
        case 0x2606:  // ☆
        case 0x2728:  // ✨
        case 0x1F31F: return UI_ICON_STAR; // 🌟

        case 0x2764:  // ❤
        case 0x1F49E: // 💞
        case 0x1F496: return UI_ICON_HEART; // 💖

        case 0x1F6A9: return UI_ICON_FLAG;           // 🚩
        case 0x1F3C1: return UI_ICON_FLAG_CHECKERED; // 🏁

        case 0x1F511: // 🔑
        case 0x1F5DD: return UI_ICON_KEY; // 🗝

        case 0x1F501: return UI_ICON_REPEAT; // 🔁

        case 0x1F4B0: // 💰
        case 0x1FA99: return UI_ICON_MONEY; // 🪙

        case 0x2694:  // ⚔
        case 0x1F47E: return UI_ICON_COMBAT; // 👾

        case 0x231A:  // ⌚
        case 0x23F0: return UI_ICON_CLOCK; // ⏰

        case 0x23F3:  // ⏳
        case 0x23F1: return UI_ICON_TIMER; // ⏱

        case 0x1F6B6: return UI_ICON_PERSON;  // 🚶
        case 0x1F465: return UI_ICON_PEOPLE; // 👥
        case 0x1F6AA: return UI_ICON_EXIT;   // 🚪

        case 0x1F4CD: // 📍
        case 0x1F5FA: return UI_ICON_PIN; // 🗺

        case 0x1F480: return UI_ICON_SKULL;  // 💀
        case 0x1F4A3: return UI_ICON_BOMB;   // 💣
        case 0x1F680: return UI_ICON_ROCKET; // 🚀

        case 0x1F4BE: // 💾
        case 0x1F4DD: return UI_ICON_SAVE; // 📝

        case 0x25A2: return UI_ICON_CIRCLE_MARK;        // ▢
        case 0x25A3: return UI_ICON_CHECKMARK;          // ▣

        case 0x25CF: return UI_ICON_BULLET_5;           // ●
        case 0x25CB: return UI_ICON_CIRCLE_MARK;        // ○

        case 0x25FC: // ◼ // ◻ (KDL3 health meter)
        case 0x25A0: // ■
        case 0x25AA: return UI_ICON_BAR_FILLED; // ▪
        case 0x25FB:
        case 0x25A1: // □
        case 0x25AB: return UI_ICON_BAR_EMPTY;  // ▫

        case 0x258C: return UI_ICON_PROGRESS; // ▌

        case 0x1F534: return 'R'; // 🔴
        case 0x1F7E0: return 'O'; // 🟠
        case 0x1F7E1: return 'Y'; // 🟡
        case 0x1F7E2: return 'G'; // 🟢
        case 0x1F535: return 'B'; // 🔵
        case 0x1F7E3: return 'P'; // 🟣
    }
    return 0;
}

static const char *ui3dsUnicodeToText(u32 cp)
{
    switch (cp) {
        case 0x1F4AF: return "Score"; // 💯
        case 0x2696:  return "Difficulty";
        case 0x1F162: return "(S)";   // 🅢
        case 0x1F15F: return "(P)";   // 🅟
    }
    return 0;
}

static bool ui3dsDecodeUtf8(u8 lead, const char *src, size_t *i, u32 *cp)
{
    int extra;
    if ((lead & 0xE0) == 0xC0)      { *cp = lead & 0x1F; extra = 1; }
    else if ((lead & 0xF0) == 0xE0) { *cp = lead & 0x0F; extra = 2; }
    else if ((lead & 0xF8) == 0xF0) { *cp = lead & 0x07; extra = 3; }
    else return false;

    size_t pos = *i;
    while (extra-- > 0) {
        u8 tail = (u8)src[pos];
        if ((tail & 0xC0) != 0x80)
            return false;
        *cp = (*cp << 6) | (tail & 0x3F);
        pos++;
    }

    *i = pos;
    return true;
}

void glyph3dsEncodeUtf8(char *dst, size_t dstSize, const char *src)
{
    if (dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t writePos = 0;
    for (size_t i = 0; src[i] && writePos + 1 < dstSize; ) {
        u8 c = (u8)src[i++];

        if (c < 0x80) {
            if (c == '\t') {
                dst[writePos++] = ' ';
                continue;
            }
            if (c == '\n') {
                dst[writePos++] = '\n';
                continue;
            }
            if (c == '\r') {
                if (src[i] == '\n')
                    i++;
                dst[writePos++] = '\n';
                continue;
            }
            if (c < 0x20 || c == 0x7f)
                continue;
            dst[writePos++] = (char)c;
            continue;
        }

        // Latin-1 fast paths: U+0080-U+00FF map to the identical CP1252 byte.
        // Only 0xa0-0xbf: 0x80-0x9f C1 controls and non-continuation bytes
        // fall through and are dropped.
        u8 next = (u8)src[i];
        if (c == 0xc2 && next >= 0xa0 && next <= 0xbf) {
            i++;
            // Some CP1252 high slots are repurposed as icon glyphs; remap the
            // real character to a lookalike, or drop it, before it reaches one.
            switch (next) {
                case 160: next = ' ';  break;   // NBSP
                case 166: next = '|';  break;   // ¦
                case 168: next = '"';  break;   // ¨
                case 175: next = '-';  break;   // ¯
                case 180: next = '\''; break;   // ´
                case 184: next = ',';  break;   // ¸
                case 164:                       // ¤
                case 172:                       // ¬
                case 173: continue;             // SHY
            }
            dst[writePos++] = (char)next;
            continue;
        }

        if (c == 0xc3 && (next & 0xc0) == 0x80) {
            i++;
            dst[writePos++] = (char)(next + 0x40);
            continue;
        }

        u32 cp;
        if (!ui3dsDecodeUtf8(c, src, &i, &cp))
            continue;

        // ZWJ / variation selectors: render nothing.
        if (cp == 0x200D || cp == 0xFE0E || cp == 0xFE0F)
            continue;

        u8 mapped = ui3dsUnicodeToCp1252(cp);
        if (!mapped)
            mapped = ui3dsUnicodeToIcon(cp);
        if (mapped) {
            dst[writePos++] = (char)mapped;
        } else {
            const char *word = ui3dsUnicodeToText(cp);
            while (word && *word && writePos + 1 < dstSize)
                dst[writePos++] = *word++;
        }
    }

    dst[writePos] = '\0';
}
