#include "3dsra_ui.h"

#include <vector>
#include <string>
#include <utility>
#include <cstdio>
#include <cstring>

#include "3dsra.h"
#include "3dssettings.h"
#include "3dsglyphs.h"
#include "3dsthemes.h"
#include "3dsui.h"
#include "3dsui_img.h"
#include "3dsimg_cache.h"

//---------------------------------------------------------
// Badge display reader
//
// Reads the per-game RAB1 badge cache (written by the downloader in 3dsra.cpp)
// and blits one decoded badge to the second screen. Buffers are allocated once
// (ra3dsUiInitialize) and reused across cache refreshes.
//---------------------------------------------------------

static ImageCacheReader badgeReader;

static const size_t badgePixelBufferSize = badgeMaxWidth * badgeMaxHeight * sizeof(u16);

void ra3dsCloseBadgeCache(void)
{
    imgCacheClose(&badgeReader);
}

void ra3dsOpenBadgeCache(void)
{
    if(!badgeReader.pixels || !badgeReader.index)
        return;

    ra3dsCloseBadgeCache();

    u32 id = ra3dsGetLoadedGameId();
    if(id == 0)
        return;

    char path[512];
    getBadgePath(id, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if(!f)
        return;

    ImageCacheHeader h;
    u32 count = imgCacheReadIndex(f, badgeReader.index, badgeReader.maxCount,
                                  badgeMaxWidth, badgeMaxHeight, &h);
    if(count == 0 || memcmp(h.magic, RA_BADGE_MAGIC, 4) != 0) {   // reject non-RAB1 files
        fclose(f);
        return;
    }

    badgeReader.file  = f;      // take ownership only on success
    badgeReader.count = count;
}

bool ra3dsLoadBadge(u32 key, bool unlocked)
{
    if(!badgeReader.file)
        return false;

    u32 cacheKey = key * 2 + (unlocked ? 0u : 1u);
    return imgCacheLoad(&badgeReader, cacheKey);
}

void ra3dsDrawBadge(int rightX, int bottomY)
{
    if(!badgeReader.currentValid)
        return;
    img3dsDrawSwizzledRgb565(badgeReader.pixels, badgeReader.currentWidth, badgeReader.currentHeight,
                             rightX - badgeReader.currentWidth, bottomY - badgeReader.currentHeight);
}

struct RaTag { char glyph; const char* label; };

static const int RA_FOOTER_HEIGHT = 72;
static const int RA_FOOTER_GAP = 6;
static const int RA_FOOTER_BOTTOM_PAD = 10;
static const int RA_H_PAD = 20;
static const int RA_RARITY_SEGMENTS = 32;
static const int RA_ACH_THUMB_SIZE = 64;
static const int RA_ROW_RIGHT_GAP = 8;

// The first four entries match RaAchievementType.
enum RaTagId {
    RA_TAG_STANDARD = 0,
    RA_TAG_MISSABLE,
    RA_TAG_PROGRESSION,
    RA_TAG_WIN,
    RA_TAG_UNLOCKED,
    RA_TAG_UNSUPPORTED,
    RA_TAG_ACHIEVEMENTS,
    RA_TAG_POINTS,
    RA_TAG_UNLOCK_RATE,
    RA_TAG_BEATEN_PROGRESS,
    RA_TAG_BEATEN,
    RA_TAG_MASTERED,
    RA_TAG_COUNT,
};

static const RaTag raTags[RA_TAG_COUNT] = {
    { UI_ICON_LOCK, "Locked" },                     // RA_TAG_STANDARD
    { UI_ICON_INFO, "Missable" },                   // RA_TAG_MISSABLE
    { UI_ICON_CHART, "Progression" },               // RA_TAG_PROGRESSION
    { UI_ICON_CROWN, "Win Condition" },             // RA_TAG_WIN
    { UI_ICON_CHECKMARK, "Unlocked" },              // RA_TAG_UNLOCKED
    { '?', "Unsupported" },                         // RA_TAG_UNSUPPORTED
    { UI_ICON_TROPHY, "Achievements" },             // RA_TAG_ACHIEVEMENTS
    { UI_ICON_STACK, "Points" },                    // RA_TAG_POINTS
    { UI_ICON_PEOPLE, "Unlock rate" },              // RA_TAG_UNLOCK_RATE
    { UI_ICON_FLAG_CHECKERED, "Beaten Progress" },  // RA_TAG_BEATEN_PROGRESS
    { UI_ICON_CLOCK, "Beat the game" },             // RA_TAG_BEATEN
    { UI_ICON_CLOCK, "Mastered" },                  // RA_TAG_MASTERED
};

static_assert((int)RA_TAG_STANDARD == (int)RA_ACH_TYPE_STANDARD &&
              (int)RA_TAG_MISSABLE == (int)RA_ACH_TYPE_MISSABLE &&
              (int)RA_TAG_PROGRESSION == (int)RA_ACH_TYPE_PROGRESSION &&
              (int)RA_TAG_WIN == (int)RA_ACH_TYPE_WIN,
              "first tags must stay aligned with RaAchievementType");

static RaTag getTag(const RaAchievementInfo& achievement) {
    if (achievement.unsupported) return raTags[RA_TAG_UNSUPPORTED];
    if (achievement.unlocked)    return raTags[RA_TAG_UNLOCKED];
    size_t type = (size_t)achievement.type;
    return raTags[type <= (size_t)RA_TAG_WIN ? type : (size_t)RA_TAG_STANDARD];
}

// '\n' separates meta lines; UI_TEXT_SECTION_SEPARATOR starts the lower text block.
static void buildGameSummaryFooter(const std::vector<RaAchievementInfo>& achievements,
                                   const RaGameSummary& summary, char* out, size_t outSize) {
    int progressUnlocked = 0, progressTotal = 0, winUnlocked = 0, winTotal = 0;
    for (const auto& achievement : achievements) {
        if (achievement.type == RA_ACH_TYPE_PROGRESSION) {
            progressTotal++;
            if (achievement.unlocked) progressUnlocked++;
        } else if (achievement.type == RA_ACH_TYPE_WIN) {
            winTotal++;
            if (achievement.unlocked) winUnlocked++;
        }
    }
    bool hasTypedAchievements = progressTotal > 0 || winTotal > 0;

    char line1[64];
    if (hasTypedAchievements)
        snprintf(line1, sizeof(line1), "%c %s:  %d/%d",
                 raTags[RA_TAG_BEATEN_PROGRESS].glyph, raTags[RA_TAG_BEATEN_PROGRESS].label,
                 progressUnlocked + winUnlocked, progressTotal + winTotal);
    else
        snprintf(line1, sizeof(line1), "%c Unlocked: %d/%d",
                 raTags[RA_TAG_ACHIEVEMENTS].glyph, summary.unlocked, summary.total);

    // Priority: completion dates -> progression/win counts -> raw points
    char line2[96];
    if (summary.beaten && summary.mastered)
        snprintf(line2, sizeof(line2), "%c %s: %s  \267  %c %s: %s",
                 raTags[RA_TAG_BEATEN].glyph, raTags[RA_TAG_BEATEN].label, summary.beatenDate,
                 raTags[RA_TAG_MASTERED].glyph, raTags[RA_TAG_MASTERED].label, summary.masteredDate);
    else if (summary.beaten)
        snprintf(line2, sizeof(line2), "%c %s: %s",
                 raTags[RA_TAG_BEATEN].glyph, raTags[RA_TAG_BEATEN].label, summary.beatenDate);
    else if (hasTypedAchievements && progressTotal > 0)
        snprintf(line2, sizeof(line2), "%c %s: %d/%d  \267  %c %s: %d/%d",
                 raTags[RA_TAG_PROGRESSION].glyph, raTags[RA_TAG_PROGRESSION].label, progressUnlocked, progressTotal,
                 raTags[RA_TAG_WIN].glyph, raTags[RA_TAG_WIN].label, winUnlocked, winTotal);
    else if (hasTypedAchievements)
        snprintf(line2, sizeof(line2), "%c %s: %d/%d",
                 raTags[RA_TAG_WIN].glyph, raTags[RA_TAG_WIN].label, winUnlocked, winTotal);
    else
        snprintf(line2, sizeof(line2), "%c %s: %d/%d",
                 raTags[RA_TAG_POINTS].glyph, raTags[RA_TAG_POINTS].label, summary.pointsUnlocked, summary.pointsTotal);

    char richPresence[192];
    ra3dsGetRichPresence(richPresence, sizeof(richPresence));

    if (richPresence[0])
        snprintf(out, outSize, "%s\n%s%cStatus: %s", line1, line2,
                 UI_TEXT_SECTION_SEPARATOR, richPresence);
    else
        snprintf(out, outSize, "%s\n%s", line1, line2);
}

static void buildAchievementFooter(const RaAchievementInfo& achievement, char* out, size_t outSize) {
    char rate[32];
    snprintf(rate, sizeof(rate), "%c %s: %.1f%%",
             raTags[RA_TAG_UNLOCK_RATE].glyph, raTags[RA_TAG_UNLOCK_RATE].label, achievement.rarity);

    RaTag tag = getTag(achievement);
    char line1[96];
    if (achievement.unlocked && achievement.unlockDate[0])
        snprintf(line1, sizeof(line1), "%c %s %s  \267  %s", tag.glyph, tag.label, achievement.unlockDate, rate);
    else if (tag.glyph)
        snprintf(line1, sizeof(line1), "%c %s  \267  %s", tag.glyph, tag.label, rate);

    snprintf(out, outSize, "%s%c%s", line1, UI_TEXT_SECTION_SEPARATOR, achievement.description);
}

static void ra3dsDrawAchievementFooter(int selectedIndex, bool isTextView, int footerTop, int footerHeight,
                                    int menuItemFrame, int menuBackColor,
                                    const std::vector<RaAchievementInfo>& achievements,
                                    const RaGameSummary& summary) {
    char footerText[512]; // meta lines + UI_TEXT_SECTION_SEPARATOR + body
    footerText[0] = '\0';
    if (selectedIndex <= 0)
        buildGameSummaryFooter(achievements, summary, footerText, sizeof(footerText));
    else
        buildAchievementFooter(achievements[selectedIndex - 1], footerText, sizeof(footerText));

    int theme = static_cast<int>(settings3DS.Theme);
    int highlightColor = Themes[theme].headerItemTextColor;
    int statsColor      = Themes[theme].normalItemDescriptionTextColor;
    int thumbColor = 0x808080;
    int descriptionColor   = settings3DS.Theme != Setting::Theme::DarkMode 
        ? Themes[theme].normalItemTextColor
        : Themes[theme].selectedItemTextColor;

    if (menuItemFrame != 0) {
        int fmag = menuItemFrame < 0 ? -menuItemFrame : menuItemFrame;
        float alpha = (float)(ANIMATE_TAB_STEPS - fmag + 1) / (ANIMATE_TAB_STEPS + 1);
        int backAlpha = ui3dsApplyAlphaToColor(menuBackColor, 1.0f - alpha);
        highlightColor = ui3dsApplyAlphaToColor(highlightColor, alpha) + backAlpha;
        statsColor      = ui3dsApplyAlphaToColor(statsColor, alpha) + backAlpha;
        thumbColor     = ui3dsApplyAlphaToColor(thumbColor, alpha) + backAlpha;
        descriptionColor   = ui3dsApplyAlphaToColor(descriptionColor, alpha) + backAlpha;
    }

    const int fontHeight = 13;
    int footerBottom = footerTop + footerHeight - RA_FOOTER_BOTTOM_PAD;
    // Match the normal thumbnail placement.
    bool raTheme = settings3DS.Theme == Setting::Theme::RetroArch;
    int thumbX1 = settings3DS.SecondScreenWidth - (raTheme ? 8 : 0);
    int thumbX0 = thumbX1 - RA_ACH_THUMB_SIZE;
    int thumbY1 = SCREEN_HEIGHT - (raTheme ? 18 : 20);
    int thumbY0 = thumbY1 - RA_ACH_THUMB_SIZE;
    int textRight = isTextView ? (settings3DS.SecondScreenWidth - RA_H_PAD) : (thumbX0 - RA_FOOTER_GAP);
    // Align with the menu rows.
    int textLeft = RA_H_PAD + ui3dsGetStringWidth(MENU_PREFIX_FILE);

    ui3dsDrawRect(RA_H_PAD, footerTop + 1, settings3DS.SecondScreenWidth - RA_H_PAD, footerTop + 2, highlightColor);

    if (!isTextView) {
        bool hasBadge = false;
        if (selectedIndex > 0) {
            const RaAchievementInfo& ach = achievements[selectedIndex - 1];
            hasBadge = ra3dsLoadBadge(ach.id, ach.unlocked);
        } else {
            hasBadge = ra3dsLoadBadge(RA_GAME_BADGE_KEY);
        }
        if (hasBadge)
            ra3dsDrawBadge(thumbX1, thumbY1);
        else {
            ui3dsDrawRect(thumbX0, thumbY0, thumbX1, thumbY1, thumbColor, .4);
            char iconText[2] = { (char)UI_ICON_FILE_ERROR, '\0' };
            int iconY0 = thumbY0 + (thumbY1 - thumbY0 - fontHeight) / 2;           
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, thumbX0, iconY0, thumbX1, iconY0 + fontHeight, statsColor, HALIGN_CENTER, iconText);

        }
            
    }

    // Meta lines stay on top; description/status wraps below.
    if (footerText[0]) {
        char* body = footerText;
        char* bottom = strchr(body, UI_TEXT_SECTION_SEPARATOR);
        if (bottom) *bottom++ = '\0';

        int y = footerTop + RA_FOOTER_GAP;
        for (char* line = body; line; ) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = '\0';
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, textLeft, y,
                textRight, y + fontHeight, statsColor, HALIGN_LEFT, line);
            y += fontHeight;
            line = nl ? nl + 1 : nullptr;
        }
        if (bottom) {
            int bottomMaxLines = selectedIndex == 0 ? 2 : 3;
            ui3dsDrawStringWithWrapping(settings3DS.SecondScreen, textLeft, y + RA_FOOTER_GAP,
                textRight, footerBottom, descriptionColor, HALIGN_LEFT, bottom, bottomMaxLines);
        }
    }
}

