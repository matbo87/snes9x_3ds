#include "3dsretroachievements_ui.h"

#include <vector>
#include <string>
#include <utility>
#include <cstdio>
#include <cstring>

#include "3dsretroachievements.h"
#include "3dssettings.h"
#include "3dsthemes.h"
#include "3dsui.h"

struct RaBadge { char glyph; const char* label; };

static const int RA_FOOTER_HEIGHT = 72;
static const int RA_FOOTER_GAP = 6;
static const int RA_FOOTER_BOTTOM_PAD = 10;
static const int RA_H_PAD = 20;
static const int RA_RARITY_SEGMENTS = 32;
static const int RA_ACH_THUMB_SIZE = 64;
static const int RA_SUMMARY_THUMB_WIDTH = 84;
static const int RA_ROW_RIGHT_GAP = 8;

// The first four entries match RaAchievementType.
enum RaBadgeId {
    RA_BADGE_STANDARD = 0,
    RA_BADGE_MISSABLE,
    RA_BADGE_PROGRESSION,
    RA_BADGE_WIN,
    RA_BADGE_UNLOCKED,
    RA_BADGE_UNSUPPORTED,
    RA_BADGE_ACHIEVEMENTS,
    RA_BADGE_POINTS,
    RA_BADGE_UNLOCK_RATE,
    RA_BADGE_BEATEN_PROGRESS,
    RA_BADGE_BEATEN,
    RA_BADGE_MASTERED,
    RA_BADGE_COUNT,
};

static const RaBadge raBadges[RA_BADGE_COUNT] = {
    { '\x1b', "Locked" },           // RA_BADGE_STANDARD
    { '\x1c', "Missable" },         // RA_BADGE_MISSABLE
    { '\x1d', "Progression" },      // RA_BADGE_PROGRESSION
    { '\x1e', "Win Condition" },    // RA_BADGE_WIN
    { '\xfd', "Unlocked" },         // RA_BADGE_UNLOCKED
    { '?', "Unsupported" },         // RA_BADGE_UNSUPPORTED
    { '\x02', "Achievements" },     // RA_BADGE_ACHIEVEMENTS
    { '\x03', "Points" },           // RA_BADGE_POINTS
    { '\x1f', "Unlock rate" },      // RA_BADGE_UNLOCK_RATE
    { '\x04', "Beaten Progress" },  // RA_BADGE_BEATEN_PROGRESS
    { '\x81', "Beat the game" },    // RA_BADGE_BEATEN
    { '\x81', "Mastered" },         // RA_BADGE_MASTERED
};

static_assert((int)RA_BADGE_STANDARD == (int)RA_ACH_TYPE_STANDARD &&
              (int)RA_BADGE_MISSABLE == (int)RA_ACH_TYPE_MISSABLE &&
              (int)RA_BADGE_PROGRESSION == (int)RA_ACH_TYPE_PROGRESSION &&
              (int)RA_BADGE_WIN == (int)RA_ACH_TYPE_WIN,
              "first badges must stay aligned with RaAchievementType");

static RaBadge ra3dsGetBadge(const RaAchievementInfo& achievement) {
    if (achievement.unsupported) return raBadges[RA_BADGE_UNSUPPORTED];
    if (achievement.unlocked)    return raBadges[RA_BADGE_UNLOCKED];
    size_t type = (size_t)achievement.type;
    return raBadges[type <= (size_t)RA_BADGE_WIN ? type : (size_t)RA_BADGE_STANDARD];
}

