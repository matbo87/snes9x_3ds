#include "snes9x.h"
#include "memmap.h"

#include <cstring>

#include "3dsutils.h"
#include "3dssettings.h"
#include "3dsfiles.h"
#include "3dsui.h"
#include "3dsui_notif.h"
#include "3dsui_img.h"
#include "3dsimpl.h"
#include "3dsmenu.h"

#define ANIMATE_TAB_STEPS 3
#define ANIMATE_DIALOG_STEPS 8

static bool swapBuffer = true;
static bool gameScreenDirty = true;
static bool secondScreenDirty = true;
static bool isScrolling = false;

static int cheatsActive = 0;
static int cheatsTotal = 0;
static int lastSelectedTabIndex = 1; // defaults to File Menu tab

static u8 currentBatteryLevel = 0;
static u8 currentChargeState = 0;

static u32 lastKeysHeld = 0xffffff;
static u32 thisKeysHeld = 0;
static int dialogBackColor = 0x000000;

MenuButton bottomMenuButtons[] = {
    {"Select", "\x0cc", 0x800d1d},
    {"Back", "\x0cd", 0x999409},
    {"Options", "\x0ce", 0x0d5280},
    {"Page \x0d1", "\x0cf", 0x0d8014}
};


bool menu3dsUpdateThumb(SMenuTab *currentTab)
{
    const std::string& text = currentTab->MenuItems[currentTab->SelectedItemIndex].Text;
    if (text.rfind(MENU_PREFIX_FILE, 0) == 0) {
        const char* romName = text.c_str() + strlen(MENU_PREFIX_FILE);

        if (img3dsLoadThumb(romName)) {
            img3dsDrawThumb();

            return true;
        }
    }

    return false;
}

void menu3dsDrawSplash(float fade = 1.0f)
{
    if (settings3DS.isRomLoaded)
        return;

    bool isTopStereo = gpu3dsIs3DEnabled();
    float iod = isTopStereo ? gpu3dsGetIOD() : 0;

    GSPGPU_FramebufferFormat gpuBufFmt = (GSPGPU_FramebufferFormat)DISPLAY_TRANSFER_FMT;
    if (gfxGetScreenFormat(settings3DS.GameScreen) != gpuBufFmt)
        gfxSetScreenFormat(settings3DS.GameScreen, gpuBufFmt);

    gpu3dsFrameBegin();
        gpu3dsClearScreen(settings3DS.GameScreen, isTopStereo);
        img3dsDrawSplash(UI_ATLAS, isTopStereo, iod, fade);
    gpu3dsFrameEnd();
}

void menu3dsSetCheatsCount(SMenuItem& item, int active, int total) {
    cheatsActive = active;
    cheatsTotal = total;
    
    if (total) {
        item.Text = "ENABLED CHEAT CODES: " +  std::to_string(cheatsActive) + "/" + std::to_string(cheatsTotal);
    }
}

int menu3dsGetLastSelectedTabIndex() {
    return lastSelectedTabIndex;
}

void menu3dsSetLastSelectedTabIndex(int index) {
    lastSelectedTabIndex = index;
}

void menu3dsSwapBuffersAndWaitForVBlank()
{
	if (swapBuffer) {
    	impl3dsFlushScreen(settings3DS.SecondScreen, false, false);
		gfxScreenSwapBuffers(settings3DS.SecondScreen, false);

        swapBuffer = false;
	}

    gpu3dsWaitForVBlank(settings3DS.SecondScreen);
}

bool menu3dsGaugeIsDisabled(SMenuTab *currentTab, int index)
{
    SMenuItem& item = currentTab->MenuItems[index];
    return item.GaugeMaxValue == GAUGE_DISABLED_VALUE;
}

bool menu3dsHasHighlightableItems(SMenuTab *currentTab) {
    bool hasSelectableItems = false;

    for (size_t i = 0; i < currentTab->MenuItems.size(); i++) {
       if (currentTab->MenuItems[i].IsHighlightable()) {
            hasSelectableItems = true;
            break;
        } 
    }

    return hasSelectableItems;
}

// enable/disable gauge
// find related item via id
// gauge item currently needs to follow related menu item (ideally it would have something like relatedId attribute)
void menu3dsUpdateGaugeVisibility(SMenuTab *currentTab, int id, int value)
{
    size_t gi = 0;
    for (size_t i = 0; i < currentTab->MenuItems.size(); i++)
    {
        // assumption: gauge item follows related menu item
        // (e.g. SecondScreenBgOpacity gauge follows SecondScreenBg picker)
        if (currentTab->MenuItems[i].GaugeMaxValue == id) {
            gi = i + 1;
            break;
        }
    }
    
    if (gi && currentTab->MenuItems[gi].Type == MenuItemType::Gauge)
        currentTab->MenuItems[gi].GaugeMaxValue = value;
}

