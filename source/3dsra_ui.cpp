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
#include "3dsutils.h"

//---------------------------------------------------------
// Badge display reader
//
// Reads the per-game RAB1 badge cache (written by the downloader in 3dsra.cpp)
// and blits one decoded badge to the second screen. Buffers are allocated once
// (ra3dsUiInitialize) and reused across cache refreshes.
//---------------------------------------------------------

static ImageCacheReader badgeReader;

static const size_t badgePixelBufferSize = badgeMaxWidth * badgeMaxHeight * sizeof(u16);

// Cache build date, read once per open so the menu never has to hit the FS.
static char badgeCacheDate[32] = "";

void ra3dsCloseBadgeCache(void)
{
    imgCacheClose(&badgeReader);
    badgeCacheDate[0] = '\0';
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

    // The file's mtime is its build date: stamped on rebuild, untouched by a skipped sync.
    u64 mtime = 0;
    if(R_SUCCEEDED(archive_getmtime(path, &mtime)) && mtime)
        utils3dsGetFormattedDate((time_t)mtime, badgeCacheDate, sizeof(badgeCacheDate), "Downloaded %Y-%m-%d");
}

const char* ra3dsGetBadgeCacheDate(void)
{
    return badgeCacheDate;
}

bool ra3dsLoadBadge(u32 key, bool unlocked)
{
    if(!badgeReader.file)
        return false;

    u32 cacheKey = key * 2 + (unlocked ? 0u : 1u);
    return imgCacheLoad(&badgeReader, cacheKey);
}

// Crop 64px cached badges to the 60px footer thumbnail.
static const int RA_BADGE_INSET = 2;

void ra3dsDrawBadge(int rightX, int bottomY)
{
    if(!badgeReader.currentValid)
        return;
    int drawWidth = badgeReader.currentWidth - 2 * RA_BADGE_INSET;
    int drawHeight = badgeReader.currentHeight - 2 * RA_BADGE_INSET;
    img3dsDrawSwizzledRgb565(badgeReader.pixels, badgeReader.currentWidth, badgeReader.currentHeight,
                             rightX - drawWidth, bottomY - drawHeight, RA_BADGE_INSET);
}

const u16* ra3dsGetBadgePixels(int* w, int* h)
{
    if(!badgeReader.currentValid)
        return NULL;
    if(w) *w = badgeReader.currentWidth;
    if(h) *h = badgeReader.currentHeight;
    return badgeReader.pixels;
}

static const int RA_FOOTER_HEIGHT = 60;
static const int RA_FOOTER_GAP = 8;
static const int RA_FOOTER_BODY_GAP = 5;
static const int RA_FOOTER_BOTTOM_PAD = 8;
static const int RA_H_PAD = 20;
static const int RA_RARITY_SEGMENTS = 32;
static const int RA_ACH_THUMB_SIZE = 60;
static const int RA_ROW_RIGHT_GAP = 8;

static const RaTag raTags[RA_TAG_COUNT] = {
    { UI_ICON_LOCK, "Locked" },                     // RA_TAG_STANDARD
    { UI_ICON_INFO, "Missable" },                   // RA_TAG_MISSABLE
    { UI_ICON_CHART, "Progression" },               // RA_TAG_PROGRESSION
    { UI_ICON_MEDAL, "Win Condition" },             // RA_TAG_WIN
    { UI_ICON_CHECKMARK, "Unlocked" },              // RA_TAG_UNLOCKED
    { '?', "Unsupported" },                         // RA_TAG_UNSUPPORTED
    { UI_ICON_TROPHY, "Achievements" },             // RA_TAG_ACHIEVEMENTS
    { UI_ICON_STACK, "Points" },                    // RA_TAG_POINTS
    { UI_ICON_PEOPLE, "Unlock rate" },              // RA_TAG_UNLOCK_RATE
    { UI_ICON_FLAG_CHECKERED, "Beaten Progress" },  // RA_TAG_BEATEN_PROGRESS
    { UI_ICON_CLOCK, "Beat the game" },             // RA_TAG_BEATEN
    { UI_ICON_CLOCK, "Mastered" },                  // RA_TAG_MASTERED
    { UI_ICON_SPEECH_BUBBLE, "Status" },            // RA_TAG_RICH_PRESENCE
};

static_assert((int)RA_TAG_STANDARD == (int)RA_ACH_TYPE_STANDARD &&
              (int)RA_TAG_MISSABLE == (int)RA_ACH_TYPE_MISSABLE &&
              (int)RA_TAG_PROGRESSION == (int)RA_ACH_TYPE_PROGRESSION &&
              (int)RA_TAG_WIN == (int)RA_ACH_TYPE_WIN,
              "first tags must stay aligned with RaAchievementType");