static SMenuItem buildAchievementRow(const RaAchievementInfo& achievement, int achievementIndex) {
    char right[64];
    right[0] = '\0';
    if (!achievement.unsupported) {
        int filled = (int)(achievement.rarity / 100.0f * RA_RARITY_SEGMENTS + 0.5f);
        if (filled < 0) filled = 0;
        if (filled > RA_RARITY_SEGMENTS) filled = RA_RARITY_SEGMENTS;
        for (int segment = 0; segment < RA_RARITY_SEGMENTS; segment++)
            right[segment] = segment < filled ? UI_ICON_BAR_FILLED : UI_ICON_BAR_EMPTY;
        right[RA_RARITY_SEGMENTS] = '\0';
    }

    char marker = getTag(achievement).glyph;
    char marker2 = '\0';

    if (!achievement.unsupported) {
        bool progressionOrWin = achievement.type == RA_ACH_TYPE_PROGRESSION ||
                                achievement.type == RA_ACH_TYPE_WIN;
        if (achievement.unlocked && progressionOrWin) {
            // Keep the type glyph as the primary marker for progression or win achievement
            // and append the unlocked glyph so both survive
            marker = raTags[achievement.type].glyph;
            marker2 = raTags[RA_TAG_UNLOCKED].glyph;
        } else if (!achievement.unlocked && achievement.type == RA_ACH_TYPE_STANDARD) {
            marker = '\0';   // plain locked standard achievement: no glyph
        }
    }

    char prefix[8];
    snprintf(prefix, sizeof(prefix), "%s", MENU_PREFIX_FILE);

    char suffix[40];
    if (achievement.unsupported)
        snprintf(suffix, sizeof(suffix), " (%d) (Unsupported)", achievement.points);
    else if (marker)
        snprintf(suffix, sizeof(suffix), " (%d) %c%c", achievement.points, marker, marker2);
    else
        snprintf(suffix, sizeof(suffix), " (%d)", achievement.points);

    int rightWidth = achievement.unsupported ? 0 : ui3dsGetStringWidth(right);
    int titleBudget = settings3DS.SecondScreenWidth - RA_H_PAD * 2 - rightWidth - RA_ROW_RIGHT_GAP
                      - ui3dsGetStringWidth(prefix) - ui3dsGetStringWidth(suffix);

    char truncatedTitle[160];
    ui3dsEllipsize(achievement.title, truncatedTitle, sizeof(truncatedTitle), titleBudget);

    char title[224];
    snprintf(title, sizeof(title), "%s%s%s", prefix, truncatedTitle, suffix);

    return SMenuItem(nullptr, MenuItemType::Action, std::string(title), std::string(right), achievementIndex);
}