void menu3dsDrawItems(
    SMenuTab *currentTab, int horizontalPadding, int menuStartY, int maxItems,
    int selectedItemBackColor,
    int selectedItemTextColor, 
    int selectedItemDescriptionTextColor,
    int normalItemTextColor,
    int normalItemDescriptionTextColor,
    int disabledItemTextColor, 
    int headerItemTextColor, 
    int subtitleTextColor,
    int offsetX = 0)
{
    int fontHeight = 13;
    
    // Display the subtitle
    if (!currentTab->SubTitle.empty())
    {
        maxItems--;
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, menuStartY, settings3DS.SecondScreenWidth - horizontalPadding, menuStartY + fontHeight, 
            subtitleTextColor, HALIGN_LEFT, currentTab->SubTitle.c_str());
        ui3dsDrawRect(horizontalPadding, menuStartY + fontHeight - 1, settings3DS.SecondScreenWidth - horizontalPadding, menuStartY + fontHeight, subtitleTextColor);
        menuStartY += fontHeight;
    }

    int line = 0;
    int color = Themes[static_cast<int>(settings3DS.Theme)].selectedTabTextColor;

    // Draw all the individual items
    //
    for (int i = currentTab->FirstItemIndex;
         i < static_cast<int>(currentTab->MenuItems.size()) && i < (currentTab->FirstItemIndex + maxItems); i++)
    {
        int y = line * fontHeight + menuStartY;

        // Draw the selected background 
        //
        if (currentTab->SelectedItemIndex == i)
        {
            if (selectedItemBackColor != -1) {
                ui3dsDrawRect(0, y, settings3DS.SecondScreenWidth, y + 14, selectedItemBackColor);                
            }

            if (settings3DS.Theme == Setting::Theme::RetroArch && currentTab->MenuItems[i].IsHighlightable()) {
                int xi = horizontalPadding - offsetX;
                ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, xi, y, xi + 10, y + 14, selectedItemTextColor, HALIGN_LEFT, ">");
            }
        }
        
        if (currentTab->MenuItems[i].Type == MenuItemType::Header1)
        {
            color = headerItemTextColor;
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            ui3dsDrawRect(horizontalPadding, y + fontHeight - 1, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color);
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Header2)
        {
            color = headerItemTextColor;
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Textarea)
        {
            color = normalItemDescriptionTextColor;
            int maxLines = 15; // TODO: set value based on content
            ui3dsDrawStringWithWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight * maxLines, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Disabled)
        {
            color = disabledItemTextColor;
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Action)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;
            
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

            color = normalItemDescriptionTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemDescriptionTextColor;
            if (!currentTab->MenuItems[i].Description.empty())
            {
                ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Description.c_str());
            }
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Checkbox)
        {
            color = currentTab->MenuItems[i].Value == 0 ? disabledItemTextColor : normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;
               
            
            int checkboxOffsetX = settings3DS.SecondScreenWidth - horizontalPadding - 20;
            int descriptionOffsetX = checkboxOffsetX;
            if (!currentTab->MenuItems[i].Description.empty()) {
                descriptionOffsetX = checkboxOffsetX - currentTab->MenuItems[i].Description.size() * 8;
                ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, descriptionOffsetX, y, checkboxOffsetX, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Description.c_str());
            }

            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, descriptionOffsetX, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, checkboxOffsetX, y, checkboxOffsetX + 20, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Value == 1 ? "\xfd" : "\xfe");    
        }
        
        else if (currentTab->MenuItems[i].Type == MenuItemType::Radio)
        {
            radio_state val = static_cast<radio_state>(currentTab->MenuItems[i].Value);

            color = (val == RADIO_INACTIVE || val == RADIO_INACTIVE_CHECKED) ? disabledItemTextColor : normalItemTextColor;
            bool isSelected = val == RADIO_ACTIVE_CHECKED || val == RADIO_INACTIVE_CHECKED;
            if (currentTab->SelectedItemIndex == i) {
                color = selectedItemTextColor;
            }
            
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, settings3DS.SecondScreenWidth - 100 + 60, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, isSelected ? "\xfd" : "\xfe");
        }

        else if (currentTab->MenuItems[i].Type == MenuItemType::Gauge)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;
            
            if (menu3dsGaugeIsDisabled(currentTab, i)) {
                color = disabledItemTextColor;
            }

            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

            const int max = 40;
            int diff = currentTab->MenuItems[i].GaugeMaxValue - currentTab->MenuItems[i].GaugeMinValue;
            int pos = (currentTab->MenuItems[i].Value - currentTab->MenuItems[i].GaugeMinValue) * (max - 1) / diff;

            char gauge[max+1];
            for (int j = 0; j < max; j++)
            if (j == pos) {
                gauge[j] = settings3DS.Theme == Setting::Theme::Original ? '\xfa' :  '\xfc';
            } else {
                gauge[j] = '\xfb';
            }

            gauge[max] = 0;
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 245, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, gauge);            
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Picker)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;

            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, 160, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

            if (!currentTab->MenuItems[i].PickerItems.empty() && currentTab->MenuItems[i].GaugeMinValue)
            {
                int selectedIndex = -1;
                for (size_t j = 0; j < currentTab->MenuItems[i].PickerItems.size(); j++)
                {
                    std::vector<SMenuItem>& pickerItems = currentTab->MenuItems[i].PickerItems;
                    if (pickerItems[j].Value == currentTab->MenuItems[i].Value)
                    {
                        selectedIndex = j;
                    }
                }
                if (selectedIndex > -1)
                {
                    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 160, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].PickerItems[selectedIndex].Text.c_str());
                }
            }
        }

        line ++;
    }


    // Draw the "up arrow" to indicate more options available at top
    //
    if (settings3DS.Theme == Setting::Theme::Original && currentTab->FirstItemIndex != 0)
    {
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, settings3DS.SecondScreenWidth - horizontalPadding, menuStartY, settings3DS.SecondScreenWidth, menuStartY + fontHeight, disabledItemTextColor, HALIGN_CENTER, "\xf8");
    }

    // Draw the "down arrow" to indicate more options available at bottom
    //
    if (settings3DS.Theme == Setting::Theme::Original && currentTab->FirstItemIndex + maxItems < static_cast<int>(currentTab->MenuItems.size()))
    {
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, settings3DS.SecondScreenWidth - horizontalPadding, menuStartY + (maxItems - 1) * fontHeight, settings3DS.SecondScreenWidth, menuStartY + maxItems * fontHeight, disabledItemTextColor, HALIGN_CENTER, "\xf9");
    }
    
}