RaTag ra3dsTag(RaTagId id) { return raTags[id]; }

RaTag ra3dsTagByType(int achievementType) {
    if (achievementType <= RA_ACH_TYPE_STANDARD || achievementType > RA_ACH_TYPE_WIN)
        return { 0, NULL };
    return raTags[achievementType];   // aligned by the assert above
}

static RaTag getTag(const RaAchievementInfo& achievement) {
    if (achievement.unsupported) return raTags[RA_TAG_UNSUPPORTED];
    if (achievement.unlocked)    return raTags[RA_TAG_UNLOCKED];
    size_t type = (size_t)achievement.type;
    return raTags[type <= (size_t)RA_TAG_WIN ? type : (size_t)RA_TAG_STANDARD];
}

// Shared by the sub-page footer and detail dialog.
static std::vector<RaAchievementInfo> subPageAchievements;
static std::vector<int> subPageRowToAchievement;
static RaGameSummary subPageSummary;

// Returns -1 for the Back row, group headers, and invalid rows.
int ra3dsGetSubPageAchievementIndex(int selectedIndex) {
    if (selectedIndex < 0 || selectedIndex >= (int)subPageRowToAchievement.size())
        return -1;
    return subPageRowToAchievement[selectedIndex];
}

void ra3dsGetSubPageItemInfo(int achievementIndex, char *title, size_t titleSize,
                             char *body, size_t bodySize) {
    if (achievementIndex < 0 || achievementIndex >= (int)subPageAchievements.size()) {
        snprintf(title, titleSize, "%s", ra3dsGetGameTitle());
        ra3dsGetRichPresence(body, bodySize);
        if (!body[0])
            snprintf(body, bodySize, "No status reported for this game.");
        return;
    }

    const RaAchievementInfo& achievement = subPageAchievements[achievementIndex];
    snprintf(title, titleSize, "%s", achievement.title);
    snprintf(body, bodySize, "%s", achievement.description);
}

// UI_TEXT_SECTION_SEPARATOR starts the footer body.
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

    // Completed games show completion dates instead of progress.
    char line1[128];
    if (summary.beaten && summary.mastered)
        snprintf(line1, sizeof(line1), "%c %s: %s  \267  %c %s: %s",
                 raTags[RA_TAG_BEATEN].glyph, raTags[RA_TAG_BEATEN].label, summary.beatenDate,
                 raTags[RA_TAG_MASTERED].glyph, raTags[RA_TAG_MASTERED].label, summary.masteredDate);
    else if (summary.beaten)
        snprintf(line1, sizeof(line1), "%c %s: %s",
                 raTags[RA_TAG_BEATEN].glyph, raTags[RA_TAG_BEATEN].label, summary.beatenDate);
    else if (hasTypedAchievements) {
        char progressPart[32] = "", winPart[32] = "";
        if (progressTotal > 0)
            snprintf(progressPart, sizeof(progressPart), "  \267  %c %d/%d",
                     raTags[RA_TAG_PROGRESSION].glyph, progressUnlocked, progressTotal);
        if (winTotal > 0)
            snprintf(winPart, sizeof(winPart), "  \267  %c %d/%d",
                     raTags[RA_TAG_WIN].glyph, winUnlocked, winTotal);

        snprintf(line1, sizeof(line1), "%c %s: %d/%d%s%s",
                 raTags[RA_TAG_BEATEN_PROGRESS].glyph, raTags[RA_TAG_BEATEN_PROGRESS].label,
                 progressUnlocked + winUnlocked, progressTotal + winTotal, progressPart, winPart);
    }
    else
        snprintf(line1, sizeof(line1), "%c Unlocked: %d/%d  \267  %c %s: %d/%d",
                 raTags[RA_TAG_ACHIEVEMENTS].glyph, summary.unlocked, summary.total,
                 raTags[RA_TAG_POINTS].glyph, raTags[RA_TAG_POINTS].label,
                 summary.pointsUnlocked, summary.pointsTotal);

    char richPresence[192];
    ra3dsGetRichPresence(richPresence, sizeof(richPresence));

    if (richPresence[0])
        snprintf(out, outSize, "%s%c%c %s: %s", line1,
                 UI_TEXT_SECTION_SEPARATOR, raTags[RA_TAG_RICH_PRESENCE].glyph,
                 raTags[RA_TAG_RICH_PRESENCE].label, richPresence);
    else
        snprintf(out, outSize, "%s", line1);
}