// '\n' separates meta lines; '\f' starts the lower text block.
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
                 raBadges[RA_BADGE_BEATEN_PROGRESS].glyph, raBadges[RA_BADGE_BEATEN_PROGRESS].label,
                 progressUnlocked + winUnlocked, progressTotal + winTotal);
    else
        snprintf(line1, sizeof(line1), "%c Unlocked: %d/%d",
                 raBadges[RA_BADGE_ACHIEVEMENTS].glyph, summary.unlocked, summary.total);

    // Priority: completion dates -> progression/win counts -> raw points
    char line2[96];
    if (summary.beaten && summary.mastered)
        snprintf(line2, sizeof(line2), "%c %s: %s  \xb7  %c %s: %s",
                 raBadges[RA_BADGE_BEATEN].glyph, raBadges[RA_BADGE_BEATEN].label, summary.beatenDate,
                 raBadges[RA_BADGE_MASTERED].glyph, raBadges[RA_BADGE_MASTERED].label, summary.masteredDate);
    else if (summary.beaten)
        snprintf(line2, sizeof(line2), "%c %s: %s",
                 raBadges[RA_BADGE_BEATEN].glyph, raBadges[RA_BADGE_BEATEN].label, summary.beatenDate);
    else if (hasTypedAchievements && progressTotal > 0)
        snprintf(line2, sizeof(line2), "%c %s: %d/%d  \xb7  %c %s: %d/%d",
                 raBadges[RA_BADGE_PROGRESSION].glyph, raBadges[RA_BADGE_PROGRESSION].label, progressUnlocked, progressTotal,
                 raBadges[RA_BADGE_WIN].glyph, raBadges[RA_BADGE_WIN].label, winUnlocked, winTotal);
    else if (hasTypedAchievements)
        snprintf(line2, sizeof(line2), "%c %s: %d/%d",
                 raBadges[RA_BADGE_WIN].glyph, raBadges[RA_BADGE_WIN].label, winUnlocked, winTotal);
    else
        snprintf(line2, sizeof(line2), "%c %s: %d/%d",
                 raBadges[RA_BADGE_POINTS].glyph, raBadges[RA_BADGE_POINTS].label, summary.pointsUnlocked, summary.pointsTotal);

    char richPresence[192];
    ra3dsGetRichPresence(richPresence, sizeof(richPresence));

    snprintf(out, outSize, "%s\n%s%s%s%s", line1, line2,
             richPresence[0] ? "\f" : "", richPresence[0] ? "Status: " : "", richPresence);
}

static void buildAchievementFooter(const RaAchievementInfo& achievement, char* out, size_t outSize) {
    char rate[32];
    snprintf(rate, sizeof(rate), "%c %s: %.1f%%",
             raBadges[RA_BADGE_UNLOCK_RATE].glyph, raBadges[RA_BADGE_UNLOCK_RATE].label, achievement.rarity);

    RaBadge badge = ra3dsGetBadge(achievement);
    char line1[96];
    if (achievement.unlocked && achievement.unlockDate[0])
        snprintf(line1, sizeof(line1), "%c %s %s  \xb7  %s", badge.glyph, badge.label, achievement.unlockDate, rate);
    else if (badge.glyph)
        snprintf(line1, sizeof(line1), "%c %s  \xb7  %s", badge.glyph, badge.label, rate);

    snprintf(out, outSize, "%s\f%s", line1, achievement.description);
}