// Display the list of choices for selection
//
void menu3dsDrawMenu(std::vector<SMenuTab>& menuTabs, int& currentMenuTab, int menuItemFrame, int translateY)
{
    SMenuTab *currentTab = &menuTabs[currentMenuTab];

    // Draw the background
    if (settings3DS.Theme != Setting::Theme::RetroArch) {
        ui3dsDrawRect(0, 0, settings3DS.SecondScreenWidth, 24, Themes[static_cast<int>(settings3DS.Theme)].menuTopBarColor);
        ui3dsDrawRect(0, 24, settings3DS.SecondScreenWidth, 220, Themes[static_cast<int>(settings3DS.Theme)].menuBackColor);
        ui3dsDrawRect(0, 220, settings3DS.SecondScreenWidth, SCREEN_HEIGHT, Themes[static_cast<int>(settings3DS.Theme)].menuBottomBarColor);
    } else {
        // draw checkerboard background for retroarch theme
        int cb1 = Themes[static_cast<int>(settings3DS.Theme)].menuBackColor;
        int cb2 = ui3dsOverlayBlendColor(cb1, 0xededed); 
        ui3dsDrawCheckerboard(0, 0, settings3DS.SecondScreenWidth, SCREEN_HEIGHT, cb1, cb2);

        // draw frame
        int cwidth = 4;
        int cx0 = 8;
        int cy0 = 20;
        int cx1 = settings3DS.SecondScreenWidth - cx0;
        int cy1 = 222;

        int cf1 = ui3dsOverlayBlendColor(cb1, Themes[static_cast<int>(settings3DS.Theme)].accentColor);
        int cf2 = ui3dsOverlayBlendColor(cb2, Themes[static_cast<int>(settings3DS.Theme)].accentColor);

        // horizontal
        ui3dsDrawCheckerboard(cx0, cy0, cx1, cy0 + cwidth, cf1, cf2);
        ui3dsDrawCheckerboard(cx0, cy1 - cwidth, cx1, cy1, cf1, cf2);
        
        // vertical
        ui3dsDrawCheckerboard(cx0, cy0 + cwidth, cx0 + cwidth, cy1 - cwidth, cf1, cf2);
        ui3dsDrawCheckerboard(cx1 - cwidth, cy0 + cwidth, cx1, cy1 - cwidth, cf1, cf2);
    }

    // Draw the tabs at the top
    //
    for (int i = 0; i < static_cast<int>(menuTabs.size()); i++)
    {
        int color = i == currentMenuTab ?  Themes[static_cast<int>(settings3DS.Theme)].selectedTabTextColor :  Themes[static_cast<int>(settings3DS.Theme)].tabTextColor;
        int accentColor = i == currentMenuTab ? Themes[static_cast<int>(settings3DS.Theme)].accentColor : Themes[static_cast<int>(settings3DS.Theme)].accentUnselectedColor;

        int offsetLeft = 10;
        int offsetRight = 10;

        int availableSpace = settings3DS.SecondScreenWidth - ( offsetLeft + offsetRight );
        int pixelPerOption =      availableSpace / static_cast<int>(menuTabs.size());
        int extraPixelOnOptions = availableSpace % static_cast<int>(menuTabs.size());

        // each tab gains an equal amount of horizontal space
        // if space is not cleanly divisible by tab count, the earlier tabs gain one extra pixel each until we reach the requested space
        int xLeft =  (     i     * pixelPerOption ) + offsetLeft + std::min( i,     extraPixelOnOptions );
        int xRight = ( ( i + 1 ) * pixelPerOption ) + offsetLeft + std::min( i + 1, extraPixelOnOptions );
        int yTextTop = 6;
        int yCurrentTabBoxTop = 21;
        int yCurrentTabBoxBottom = 24;

        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, xLeft, yTextTop, xRight, yCurrentTabBoxTop, color, HALIGN_CENTER, menuTabs[i].Title.c_str());

        if (i == currentMenuTab &&
            Themes[static_cast<int>(settings3DS.Theme)].selectedTabIndicatorColor != static_cast<uint32>(-1)) {
            ui3dsDrawRect(xLeft, yCurrentTabBoxTop, xRight, yCurrentTabBoxBottom, Themes[static_cast<int>(settings3DS.Theme)].selectedTabIndicatorColor);
        }

        // draw indicator when game has (active) cheats
        if (menuTabs[i].Title == "Cheats" && cheatsTotal > 0) {
            int offsetX = settings3DS.SecondScreen == GFX_TOP ? 19 : 14;
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, xRight - offsetX, yTextTop - 3, xRight, yCurrentTabBoxTop, cheatsActive > 0 ? accentColor : color, HALIGN_LEFT, "\x95");        
        }
    }

    //battery display
    const int maxBatteryLevel = 5;
    const int battLevelWidth = 3;
    const int battFullLevelWidth = (maxBatteryLevel) * battLevelWidth + 1;
    const int battBorderWidth = 1;
    const int battY1 = 227;
    const int battY2 = 234;
    const int battX2 = settings3DS.SecondScreenWidth - 10;
    const int battHeadWidth = 2;
    const int battHeadSpacing = 1;

    // battery positive end
    ui3dsDrawRect(
        battX2 - battFullLevelWidth - battBorderWidth - battHeadWidth, 
        battY1 + battHeadSpacing, 
        battX2 - battFullLevelWidth - battBorderWidth, 
        battY2 - battHeadSpacing, 
        Themes[static_cast<int>(settings3DS.Theme)].selectedTabTextColor, 1.0f);
    // battery body
    ui3dsDrawRect(
        battX2 - battFullLevelWidth - battBorderWidth, 
        battY1 - battBorderWidth, 
        battX2 + battBorderWidth, 
        battY2 + battBorderWidth, 
        Themes[static_cast<int>(settings3DS.Theme)].selectedTabTextColor, 1.0f);
    // battery's empty insides
    ui3dsDrawRect(
        battX2 - battFullLevelWidth, 
        battY1, 
        battX2, 
        battY2, 
        Themes[static_cast<int>(settings3DS.Theme)].menuBottomBarColor, 1.0f);
        
    if(currentChargeState) {
        ui3dsDrawRect(
            battX2-battFullLevelWidth + 1, battY1 + 1, 
            battX2 - 1, battY2 - 1, Themes[static_cast<int>(settings3DS.Theme)].accentColor, 1.0f);
    } else {
        u8 batteryLevel = currentBatteryLevel;
        if (batteryLevel > 5) batteryLevel = 5;
        for (int i = 0; i < batteryLevel; i++)
        {
            ui3dsDrawRect(
                battX2-battLevelWidth*(i+1), battY1 + 1, 
                battX2-battLevelWidth*(i) - 1, battY2 - 1, Themes[static_cast<int>(settings3DS.Theme)].accentColor, 1.0f);
        }
    }
    
    int buttonRightMargin = 5;
    int buttonLeftMargin = 10;
    int bottomMenuPosX = 10;
    int buttonColor = settings3DS.Theme == Setting::Theme::Original ? 0x529eeb : 0x555555;

    for (const auto& button : bottomMenuButtons) {
        if (settings3DS.Theme == Setting::Theme::DarkMode) {
            // multi color buttons for dark mode theme
            buttonColor = button.color;
        }
        
        if ((strcmp(button.label, "Options") != 0 && strcmp(button.label, "Page \x0d1") != 0) || currentTab->Title == "Load Game") {
            ui3dsDrawRect(bottomMenuPosX + 2, SCREEN_HEIGHT - 13, bottomMenuPosX + 9, SCREEN_HEIGHT - 5,0xffffff);
            bottomMenuPosX = ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, bottomMenuPosX, SCREEN_HEIGHT - 16, bottomMenuPosX + 12, SCREEN_HEIGHT, buttonColor, HALIGN_LEFT,  button.icon) + buttonRightMargin;
            bottomMenuPosX = ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, bottomMenuPosX, SCREEN_HEIGHT - 17, bottomMenuPosX + 100, SCREEN_HEIGHT, Themes[static_cast<int>(settings3DS.Theme)].menuBottomBarTextColor, HALIGN_LEFT, button.label) + buttonLeftMargin;
        }
    }

    const int rightEdge = battX2 - battFullLevelWidth - battBorderWidth - 6;
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 97, SCREEN_HEIGHT - 17, rightEdge, SCREEN_HEIGHT, Themes[static_cast<int>(settings3DS.Theme)].menuBottomBarTextColor, HALIGN_RIGHT, settings3dsGetAppVersion("v"));
    
    int maxItems = MENU_HEIGHT;
    int menuStartY = 29;

    int menuBackColor = Themes[static_cast<int>(settings3DS.Theme)].menuBackColor;
    int selectedItemBackColor = menu3dsHasHighlightableItems(currentTab) ? Themes[static_cast<int>(settings3DS.Theme)].selectedItemBackColor : -1;
    
    ui3dsSetTranslate(menuItemFrame * 3, translateY);

    if (menuItemFrame == 0)
    {
        menu3dsDrawItems(
            currentTab, 20, menuStartY, maxItems,
            selectedItemBackColor,
            Themes[static_cast<int>(settings3DS.Theme)].selectedItemTextColor,
            Themes[static_cast<int>(settings3DS.Theme)].selectedItemDescriptionTextColor,
            Themes[static_cast<int>(settings3DS.Theme)].normalItemTextColor,
            Themes[static_cast<int>(settings3DS.Theme)].normalItemDescriptionTextColor,
            Themes[static_cast<int>(settings3DS.Theme)].disabledItemTextColor,
            Themes[static_cast<int>(settings3DS.Theme)].headerItemTextColor,
            Themes[static_cast<int>(settings3DS.Theme)].subtitleTextColor);

        if (currentTab->Title == "Load Game" && file3dsIsCurrentDirLoadedFromCache()) {
            const char* cacheBadgeText = "\xfd Cached";
            const int cacheBadgePaddingX = 0;
            const int cacheBadgeY = menuStartY - 1;
            const int cacheBadgeRight = settings3DS.SecondScreenWidth - 20;
            const int cacheBadgeLeft = cacheBadgeRight - ui3dsGetStringWidth(cacheBadgeText) - cacheBadgePaddingX;

            // Clear a dedicated area so long path subtitles don't overlap the cache badge.
            ui3dsDrawRect(cacheBadgeLeft - 4, cacheBadgeY, cacheBadgeRight, cacheBadgeY + 13, menuBackColor);
            ui3dsDrawStringWithNoWrapping(
                settings3DS.SecondScreen,
                cacheBadgeLeft, cacheBadgeY,
                cacheBadgeRight, cacheBadgeY + 13,
                Themes[static_cast<int>(settings3DS.Theme)].normalItemDescriptionTextColor,
                HALIGN_RIGHT,
                cacheBadgeText);
        }

    }
    else
    {
        if (menuItemFrame < 0)
            menuItemFrame = -menuItemFrame;
        float alpha = (float)(ANIMATE_TAB_STEPS - menuItemFrame + 1) / (ANIMATE_TAB_STEPS + 1);

        int menuBackColorAlpha = ui3dsApplyAlphaToColor(menuBackColor, 1.0f - alpha);
        
         menu3dsDrawItems(
            currentTab, 20, menuStartY, maxItems,
            selectedItemBackColor != -1 ? ui3dsApplyAlphaToColor(selectedItemBackColor, alpha) + menuBackColorAlpha : selectedItemBackColor,
            ui3dsApplyAlphaToColor(Themes[static_cast<int>(settings3DS.Theme)].selectedItemTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[static_cast<int>(settings3DS.Theme)].selectedItemDescriptionTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[static_cast<int>(settings3DS.Theme)].normalItemTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[static_cast<int>(settings3DS.Theme)].normalItemDescriptionTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[static_cast<int>(settings3DS.Theme)].disabledItemTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[static_cast<int>(settings3DS.Theme)].headerItemTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[static_cast<int>(settings3DS.Theme)].subtitleTextColor, alpha) + menuBackColorAlpha);
    } 
}