static void buildAchievementFooter(const RaAchievementInfo& achievement, char* out, size_t outSize) {
    char rate[32];
    snprintf(rate, sizeof(rate), "%c %s: %.1f%%",
             raTags[RA_TAG_UNLOCK_RATE].glyph, raTags[RA_TAG_UNLOCK_RATE].label, achievement.rarity);

    RaTag tag = getTag(achievement);
    if (achievement.unlocked && !achievement.unsupported) {
        RaTag typeTag = ra3dsTagByType(achievement.type);
        if (typeTag.glyph)
            tag.glyph = typeTag.glyph;
    }

    char line1[96] = "";
    if (achievement.unlocked && achievement.unlockDate[0])
        snprintf(line1, sizeof(line1), "%c %s %s  \267  %s", tag.glyph, tag.label, achievement.unlockDate, rate);
    else if (tag.glyph)
        snprintf(line1, sizeof(line1), "%c %s  \267  %s", tag.glyph, tag.label, rate);

    snprintf(out, outSize, "%s%c%s", line1, UI_TEXT_SECTION_SEPARATOR, achievement.description);
}

static void ra3dsDrawAchievementFooter(int selectedIndex, int footerTop, int footerHeight,
                                    int menuItemFrame, int menuBackColor) {
    const std::vector<RaAchievementInfo>& achievements = subPageAchievements;
    int achievementIndex = ra3dsGetSubPageAchievementIndex(selectedIndex);

    char footerText[512]; // meta lines + UI_TEXT_SECTION_SEPARATOR + body
    footerText[0] = '\0';
    if (achievementIndex < 0)
        buildGameSummaryFooter(achievements, subPageSummary, footerText, sizeof(footerText));
    else
        buildAchievementFooter(achievements[achievementIndex], footerText, sizeof(footerText));

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
    int textRight = thumbX0 - RA_FOOTER_GAP;
    // Align with the menu rows.
    int textLeft = RA_H_PAD + ui3dsGetStringWidth(MENU_PREFIX_FILE);

    ui3dsDrawRect(RA_H_PAD, footerTop, settings3DS.SecondScreenWidth - RA_ACH_THUMB_SIZE - RA_FOOTER_GAP, footerTop + 1, highlightColor);

    bool hasBadge = achievementIndex >= 0
        ? ra3dsLoadBadge(achievements[achievementIndex].id, achievements[achievementIndex].unlocked)
        : ra3dsLoadBadge(RA_GAME_BADGE_KEY);
    if (hasBadge)
        ra3dsDrawBadge(thumbX1, thumbY1);
    else {
        ui3dsDrawRect(thumbX0, thumbY0, thumbX1, thumbY1, thumbColor, .4);
        char iconText[2] = { (char)UI_ICON_FILE_ERROR, '\0' };
        int iconY0 = thumbY0 + (thumbY1 - thumbY0 - fontHeight) / 2;
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, thumbX0, iconY0, thumbX1, iconY0 + fontHeight, statsColor, HALIGN_CENTER, iconText);
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
            // Fit the body within the remaining footer height.
            int bodyY = y + RA_FOOTER_BODY_GAP;
            int bodyLines = (footerBottom - bodyY) / FONT_LINE_HEIGHT;
            ui3dsDrawStringWithWrapping(settings3DS.SecondScreen,
                textLeft, bodyY,
                textRight, footerBottom, descriptionColor, HALIGN_LEFT,
                bottom, bodyLines);
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

    const char* progress = (!achievement.unlocked && !achievement.unsupported)
                         ? achievement.measuredProgress : "";

    char suffix[64];
    if (achievement.unsupported)
        snprintf(suffix, sizeof(suffix), " (%d) (Unsupported)", achievement.points);
    else if (marker)
        snprintf(suffix, sizeof(suffix), " (%d)%s%s %c%c", achievement.points,
                 progress[0] ? " " : "", progress, marker, marker2);
    else
        snprintf(suffix, sizeof(suffix), " (%d)%s%s", achievement.points,
                 progress[0] ? " " : "", progress);

    int rightWidth = achievement.unsupported ? 0 : ui3dsGetStringWidth(right);
    int titleBudget = settings3DS.SecondScreenWidth - RA_H_PAD * 2 - rightWidth - RA_ROW_RIGHT_GAP
                      - ui3dsGetStringWidth(prefix) - ui3dsGetStringWidth(suffix);

    char truncatedTitle[160];
    ui3dsEllipsize(achievement.title, truncatedTitle, sizeof(truncatedTitle), titleBudget);

    char title[256];
    snprintf(title, sizeof(title), "%s%s%s", prefix, truncatedTitle, suffix);

    return SMenuItem(nullptr, MenuItemType::Action, std::string(title), std::string(right), achievementIndex);
}