// Builds the achievement sub-page rows + footer from live rc_client state.
static void buildAchievementsSubPage(SMenuTab& tab, u32 selectAchievementId,
                                     int parentSelectedIndex, int parentFirstItemIndex) {
    RaGameSummary summary = {};
    if (!ra3dsGetGameSummary(&summary) || summary.total <= 0)
        return;

    std::vector<RaAchievementInfo> achievements(summary.total);
    int count = ra3dsGetAchievements(achievements.data(), summary.total);
    if (count <= 0) return;

    tab.MenuItems.clear();
    std::string sortBadge = summary.unlocked > 0 ? std::string(1, UI_ICON_SORT) + " Unlocked first" : std::string();
    tab.MenuItems.emplace_back(nullptr, MenuItemType::Action, std::string("  Game Summary"), sortBadge, -1);

    int selectRow = 0;   // default: top row (Game Summary)
    for (int i = 0; i < count; i++) {
        if (selectAchievementId && achievements[i].id == selectAchievementId)
            selectRow = i + 1;   // +1 for the Game Summary row
        tab.MenuItems.emplace_back(buildAchievementRow(achievements[i], i));
    }

    tab.SelectedItemIndex = selectRow;
    tab.FirstItemIndex = 0;

    // Move the achievement list into the footer callback so it stays alive
    // after this function returns, without copying the whole vector.
    tab.subPage = { SUBPAGE_RETRO_ACHIEVEMENTS, RA_FOOTER_HEIGHT, false,
        [achievements = std::move(achievements), summary](int selectedIndex, bool isTextView, int footerTop, int footerHeight, int frame, int back) {
            ra3dsDrawAchievementFooter(selectedIndex, isTextView, footerTop, footerHeight, frame, back, achievements, summary);
        },
        parentSelectedIndex, parentFirstItemIndex };
}