void menu3dsDrawDialog(SMenuTab& dialogTab)
{
    int dialogTextColor = 0xffffff;
    int selectedItemBackColor = 0x000000;
    int offsetX = settings3DS.Theme == Setting::Theme::RetroArch ? 6 : 0;
    int horizontalPadding = 32;
    int topHeight = 76;
    int bottomHeight = 84;
    
    int dialogBackColorBottom = settings3DS.Theme == Setting::Theme::Original ? dialogBackColor : Themes[static_cast<int>(settings3DS.Theme)].menuBackColor;
    int dialogBackColorTop = settings3DS.Theme == Setting::Theme::Original ? ui3dsApplyAlphaToColor(dialogBackColorBottom, 0.9f) : ui3dsOverlayBlendColor(dialogBackColorBottom, 0xaaaaaa);
    ui3dsDrawRect(0, 0, settings3DS.SecondScreenWidth, topHeight, dialogBackColorTop);
    ui3dsDrawRect(0, topHeight, settings3DS.SecondScreenWidth, topHeight + bottomHeight, dialogBackColorBottom);

    int dialogTitleTextColor = 
        ui3dsApplyAlphaToColor(dialogBackColorTop, 1.0f - Themes[static_cast<int>(settings3DS.Theme)].dialogTextAlpha) + 
        ui3dsApplyAlphaToColor(dialogTextColor, Themes[static_cast<int>(settings3DS.Theme)].dialogTextAlpha);
    
    int dialogItemDescriptionTextColor = 
        ui3dsApplyAlphaToColor(dialogBackColorBottom, 1.0f - Themes[static_cast<int>(settings3DS.Theme)].dialogTextAlpha) + 
        ui3dsApplyAlphaToColor(dialogTextColor, Themes[static_cast<int>(settings3DS.Theme)].dialogTextAlpha);

    int dialogSelectedItemBackColor;

    if (settings3DS.Theme == Setting::Theme::DarkMode) {    
        ui3dsDrawRect(0, topHeight - 2, settings3DS.SecondScreenWidth, topHeight, dialogBackColor);
        ui3dsDrawRect(0, topHeight, settings3DS.SecondScreenWidth, topHeight + 2, dialogBackColor);
        dialogSelectedItemBackColor = Themes[static_cast<int>(settings3DS.Theme)].selectedItemBackColor;
    }
    else if (settings3DS.Theme == Setting::Theme::RetroArch) {   
        int cb1 = ui3dsOverlayBlendColor(dialogBackColorTop, dialogBackColor);
        int cb3 = ui3dsOverlayBlendColor(ui3dsApplyAlphaToColor(dialogBackColorBottom, 0.85f), dialogBackColor);
        ui3dsDrawCheckerboard(0, topHeight - 2, settings3DS.SecondScreenWidth, topHeight, cb1, cb3);
        ui3dsDrawCheckerboard(0, topHeight, settings3DS.SecondScreenWidth, topHeight + 2, cb1, cb3);
        dialogSelectedItemBackColor = -1;
    } else {
        dialogSelectedItemBackColor = Themes[static_cast<int>(settings3DS.Theme)].selectedItemBackColor == static_cast<uint32>(-1) ? -1 :
        ui3dsApplyAlphaToColor(dialogBackColorBottom, 1.0f - Themes[static_cast<int>(settings3DS.Theme)].dialogSelectedItemBackAlpha) + 
        ui3dsApplyAlphaToColor(selectedItemBackColor, Themes[static_cast<int>(settings3DS.Theme)].dialogSelectedItemBackAlpha);
    }

    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding - offsetX, 10, settings3DS.SecondScreenWidth - horizontalPadding, 25, dialogTitleTextColor, HALIGN_LEFT, dialogTab.Title.c_str());
    ui3dsDrawStringWithWrapping(settings3DS.SecondScreen, horizontalPadding - offsetX, 30, settings3DS.SecondScreenWidth - horizontalPadding, 70, dialogTextColor, HALIGN_LEFT, dialogTab.DialogText.c_str());

    int menuStartY = settings3DS.Theme == Setting::Theme::RetroArch ? bottomHeight + 1 : bottomHeight + 3;
    menu3dsDrawItems(
        &dialogTab, horizontalPadding, menuStartY, DIALOG_HEIGHT,
        dialogSelectedItemBackColor,
        Themes[static_cast<int>(settings3DS.Theme)].selectedItemTextColor,
        dialogItemDescriptionTextColor,
        dialogTextColor,
        dialogItemDescriptionTextColor,
        dialogItemDescriptionTextColor,
        dialogTextColor,
        dialogTextColor,
        offsetX);
}