// Builds the achievement sub-page rows + footer from live rc_client state.
static void buildAchievementsSubPage(SMenuTab& tab, u32 selectAchievementId,
                                     int parentSelectedIndex, int parentFirstItemIndex) {
    RaGameSummary summary = {};
    if (!ra3dsGetGameSummary(&summary) || summary.total <= 0)
        return;

    // Retain prior rows if fetching fails; vector capacity grows as needed.
    if ((int)subPageAchievements.size() < summary.total)
        subPageAchievements.resize(summary.total);
    int count = ra3dsGetAchievements(subPageAchievements.data(), summary.total);
    if (count <= 0)
        return;
    subPageAchievements.resize(count);
    subPageSummary = summary;

    tab.MenuItems.clear();
    tab.SubTitle.assign(ra3dsGetGameTitle());

    char gameIdLabel[24];
    snprintf(gameIdLabel, sizeof(gameIdLabel), "Game ID: %u", (unsigned)ra3dsGetLoadedGameId());
    tab.SubTitleRight.assign(gameIdLabel);
    const char *backLabel = settings3DS.Theme == Setting::Theme::RetroArch ? "  Back" : "  \213 Back";
    tab.MenuItems.emplace_back(nullptr, MenuItemType::Action,
        backLabel, "", -1);

    tab.FirstItemIndex = 0;
    tab.SelectedItemIndex = tab.FirstItemIndex;

    subPageRowToAchievement.clear();
    subPageRowToAchievement.reserve(count + 16);
    subPageRowToAchievement.push_back(-1);

    int groupCount = 0;
    for (int i = 0; i < count; i++)
        if (subPageAchievements[i].groupLabel[0])
            groupCount++;

    for (int i = 0; i < count; i++) {
        if (groupCount > 1 && subPageAchievements[i].groupLabel[0]) {
            tab.MenuItems.emplace_back(nullptr, MenuItemType::Header2,
                                       std::string("  ") + subPageAchievements[i].groupLabel, "");
            subPageRowToAchievement.push_back(-1);
        }
        if (selectAchievementId && subPageAchievements[i].id == selectAchievementId)
            tab.SelectedItemIndex = tab.FirstItemIndex + (int)subPageRowToAchievement.size();
        subPageRowToAchievement.push_back(i);
        tab.MenuItems.emplace_back(buildAchievementRow(subPageAchievements[i], i));
    }

    tab.subPage = { SUBPAGE_RETRO_ACHIEVEMENTS, RA_FOOTER_HEIGHT,
        ra3dsDrawAchievementFooter,
        parentSelectedIndex, parentFirstItemIndex };
    tab.MakeSureSelectionIsOnScreen(menu3dsGetListVisibleItems(tab), 2);
}

void ra3dsOpenAchievementsPage(SMenuTab& tab) {
    // Do not restore an unlock selection when opening from the menu.
    ra3dsTakeLastUnlockedId();
    buildAchievementsSubPage(tab, 0, tab.SelectedItemIndex, tab.FirstItemIndex);
}

void ra3dsRefreshAchievementsPage(SMenuTab& tab) {
    u32 selectAchievementId = ra3dsTakeLastUnlockedId();
    if (!selectAchievementId) {
        int index = ra3dsGetSubPageAchievementIndex(tab.SelectedItemIndex);
        if (index >= 0 && index < (int)subPageAchievements.size())
            selectAchievementId = subPageAchievements[index].id;
    }
    buildAchievementsSubPage(tab, selectAchievementId, tab.subPage.parentSelectedIndex, tab.subPage.parentFirstItemIndex);
}

bool ra3dsAppendMenuEntry(std::vector<SMenuItem>& items) {
    if (!ra3dsIsLoggedIn() || ra3dsGetLoadedGameId() == 0)
        return false;

    RaGameSummary summary = {};
    if (!ra3dsGetGameSummary(&summary))
        return false;

    if (summary.total == 0) {
        items.emplace_back(nullptr, MenuItemType::Disabled, std::string("  RetroAchievements"),
                           std::string("No achievements yet"));
    } else {
        char stats[32];
        snprintf(stats, sizeof(stats), "%c %d/%d  \267  %c %d/%d  \233",
                 raTags[RA_TAG_ACHIEVEMENTS].glyph, summary.unlocked, summary.total,
                 raTags[RA_TAG_POINTS].glyph, summary.pointsUnlocked, summary.pointsTotal);

        // Enter from the outer loop; changing this tab inside its item callback
        // would destroy the callback while it is still running.
        items.emplace_back(nullptr, MenuItemType::Action, std::string("  RetroAchievements"),
                           std::string(stats), MENU_ENTER_SUBPAGE - SUBPAGE_RETRO_ACHIEVEMENTS);
    }

    // Retry state is client-wide, not game-specific.
    if (ra3dsHasUnsyncedUnlocks())
        items.emplace_back(nullptr, MenuItemType::Disabled, "  Sync still pending, retrying in the background", std::string());

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