// Opens the achievement detail page in this tab.
void ra3dsOpenAchievementsPage(SMenuTab& tab) {
    buildAchievementsSubPage(tab, 0, tab.SelectedItemIndex, tab.FirstItemIndex);
}

// Rebuilds an already-open achievement page.
// Called when an unlock happened while it was open.
void ra3dsRefreshAchievementsPage(SMenuTab& tab) {
    buildAchievementsSubPage(tab, ra3dsGetLastUnlockedId(), tab.subPage.parentSelectedIndex, tab.subPage.parentFirstItemIndex);
}

static void ra3dsAppendChecksPicker(std::vector<SMenuItem>& items) {
    std::vector<SMenuItem> options;
    options.emplace_back(nullptr, MenuItemType::Action, std::string("Performance"),
                         std::string("Default (recommended)"), 0);
    options.emplace_back(nullptr, MenuItemType::Action, std::string("Accuracy"),
                         std::string("No skipped checks"), 1);

    items.emplace_back(
        [](int val) { settings3DS.RAChecks = (Setting::RAChecks)val; settings3DS.isDirty = true; },
        MenuItemType::Picker, std::string("  Achievement Checks"), std::string(),
        (int)settings3DS.RAChecks, 1, 0,
        std::string("Accuracy is safer for timing-sensitive unlocks, but can slow games down, especially on O3DS."),
        options, DIALOG_TYPE_INFO);
}