void menu3dsDrawEverything(int& currentMenuTab, std::vector<SMenuTab>& menuTabs) {
        ui3dsSetViewport(0, 0, settings3DS.SecondScreenWidth, SCREEN_HEIGHT);
        ui3dsSetTranslate(0, 0);
        ui3dsDrawRect(0, 0, settings3DS.SecondScreenWidth, 0, 0x000000);
        ui3dsSetTranslate(0, 0);
        menu3dsDrawMenu(menuTabs, currentMenuTab, 0, 0);

        swapBuffer = true;
}

void menu3dsDrawEverything(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, int menuFrame, int menuItemsFrame, int dialogFrame)
{
    if (!isDialog)
    {
        int y = 0 + menuFrame * menuFrame * 120 / 32;

        ui3dsSetViewport(0, 0, settings3DS.SecondScreenWidth, SCREEN_HEIGHT);
        ui3dsSetTranslate(0, 0);
        ui3dsDrawRect(0, 0, settings3DS.SecondScreenWidth, y, 0x000000);
        ui3dsSetTranslate(0, y);
        menu3dsDrawMenu(menuTabs, currentMenuTab, menuItemsFrame, y);

        bool showThumb = !menuItemsFrame && !isScrolling
            && menuTabs[currentMenuTab].Title == "Load Game"
            && settings3DS.GameThumbnailType != Setting::ThumbnailMode::None;

        if (showThumb)
            menu3dsUpdateThumb(&menuTabs[currentMenuTab]);
    }
    else
    {
        int y = 80 + dialogFrame * dialogFrame * 80 / 32;

        ui3dsSetViewport(0, 0, settings3DS.SecondScreenWidth, y);
        ui3dsSetTranslate(0, 0);
        menu3dsDrawMenu(menuTabs, currentMenuTab, 0, 0);
        ui3dsDrawRect(0, 0, settings3DS.SecondScreenWidth, y, 0x000000, (float)(ANIMATE_DIALOG_STEPS - dialogFrame) / 10);

        ui3dsSetViewport(0, 0, settings3DS.SecondScreenWidth, SCREEN_HEIGHT);
        ui3dsSetTranslate(0, y);
        menu3dsDrawDialog(dialogTab);
        ui3dsSetTranslate(0, 0);
    }
    swapBuffer = true;

}

SMenuTab *menu3dsAnimateTab(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, int direction)
{
    SMenuTab *currentTab = &menuTabs[currentMenuTab];

    if (direction < 0)
    {
        for (int i = 1; i <= ANIMATE_TAB_STEPS; i++)
        {
            if (!aptMainLoop()) return currentTab;
            menu3dsDrawSplash();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, i, 0);
            menu3dsSwapBuffersAndWaitForVBlank();
        }

        currentMenuTab--;
        if (currentMenuTab < 0)
            currentMenuTab = static_cast<int>(menuTabs.size() - 1);
        currentTab = &menuTabs[currentMenuTab];
        
        for (int i = -ANIMATE_TAB_STEPS; i <= 0; i++)
        {
            if (!aptMainLoop()) return currentTab;
            menu3dsDrawSplash();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, i, 0);
            menu3dsSwapBuffersAndWaitForVBlank();
        }
    }
    else if (direction > 0)
    {
        for (int i = -1; i >= -ANIMATE_TAB_STEPS; i--)
        {
            if (!aptMainLoop()) return currentTab;
            menu3dsDrawSplash();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, i, 0);
            menu3dsSwapBuffersAndWaitForVBlank();
        }

        currentMenuTab++;
        if (currentMenuTab >= static_cast<int>(menuTabs.size()))
            currentMenuTab = 0;
        currentTab = &menuTabs[currentMenuTab];
        
        for (int i = ANIMATE_TAB_STEPS; i >= 0; i--)
        {            
            if (!aptMainLoop()) return currentTab;
            menu3dsDrawSplash();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, i, 0);
            menu3dsSwapBuffersAndWaitForVBlank();
        }
    }
    return currentTab;
}