static void ra3dsDrawAchievementFooter(int selectedIndex, bool isTextView, int footerTop, int footerHeight,
                                    int menuItemFrame, int menuBackColor,
                                    const std::vector<RaAchievementInfo>& achievements,
                                    const RaGameSummary& summary) {
    char footerText[512]; // meta lines + '\f' + body
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
    int thumbWidth  = selectedIndex == 0 ? RA_SUMMARY_THUMB_WIDTH : RA_ACH_THUMB_SIZE;
    int thumbHeight = RA_ACH_THUMB_SIZE;
    int footerBottom = footerTop + footerHeight - RA_FOOTER_BOTTOM_PAD;
    // Match the normal thumbnail placement.
    bool raTheme = settings3DS.Theme == Setting::Theme::RetroArch;
    int thumbX1 = settings3DS.SecondScreenWidth - (raTheme ? 8 : 0);
    int thumbX0 = thumbX1 - thumbWidth;
    int thumbY1 = SCREEN_HEIGHT - (raTheme ? 18 : 20);
    int thumbY0 = thumbY1 - thumbHeight;
    int textRight = isTextView ? (settings3DS.SecondScreenWidth - RA_H_PAD) : (thumbX0 - RA_FOOTER_GAP);
    // Align with the menu rows.
    int textLeft = RA_H_PAD + ui3dsGetStringWidth(MENU_PREFIX_FILE);

    ui3dsDrawRect(RA_H_PAD, footerTop + 1, settings3DS.SecondScreenWidth - RA_H_PAD, footerTop + 2, highlightColor);

    if (!isTextView)
        ui3dsDrawRect(thumbX0, thumbY0, thumbX1, thumbY1, thumbColor);

    // Meta lines stay on top; description/status wraps below.
    if (footerText[0]) {
        char* body = footerText;
        char* bottom = strchr(body, '\f');
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
            right[segment] = segment < filled ? '\x05' : '\x06';
        right[RA_RARITY_SEGMENTS] = '\0';
    }

    char marker = ra3dsGetBadge(achievement).glyph;
    if (!achievement.unlocked && !achievement.unsupported && achievement.type == RA_ACH_TYPE_STANDARD)
        marker = '\0';

    char prefix[8];
    snprintf(prefix, sizeof(prefix), "%s", MENU_PREFIX_FILE);

    char suffix[40];
    if (achievement.unsupported)
        snprintf(suffix, sizeof(suffix), " (%d)  (Unsupported)", achievement.points);
    else if (marker)
        snprintf(suffix, sizeof(suffix), " (%d)  %c", achievement.points, marker);
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

// Opens the achievement detail page in this tab.
void ra3dsOpenAchievementsPage(SMenuTab& tab) {
    RaGameSummary summary = {};
    if (!ra3dsGetGameSummary(&summary) || summary.total <= 0)
        return;

    std::vector<RaAchievementInfo> achievements(summary.total);
    int count = ra3dsGetAchievements(achievements.data(), summary.total);
    if (count <= 0) return;

    int parentSelectedIndex = tab.SelectedItemIndex;
    int parentFirstItemIndex = tab.FirstItemIndex;

    tab.MenuItems.clear();
    std::string sortBadge = summary.unlocked > 0 ? std::string("\xd0 Unlocked first") : std::string();
    tab.MenuItems.emplace_back(nullptr, MenuItemType::Action, std::string("  Game Summary"), sortBadge, -1);

    for (int i = 0; i < count; i++)
        tab.MenuItems.emplace_back(buildAchievementRow(achievements[i], i));

    tab.SelectedItemIndex = 0;
    tab.FirstItemIndex = 0;

    // Move the achievement list into the footer callback so it stays alive
    // after this function returns, without copying the whole vector.
    tab.subPage = { SUBPAGE_RETRO_ACHIEVEMENTS, RA_FOOTER_HEIGHT, false,
        [achievements = std::move(achievements), summary](int selectedIndex, bool isTextView, int footerTop, int footerHeight, int frame, int back) {
            ra3dsDrawAchievementFooter(selectedIndex, isTextView, footerTop, footerHeight, frame, back, achievements, summary);
        },
        parentSelectedIndex, parentFirstItemIndex };
}

bool ra3dsAppendMenuEntry(std::vector<SMenuItem>& items) {
    if (!ra3dsIsLoggedIn())
        return false;
    RaGameSummary summary = {};
    if (!ra3dsGetGameSummary(&summary) || summary.total == 0)
        return false;

    char stats[32];
    snprintf(stats, sizeof(stats), "%c %d/%d  \xb7  %c %d/%d",
             raBadges[RA_BADGE_ACHIEVEMENTS].glyph, summary.unlocked, summary.total,
             raBadges[RA_BADGE_POINTS].glyph, summary.pointsUnlocked, summary.pointsTotal);
    // Enter from the outer loop; changing this tab inside its item callback
    // would destroy the callback while it is still running.
    items.emplace_back(nullptr, MenuItemType::Action, std::string("  RetroAchievements"),
                       std::string(stats), MENU_ENTER_SUBPAGE - SUBPAGE_RETRO_ACHIEVEMENTS);
    return true;
}