bool ra3dsAppendMenuEntry(std::vector<SMenuItem>& items) {
    if (!ra3dsIsLoggedIn())
        return false;

    RaGameSummary summary = {};
    char stats[32];

    if (!ra3dsGetGameSummary(&summary) || summary.total == 0) {
        snprintf(stats, sizeof(stats), "%c %d  \267  %c %d",
             raTags[RA_TAG_ACHIEVEMENTS].glyph, 0,
             raTags[RA_TAG_POINTS].glyph, 0);

        items.emplace_back(nullptr, MenuItemType::Disabled, std::string("  RetroAchievements"), std::string(stats));
    } else {
        snprintf(stats, sizeof(stats), "%c %d/%d  \267  %c %d/%d",
                 raTags[RA_TAG_ACHIEVEMENTS].glyph, summary.unlocked, summary.total,
                 raTags[RA_TAG_POINTS].glyph, summary.pointsUnlocked, summary.pointsTotal);
        // Enter from the outer loop; changing this tab inside its item callback
        // would destroy the callback while it is still running.
        items.emplace_back(nullptr, MenuItemType::Action, std::string("  RetroAchievements"),
                           std::string(stats), MENU_ENTER_SUBPAGE - SUBPAGE_RETRO_ACHIEVEMENTS);
    }

    ra3dsAppendChecksPicker(items);
    return true;
}

void ra3dsUiInitialize(void)
{
    if(!badgeReader.pixels)
        imgCacheAlloc(&badgeReader, badgeMaxCount, badgePixelBufferSize);
}

void ra3dsUiFinalize(void)
{
    imgCacheFree(&badgeReader);
}