// Displays the menu and allows the user to select from
// a list of choices.
//
int menu3dsMenuSelectItem(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    int framesDKeyHeld = 0;
    int returnResult = -1;
    char menuTextBuffer[512];
    float prevIOD = -1;
    bool wasScrolling = false;
    bool firstFrame = !isDialog;

    SMenuTab *currentTab = &menuTabs[currentMenuTab];

    if (!isDialog) {
        secondScreenDirty = true;

        // Query battery state once before entering the loop to avoid
        // spamming PTMU service calls on every frame during animations.
        ptmuInit();
        PTMU_GetBatteryChargeState(&currentChargeState);
        PTMU_GetBatteryLevel(&currentBatteryLevel);
        ptmuExit();
    }
    else {
        currentTab = &dialogTab;
    }

    while (aptMainLoop())
    {
        if (GPU3DS.emulatorState == EMUSTATE_END)
        {
            returnResult = -1;
            break;
        }

        hidScanInput();
        thisKeysHeld = hidKeysHeld();

        u32 keysDown = firstFrame ? 0 : (~lastKeysHeld) & thisKeysHeld;
        firstFrame = false;
        lastKeysHeld = thisKeysHeld;

        int maxItems = MENU_HEIGHT;
        if (isDialog)
            maxItems = DIALOG_HEIGHT;

        if (!currentTab->SubTitle.empty())
        {
            maxItems--;
        }

        if ((thisKeysHeld & KEY_UP) || (thisKeysHeld & KEY_DOWN) || (thisKeysHeld & KEY_LEFT) || (thisKeysHeld & KEY_RIGHT))
            framesDKeyHeld ++;
        else
            framesDKeyHeld = 0;

        // continue game via start button
        if (keysDown & KEY_START && settings3DS.isRomLoaded && !isDialog)
        {
            returnResult = MENU_CONTINUE_GAME;

            break;
        }

        if (keysDown & KEY_B)
        {
            if (isDialog) {
                returnResult = -1;

                break;
            }
            else if (currentTab->MenuItems[0].Text == PARENT_DIRECTORY_LABEL) { 
                // if current tab has parent directory, navigate to parent directory
                currentTab->MenuItems[0].SetValue(1);
                returnResult = currentTab->MenuItems[0].Value;

                break;
            }
            else {
                // scroll to top
                int lastSelectedItemIndex = currentTab->SelectedItemIndex;
                for (size_t i = 0; i < currentTab->MenuItems.size(); i++) {
                    if (currentTab->MenuItems[i].IsHighlightable()) {
                        currentTab->SelectedItemIndex = static_cast<int>(i);
                        currentTab->MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);

                        break;
                    }
                }

                if (lastSelectedItemIndex == currentTab->SelectedItemIndex && currentTab->Title != "Emulator") {
                    currentTab = menu3dsAnimateTab(dialogTab, isDialog, currentMenuTab, menuTabs, -1);
                } else {
                    secondScreenDirty = true;
                }
                
                //returnResult = 0;  
            }
        }
        if (keysDown & KEY_X && currentTab->Title == "Load Game")
        {
            returnResult = MENU_ENTRY_CONTEXT_MENU;
            break;
        }
        
        if ((keysDown & KEY_RIGHT) || (keysDown & KEY_R) || ((thisKeysHeld & KEY_RIGHT) && (framesDKeyHeld > 15) && (framesDKeyHeld % 2 == 0)))
        {
            if (!isDialog)
            {
                if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Gauge && !(keysDown & KEY_R))
                {
                    if (currentTab->MenuItems[currentTab->SelectedItemIndex].Value <
                        currentTab->MenuItems[currentTab->SelectedItemIndex].GaugeMaxValue)
                    {
                        currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(currentTab->MenuItems[currentTab->SelectedItemIndex].Value + 1);
                    }
                    secondScreenDirty = true;

                }
                else
                {
                    currentTab = menu3dsAnimateTab(dialogTab, isDialog, currentMenuTab, menuTabs, +1);
                }
            }
        }
        if ((keysDown & KEY_LEFT) || (keysDown & KEY_L)|| ((thisKeysHeld & KEY_LEFT) && (framesDKeyHeld > 15) && (framesDKeyHeld % 2 == 0)))
        {
            if (!isDialog)
            {
                if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Gauge && !(keysDown & KEY_L))
                {
                    // Gauge adjustment
                    if (currentTab->MenuItems[currentTab->SelectedItemIndex].Value >
                        currentTab->MenuItems[currentTab->SelectedItemIndex].GaugeMinValue)
                    {
                        currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(currentTab->MenuItems[currentTab->SelectedItemIndex].Value - 1);
                    }
                    secondScreenDirty = true;
                }
                else
                {
                    currentTab = menu3dsAnimateTab(dialogTab, isDialog, currentMenuTab, menuTabs, -1);
                }
            }
        }
        if (keysDown & KEY_A)
        {
            if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Action)
            {
                returnResult = currentTab->MenuItems[currentTab->SelectedItemIndex].Value;
                currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(1);
                break;
            }
            if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Radio)
            {
                // workaround:  use GaugeMinValue for radio group id to update state
                int groupId = currentTab->MenuItems[currentTab->SelectedItemIndex].GaugeMinValue;
                if (groupId) {
                    for (size_t i = 0; i < currentTab->MenuItems.size(); i++)
                    {
                        // match related items
                        if (currentTab->MenuItems[i].GaugeMinValue == groupId)
                        {
                            // uncheck active radio item, but don't set it to disabledItemTextColor
                            if (currentTab->MenuItems[i].Type == MenuItemType::Radio && currentTab->MenuItems[i].Value == RADIO_ACTIVE_CHECKED)
                                currentTab->MenuItems[i].SetValue(RADIO_ACTIVE);
                            
                            if (currentTab->MenuItems[i].Type == MenuItemType::Radio && currentTab->MenuItems[i].Value == RADIO_INACTIVE_CHECKED)
                                currentTab->MenuItems[i].SetValue(RADIO_INACTIVE);

                        }
                    }
                }
                currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(RADIO_ACTIVE_CHECKED);
                secondScreenDirty = true;
            }
            if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Checkbox)
            {
                bool setEnabled = currentTab->MenuItems[currentTab->SelectedItemIndex].Value == 0;
                if (setEnabled)
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(1);
                else
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(0);

                if (currentTab->Title == "Cheats") {
                    menu3dsSetCheatsCount(currentTab->MenuItems[0],
                        setEnabled ? ++cheatsActive : --cheatsActive, cheatsTotal);
                }

                secondScreenDirty = true;
            }
            if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Picker)
            {
                int pickerDialogBackground;

                switch (currentTab->MenuItems[currentTab->SelectedItemIndex].PickerDialogType) {
                    case DIALOG_TYPE_SUCCESS:
                        pickerDialogBackground = Themes[static_cast<int>(settings3DS.Theme)].dialogColorSuccess;
                        break;
                    case DIALOG_TYPE_WARN:
                        pickerDialogBackground = Themes[static_cast<int>(settings3DS.Theme)].dialogColorWarn;
                        break;
                    default:
                        pickerDialogBackground = Themes[static_cast<int>(settings3DS.Theme)].dialogColorInfo;
                        break;
                }

                snprintf(menuTextBuffer, sizeof(menuTextBuffer), "%s", currentTab->MenuItems[currentTab->SelectedItemIndex].Text.c_str());
                int resultValue = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, menuTextBuffer,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].PickerDescription,
                    pickerDialogBackground,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].PickerItems,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].Value
                    );

                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);

                if (resultValue != -1)
                {
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(resultValue);
                }

                secondScreenDirty = true;
            }
        }

        wasScrolling = isScrolling;
        isScrolling = framesDKeyHeld > 15 && ((thisKeysHeld & KEY_UP) || (thisKeysHeld & KEY_DOWN));
        
        bool repeatFrame = framesDKeyHeld > 15 && (framesDKeyHeld % 2 == 0);

        if ((keysDown & KEY_UP) || (repeatFrame && (thisKeysHeld & KEY_UP)))
        {
            size_t moveCursorTimes = 0;

            do
            {
                if (thisKeysHeld & KEY_Y)
                {
                    currentTab->SelectedItemIndex -= 13;
                    if (currentTab->SelectedItemIndex < 0)
                        currentTab->SelectedItemIndex = 0;
                }
                else
                {
                    currentTab->SelectedItemIndex--;
                    if (currentTab->SelectedItemIndex < 0)
                    {
                        currentTab->SelectedItemIndex = currentTab->MenuItems.size() - 1;
                    }
                }
                moveCursorTimes++;
            }
            while (
                (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Disabled ||
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Header1 ||
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Header2 ||
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Textarea ||
               (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Gauge && menu3dsGaugeIsDisabled(currentTab, currentTab->SelectedItemIndex))
                ) &&
                moveCursorTimes < currentTab->MenuItems.size());

            currentTab->MakeSureSelectionIsOnScreen(maxItems, isDialog ? 1 : 2);
            secondScreenDirty = true;

        }
        if ((keysDown & KEY_DOWN) || (repeatFrame && (thisKeysHeld & KEY_DOWN)))
        {
            size_t moveCursorTimes = 0;
            do
            {
                if (thisKeysHeld & KEY_Y)
                {
                    currentTab->SelectedItemIndex += 13;
                    if (currentTab->SelectedItemIndex >= static_cast<int>(currentTab->MenuItems.size()))
                        currentTab->SelectedItemIndex = currentTab->MenuItems.size() - 1;
                }
                else
                {
                    currentTab->SelectedItemIndex++;
                    if (currentTab->SelectedItemIndex >= static_cast<int>(currentTab->MenuItems.size()))
                    {
                        currentTab->SelectedItemIndex = 0;
                        currentTab->FirstItemIndex = 0;
                    }
                }
                moveCursorTimes++;
            }
            while (
                (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Disabled ||
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Header1 ||
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Header2 ||
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Textarea ||
               (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Gauge && menu3dsGaugeIsDisabled(currentTab, currentTab->SelectedItemIndex))
                ) &&
                moveCursorTimes < currentTab->MenuItems.size());

            currentTab->MakeSureSelectionIsOnScreen(maxItems, isDialog ? 1 : 2);
            secondScreenDirty = true;
        }

        // user just stopped scrolling
        if (wasScrolling && !isScrolling)
        {
            secondScreenDirty = true;
        }

        const bool set3DEnabled =
            gpu3dsIs3DAvailable()
            && !settings3DS.Disable3DSlider
            && settings3DS.GameScreen == GFX_TOP;
        gfxSet3D(set3DEnabled);
        float iod = set3DEnabled ? gpu3dsGetIOD() : 0.0f;

        if (iod != prevIOD) {
            gameScreenDirty = true;
            prevIOD = iod;
        }

        // input -> splash -> menu -> vblank
        // GPU renders splash on game screen
        // while CPU prepares menu/dialog on second screen, then sync at vblank

        if ((gameScreenDirty && !isDialog) || !settings3DS.isRomLoaded) {
            GSPGPU_FramebufferFormat gpuBufFmt = (GSPGPU_FramebufferFormat)DISPLAY_TRANSFER_FMT;
            if (gfxGetScreenFormat(settings3DS.GameScreen) != gpuBufFmt) {
                gfxSetScreenFormat(settings3DS.GameScreen, gpuBufFmt);
            }

            int passes = GPU3DS.doubleBufferDesync ? 2 : 1;
            for (int pass = 0; pass < passes; pass++) {
                gpu3dsFrameBegin();
                    if (settings3DS.isRomLoaded) {
                        // dim ingame screen
                        notif3dsTrigger(Notif::Event::Paused, Notif::Type::Default, settings3DS.GameScreen);
                        notif3dsSync();
                        impl3dsSceneRender(true, true);
                        notif3dsHide();
                    } else {
                        bool isTopStereo = gpu3dsIs3DEnabled();
                        gpu3dsClearScreen(settings3DS.GameScreen, isTopStereo);
                        img3dsDrawSplash(UI_ATLAS, isTopStereo, iod);
                    }
                gpu3dsFrameEnd();
            }

            gameScreenDirty = false;
        }

        if (secondScreenDirty) {
            if (gfxGetScreenFormat(settings3DS.SecondScreen) != GSP_RGB565_OES) {
                gfxSetScreenFormat(settings3DS.SecondScreen, GSP_RGB565_OES);
            }

            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs);
            secondScreenDirty = false;
        }

        menu3dsSwapBuffersAndWaitForVBlank();
    }

    return returnResult;
}

void menu3dsShowSplashMessage(const char *message) {
    if (!aptMainLoop()) return;

    gfxScreen_t screen = settings3DS.SecondScreen;

    if (gfxGetScreenFormat(screen) != GSP_RGB565_OES) {
        gfxSetScreenFormat(screen, GSP_RGB565_OES);
        // let GSP finish reconfiguring before we touch the buffer
        gpu3dsWaitForVBlank(screen);
    }

    u16 w, h; // note: w = 240, h = 400/320!
    u32 bufferSize;

    for (int i = 0; i < 2; i++) {
        u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &w, &h);
        bufferSize = w * h * 2;
        memset(fb, 0, bufferSize);
        GSPGPU_FlushDataCache(fb, bufferSize);
        gfxScreenSwapBuffers(screen, false);
        gpu3dsWaitForVBlank(screen);
    }

    u8 *fb = gfxGetFramebuffer(screen, GFX_LEFT, &w, &h);
    bufferSize = w * h * 2;

    u16 x0 = 0;
    u16 y0 = (SCREEN_HEIGHT - FONT_HEIGHT) / 2;
    u16 x1 = h;
    u16 y1 = y0 + FONT_HEIGHT;

    ui3dsDrawStringWithNoWrapping(screen, x0, y0, x1, y1, 0xFFFFFF, HALIGN_CENTER, message);
    GSPGPU_FlushDataCache(fb, bufferSize);
    gfxScreenSwapBuffers(screen, false);
    gpu3dsWaitForVBlank(screen);
}


void menu3dsAddTab(std::vector<SMenuTab>& menuTabs, const char *title, const std::vector<SMenuItem>& menuItems)
{
    menuTabs.emplace_back();
    SMenuTab *currentTab = &menuTabs.back();

    currentTab->SetTitle(title);
    currentTab->MenuItems = menuItems;

    currentTab->FirstItemIndex = 0;
    currentTab->SelectedItemIndex = 0;
    for (size_t i = 0; i < currentTab->MenuItems.size(); i++)
    {
        if (menuItems[i].IsHighlightable())
        {
            currentTab->SelectedItemIndex = static_cast<int>(i);
            currentTab->MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);
            break;
        }
    }
}

void menu3dsSelectRandomGameIndex(SMenuTab& currentTab, int min, int max, int lastSelected) {
    currentTab.SelectedItemIndex = utils3dsGetRandomInt(min, max, lastSelected);
    currentTab.MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);
    currentTab.MenuItems[currentTab.SelectedItemIndex].SetValue(1);
}

void menu3dsSetScreenDirty(bool gameScreen, bool secondScreen) {
    if (gameScreen)    gameScreenDirty = true;
    if (secondScreen)  secondScreenDirty = true;
}

void menu3dsHideMenu(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    ui3dsSetTranslate(0, 0);
}

int menu3dsShowDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, const std::string& title, const std::string& dialogText, int newDialogBackColor, const std::vector<SMenuItem>& menuItems, int selectedID, bool fadeIn)
{
    SMenuTab *currentTab = &dialogTab;

    dialogBackColor = newDialogBackColor;

    currentTab->SetTitle(title);
    currentTab->DialogText.assign(dialogText);
    currentTab->MenuItems = menuItems;

    currentTab->FirstItemIndex = 0;
    currentTab->SelectedItemIndex = 0;

    for (size_t i = 0; i < currentTab->MenuItems.size(); i++)
    {
        if ((selectedID == -1 && menuItems[i].IsHighlightable()) || 
            menuItems[i].Value == selectedID)
        {
            currentTab->SelectedItemIndex = static_cast<int>(i);
            currentTab->MakeSureSelectionIsOnScreen(DIALOG_HEIGHT, 1);
            break;
        }
    }

    isDialog = true;

    for (int f = fadeIn ? ANIMATE_DIALOG_STEPS : 0; f >= 0; f--)
    {
        if (!aptMainLoop()) break;
        menu3dsDrawSplash();
        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, 0, f);
        menu3dsSwapBuffersAndWaitForVBlank();  
    }

    // Execute the dialog and return result.
    //
    if (currentTab->MenuItems.size() > 0)
    {
        int result = menu3dsMenuSelectItem(dialogTab, isDialog, currentMenuTab, menuTabs);

        return result;
    }
    return 0;
}


void menu3dsShowRomLoadingDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, const std::string& title, const std::string& text, int dialogColor)
{
    if (settings3DS.isRomLoaded) {
        menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTabs, title, text, dialogColor, std::vector<SMenuItem>());

        return;
    }

    dialogBackColor = dialogColor;

    SMenuTab *currentTab = &dialogTab;
    currentTab->SetTitle(title);
    currentTab->DialogText.assign(text);
    currentTab->MenuItems.clear();
    currentTab->FirstItemIndex = 0;
    currentTab->SelectedItemIndex = 0;

    isDialog = true;

    int fadeSteps = 24;

    for (int f = fadeSteps; f >= 0; f--)
    {
        if (!aptMainLoop()) break;
        float fade = (float)f / fadeSteps;
        menu3dsDrawSplash(fade);

        int dialogFrame = f - fadeSteps + ANIMATE_DIALOG_STEPS;
        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, 0, dialogFrame < 0 ? 0 : dialogFrame);
        menu3dsSwapBuffersAndWaitForVBlank();
    }
}

void menu3dsHideDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, bool fadeOut)
{
    // fade the dialog out
    //
    if (fadeOut) {
        for (int f = 0; f <= ANIMATE_DIALOG_STEPS; f++)
        {
            if (!aptMainLoop()) break;
            menu3dsDrawSplash();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, 0, f);
            menu3dsSwapBuffersAndWaitForVBlank();    
        }
    }

    isDialog = false;
    
    // draw the updated menu
    //
    
    menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs);
    menu3dsSwapBuffersAndWaitForVBlank();  
    
}

void menu3dsSetHotkeysData(const char* hotkeysData[HOTKEYS_COUNT][3]) {
    for (int i = 0; i < HOTKEYS_COUNT; i++) {
        switch(i) {
            case HOTKEY_OPEN_MENU: 
                hotkeysData[i][0]= "OpenEmulatorMenu";
                hotkeysData[i][1]= "  Open Emulator Menu"; 
                hotkeysData[i][2]= "";
                break;
            case HOTKEY_DISABLE_FRAMELIMIT: 
                hotkeysData[i][0]= "DisableFramelimitHold"; 
                hotkeysData[i][1]= "  Fast-Forward"; 
                hotkeysData[i][2]= "May corrupt/freeze games on Old 3DS";
                break;
            case HOTKEY_SWAP_CONTROLLERS: 
                hotkeysData[i][0]= "SwapControllers"; 
                hotkeysData[i][1]= "  Swap Controllers"; 
                hotkeysData[i][2]= "Allows you to control Player 2 (e.g. for using Konami Cheat)";
                break;
            case HOTKEY_SCREENSHOT: 
                hotkeysData[i][0]= "TakeScreenshot"; 
                hotkeysData[i][1]= "  Screenshot"; 
                hotkeysData[i][2]= "Takes a Screenshot from the current game";
                break;
            case HOTKEY_QUICK_SAVE: 
                hotkeysData[i][0]= "QuickSave"; 
                hotkeysData[i][1]= "  Quick Save"; 
                hotkeysData[i][2]= "Saves the Game to last used Save Slot (Default:  Slot #1)";
                break;
            case HOTKEY_QUICK_LOAD: 
                hotkeysData[i][0]= "QuickLoad"; 
                hotkeysData[i][1]= "  Quick Load"; 
                hotkeysData[i][2]= "Loads the Game from last used Load Slot (Default: Slot #1)";
                break;
            case HOTKEY_SAVE_SLOT_NEXT: 
                hotkeysData[i][0]= "SaveSlotNext"; 
                hotkeysData[i][1]= "  Save Slot +"; 
                hotkeysData[i][2]= "Selects next Save Slot";
                break;
            case HOTKEY_SAVE_SLOT_PREV: 
                hotkeysData[i][0]= "SaveSlotPrev"; 
                hotkeysData[i][1]= "  Save Slot -"; 
                hotkeysData[i][2]= "Selects previous Save Slot";
                break;
            default: 
                hotkeysData[i][0]= ""; 
                hotkeysData[i][1]= "  <empty>"; 
                hotkeysData[i][2]= ""; 
        }
    }
}

// TODO: re-add option in menu
void menu3dsSetRomInfo() {
    int margin = 8;

    char info[512];
    Memory.MakeRomInfoText(info);
    ui3dsDrawStringWithWrapping(settings3DS.SecondScreen, margin, margin, settings3DS.SecondScreenWidth - margin, SCREEN_HEIGHT - margin, 0xffffff, HALIGN_LEFT, info);
}
