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
#include "3dsra.h"
#include "3dsra_ui.h"

#define ANIMATE_DIALOG_STEPS 8

static const int MENU_LIST_TOP    = 29;
static const int MENU_LIST_BOTTOM = SCREEN_HEIGHT - 20;

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

static int dialogTextLines = -1; // -1 = fixed-height dialog

// Number of option rows a dialog shows at once (scroll window)
static int menu3dsGetDialogVisibleItems()
{
    return dialogTextLines > 0 ? 3 : 5;
}

static void menu3dsGetDialogLayout(int& topHeight, int& bottomHeight)
{
    if (dialogTextLines > 0)
    {
        topHeight = 35 + dialogTextLines * FONT_HEIGHT;
        bottomHeight = 19 + menu3dsGetDialogVisibleItems() * FONT_HEIGHT;
        if (topHeight + bottomHeight > SCREEN_HEIGHT)
            topHeight = SCREEN_HEIGHT - bottomHeight;
    }
    else
    {
        topHeight = 76;
        bottomHeight = 84;
    }
}

MenuButton bottomMenuButtons[] = {
    {"Select", UI_ICON_BUTTON_A, 0x800d1d, BTN_SHOW_ALWAYS},
    {"Back", UI_ICON_BUTTON_B, 0x999409, BTN_SHOW_ALWAYS},
    {"Options", UI_ICON_BUTTON_X, 0x0d5280, BTN_SHOW_FILE_TAB},
    {"Fast Scroll", UI_ICON_BUTTON_Y, 0x0d8014, BTN_SHOW_FILE_OR_SUBPAGE}
};


typedef enum {
    THUMB_GAME,
    THUMB_SAVESTATE,
} ThumbSource;

// Load and draw the second-screen thumbnail for the currently selected row.
bool menu3dsUpdateThumb(SMenuTab *currentTab, ThumbSource source)
{
    const char* text = currentTab->MenuItems[currentTab->SelectedItemIndex].Text.c_str();
    bool loaded = false;

    if (source == THUMB_GAME) {
        if (strncmp(text, MENU_PREFIX_FILE, strlen(MENU_PREFIX_FILE)) == 0)
            loaded = img3dsLoadThumb(text + strlen(MENU_PREFIX_FILE));
    } else {
        const char* marker = strstr(text, "Slot #");
        int slot = marker ? atoi(marker + strlen("Slot #")) : 0;

        if (slot >= 1 && slot <= SAVESLOTS_MAX) {
            char path[PATH_MAX];
            impl3dsGetScreenshotPath(SCREENSHOT_SAVESTATE, slot, path, sizeof(path));
            loaded = img3dsLoadStateScreenshot(path);
        }
    }

    if (loaded) {
        bool ra = settings3DS.Theme == Setting::Theme::RetroArch;
        img3dsDrawThumb(ra ? 8 : 0, ra ? 18 : 20);
        return true;
    }

    return false;
}

void menu3dsDrawSplash(float fade = 1.0f)
{
    if (settings3DS.isRomLoaded)
        return;

    float iod = gpu3dsGetIOD();
    bool renderRightEye = iod != 0;

    GSPGPU_FramebufferFormat gpuBufFmt = (GSPGPU_FramebufferFormat)DISPLAY_TRANSFER_FMT;
    if (gfxGetScreenFormat(settings3DS.GameScreen) != gpuBufFmt)
        gfxSetScreenFormat(settings3DS.GameScreen, gpuBufFmt);

    gpu3dsFrameBegin();
        gpu3dsClearScreen(settings3DS.GameScreen, renderRightEye);
        img3dsDrawSplash(UI_SPLASH, renderRightEye, iod, fade);
    gpu3dsFrameEnd();
}

void menu3dsSetCheatsCount(SMenuTab& tab, int active, int total) {
    cheatsActive = active;
    cheatsTotal = total;

    if (total)
        tab.SubTitle = "ENABLED CHEAT CODES: " + std::to_string(active) + "/" + std::to_string(total);
    else
        tab.SubTitle.clear();
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
            
            if (!currentTab->MenuItems[i].Description.empty())
            {
                ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Description.c_str());
            }
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
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;
                

            int trackSize = 14;
            int checkboxOffsetX = settings3DS.SecondScreenWidth - horizontalPadding - trackSize;
            int descriptionOffsetX = checkboxOffsetX - 3;
            if (!currentTab->MenuItems[i].Description.empty()) {
                int descriptionWidth = ui3dsGetStringWidth(currentTab->MenuItems[i].Description.c_str());
                descriptionOffsetX = descriptionOffsetX - descriptionWidth;
                ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, descriptionOffsetX, y, descriptionOffsetX + descriptionWidth, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Description.c_str());
            }

            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, descriptionOffsetX, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            
            char trackGlyph[2] = { UI_ICON_PILL, '\0' };
            char trackKnobGlyph[2] = { UI_ICON_BULLET_6, '\0' };
            int trackActiveColor = settings3DS.Theme == Setting::Theme::RetroArch ? 0x4dbf4d : Themes[static_cast<int>(settings3DS.Theme)].headerItemTextColor;
            int trackColor = currentTab->MenuItems[i].Value == 1 ? trackActiveColor : disabledItemTextColor;

            int trackKnobSize = 6;
            int trackKnobX = checkboxOffsetX + (currentTab->MenuItems[i].Value != 1 ? 1 : trackSize - 1 - trackKnobSize);
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, checkboxOffsetX, y - 1, checkboxOffsetX + trackSize + 1, y + fontHeight, trackColor, HALIGN_LEFT, trackGlyph);
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, trackKnobX, y, trackKnobX + trackKnobSize, y + fontHeight, 0xffffff, HALIGN_LEFT, trackKnobGlyph);
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Radio)
        {
            RadioState val = static_cast<RadioState>(currentTab->MenuItems[i].Value);

            color = (val == RADIO_INACTIVE || val == RADIO_INACTIVE_CHECKED) ? disabledItemTextColor : normalItemTextColor;
            bool isSelected = val == RADIO_ACTIVE_CHECKED || val == RADIO_INACTIVE_CHECKED;
            if (currentTab->SelectedItemIndex == i) {
                color = selectedItemTextColor;
            }
            
            char iconText[2] = { (char)(isSelected ? UI_ICON_RADIO_BTN_SELECTED : UI_ICON_RADIO_BTN), '\0' };
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, settings3DS.SecondScreenWidth - 100 + 60, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, iconText);
        }

        else if (currentTab->MenuItems[i].Type == MenuItemType::Gauge)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;

            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, settings3DS.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

            int diff = currentTab->MenuItems[i].GaugeMaxValue - currentTab->MenuItems[i].GaugeMinValue;

            const int barWidth = 40;
            const int barRight = settings3DS.SecondScreenWidth - horizontalPadding;
            const int barLeft  = barRight - barWidth;

            int posX = barLeft + (currentTab->MenuItems[i].Value - currentTab->MenuItems[i].GaugeMinValue) * barWidth / diff;
            if (posX < barLeft)  posX = barLeft;
            else if (posX > barRight) posX = barRight;

            int barY = y + fontHeight / 2 + 1;
            ui3dsDrawRect(barLeft, barY, barRight, barY + 1, disabledItemTextColor); // full track
            ui3dsDrawRect(barLeft, barY, posX, barY + 1, selectedItemTextColor); // active fill

            char knob[2] = { UI_ICON_BULLET_5, 0 };
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, posX - 2, y, posX + 3, y + fontHeight, color, HALIGN_LEFT, knob);

            // show the numeric value in front of the bar.
            if (!currentTab->MenuItems[i].Description.empty()) {
                char valueText[12];
                snprintf(valueText, sizeof(valueText), "%d", currentTab->MenuItems[i].Value);
                ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, horizontalPadding, y, 246, y + fontHeight, color, HALIGN_RIGHT, valueText);
            }
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
    if (settings3DS.Theme != Setting::Theme::RetroArch && currentTab->FirstItemIndex != 0)
    {
        char iconText[2] = { (char)UI_ICON_CHEVRON_UP, '\0' };
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, settings3DS.SecondScreenWidth - horizontalPadding, menuStartY, settings3DS.SecondScreenWidth, menuStartY + fontHeight, disabledItemTextColor, HALIGN_CENTER, iconText);
    }

    // Draw the "down arrow" to indicate more options available at bottom
    //
    if (settings3DS.Theme != Setting::Theme::RetroArch && currentTab->FirstItemIndex + maxItems < static_cast<int>(currentTab->MenuItems.size()))
    {
        char iconText[2] = { (char)UI_ICON_CHEVRON_DOWN, '\0' };
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, settings3DS.SecondScreenWidth - horizontalPadding, menuStartY + (maxItems - 1) * fontHeight, settings3DS.SecondScreenWidth, menuStartY + maxItems * fontHeight, disabledItemTextColor, HALIGN_CENTER, iconText);
    }
    
}

int menu3dsGetListVisibleItems(int footerHeight)
{
    const int rowPitch  = 13;
    const int rowHeight = MENU_ITEM_HEIGHT;
    int listBottom = MENU_LIST_BOTTOM - footerHeight;
    int rows = (listBottom - rowHeight - MENU_LIST_TOP) / rowPitch + 1;
    return rows < 1 ? 1 : rows;
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
        if (i == TAB_CHEATS && cheatsTotal > 0) {
            int offsetX = settings3DS.SecondScreen == GFX_TOP ? 19 : 14;
            char iconText[2] = { (char)UI_ICON_BULLET_5, '\0' };
            ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, xRight - offsetX + 1, yTextTop - 3, xRight, yCurrentTabBoxTop, cheatsActive > 0 ? accentColor : color, HALIGN_LEFT, iconText);
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
        
        bool isFileTab = menu3dsIsFileTab(currentMenuTab, menuTabs);
        bool inSubPage = currentTab->IsSubPage();
        bool visible =
            button.visibility == BTN_SHOW_ALWAYS ||
            (button.visibility == BTN_SHOW_FILE_TAB && isFileTab) ||
            (button.visibility == BTN_SHOW_FILE_OR_SUBPAGE && (isFileTab || inSubPage));

        if (visible) {
            const char* label = button.label;
            if (currentTab->subPage.footerHeight > 0 && strcmp(button.label, "Select") == 0)
                label = currentTab->subPage.textView ? "Image View" : "Text View";

            char iconText[2] = { (char)button.icon, '\0' };
            ui3dsDrawRect(bottomMenuPosX + 2, SCREEN_HEIGHT - 13, bottomMenuPosX + 9, SCREEN_HEIGHT - 5,0xffffff);
            bottomMenuPosX = ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, bottomMenuPosX, SCREEN_HEIGHT - 16, bottomMenuPosX + 12, SCREEN_HEIGHT, buttonColor, HALIGN_LEFT, iconText) + buttonRightMargin;
            bottomMenuPosX = ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, bottomMenuPosX, SCREEN_HEIGHT - 17, bottomMenuPosX + 100, SCREEN_HEIGHT, Themes[static_cast<int>(settings3DS.Theme)].menuBottomBarTextColor, HALIGN_LEFT, label) + buttonLeftMargin;
        }
    }

    const int rightEdge = battX2 - battFullLevelWidth - battBorderWidth - 6;
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, 97, SCREEN_HEIGHT - 17, rightEdge, SCREEN_HEIGHT, Themes[static_cast<int>(settings3DS.Theme)].menuBottomBarTextColor, HALIGN_RIGHT, settings3dsGetAppVersion("v", GPU3DS.isReal3DS ? "" : "e"));
    
    int menuStartY = MENU_LIST_TOP;
    int maxItems = menu3dsGetListVisibleItems(currentTab->subPage.footerHeight);

    int menuBackColor = Themes[static_cast<int>(settings3DS.Theme)].menuBackColor;
    int selectedItemBackColor = menu3dsHasHighlightableItems(currentTab) ? Themes[static_cast<int>(settings3DS.Theme)].selectedItemBackColor : -1;

    ui3dsSetTranslate(menuItemFrame * 3, translateY);

    if (currentTab->subPage.footerHeight > 0 && currentTab->subPage.drawFooter) {
        int subPageFooterTop = MENU_LIST_BOTTOM - currentTab->subPage.footerHeight;
        currentTab->subPage.drawFooter(currentTab->SelectedItemIndex, currentTab->subPage.textView,
            subPageFooterTop, currentTab->subPage.footerHeight, menuItemFrame, menuBackColor);
    }

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

        if (menu3dsIsFileTab(currentMenuTab, menuTabs) && file3dsIsCurrentDirLoadedFromCache()) {
            char cacheBadgeText[16];
            snprintf(cacheBadgeText, sizeof(cacheBadgeText), "%c Cached", UI_ICON_CHECKMARK);
            const int cacheBadgePaddingX = 0;
            const int cacheBadgeY = menuStartY - 1;
            const int cacheBadgeRight = settings3DS.SecondScreenWidth - 20;
            const int cacheBadgeLeft = cacheBadgeRight - ui3dsGetStringWidth(cacheBadgeText) - cacheBadgePaddingX;

            // Clear a dedicated area so long path subtitles don't overlap the cache badge.
            if (settings3DS.Theme == Setting::Theme::RetroArch) {
                int cb1 = Themes[static_cast<int>(settings3DS.Theme)].menuBackColor;
                int cb2 = ui3dsOverlayBlendColor(cb1, 0xededed);
                ui3dsDrawCheckerboard(cacheBadgeLeft - 4, cacheBadgeY, cacheBadgeRight, cacheBadgeY + 13, cb1, cb2);
            } else {
                ui3dsDrawRect(cacheBadgeLeft - 4, cacheBadgeY, cacheBadgeRight, cacheBadgeY + 13, menuBackColor);
            }
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
    int topHeight, bottomHeight;
    menu3dsGetDialogLayout(topHeight, bottomHeight);

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
    ui3dsDrawStringWithWrapping(settings3DS.SecondScreen, horizontalPadding - offsetX, 30, settings3DS.SecondScreenWidth - horizontalPadding, topHeight - 4, dialogTextColor, HALIGN_LEFT, dialogTab.DialogText.c_str());

    int menuStartY = topHeight + (settings3DS.Theme == Setting::Theme::RetroArch ? 9 : 11);
    menu3dsDrawItems(
        &dialogTab, horizontalPadding, menuStartY, menu3dsGetDialogVisibleItems(),
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

void menu3dsDrawEverything(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, int menuFrame, int menuItemsFrame, int dialogFrame, bool animationFinished)
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
            && menu3dsIsFileTab(currentMenuTab, menuTabs)
            && settings3DS.GameThumbnailType != Setting::ThumbnailMode::None;

        // wait until animationFinished = true, to prevent stutter due to png decoding
        bool showSlotScreenshot = !menuItemsFrame && !isScrolling && animationFinished
            && settings3DS.SaveStateScreenshots
            && currentMenuTab == TAB_EMULATOR;

        if (showThumb)
            menu3dsUpdateThumb(&menuTabs[currentMenuTab], THUMB_GAME);
        else if (showSlotScreenshot)
            menu3dsUpdateThumb(&menuTabs[currentMenuTab], THUMB_SAVESTATE);
    }
    else
    {
        int dialogTopHeight, dialogBottomHeight;
        menu3dsGetDialogLayout(dialogTopHeight, dialogBottomHeight);
        int y = (SCREEN_HEIGHT - dialogTopHeight - dialogBottomHeight) + dialogFrame * dialogFrame * 80 / 32;

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

static void menu3dsDrawLoadingDialog(
    SMenuTab& dialogTab,
    int& currentMenuTab,
    std::vector<SMenuTab>& menuTabs,
    int dialogFrame,
    int dialogHeight,
    int thumbWidth,
    int loadingDialogSteps)
{
    int openStep = loadingDialogSteps - dialogFrame;
    int yDialog = SCREEN_HEIGHT - (dialogHeight * openStep) / loadingDialogSteps;
    
    ui3dsSetViewport(0, 0, settings3DS.SecondScreenWidth, SCREEN_HEIGHT);
    ui3dsSetTranslate(0, 0);
    menu3dsDrawMenu(menuTabs, currentMenuTab, 0, 0);
    ui3dsDrawRect(0, 0, settings3DS.SecondScreenWidth, yDialog, 0x000000, (float)(loadingDialogSteps - dialogFrame) / 10);
    ui3dsSetTranslate(0, yDialog);

    bool ra = settings3DS.Theme == Setting::Theme::RetroArch;
    int offsetRight = ra ? 8 : 0;
    int offsetBottom = ra ? 18 : 20;

    {
        const int dialogTextColor = 0xffffff;
        const int offsetX = settings3DS.Theme == Setting::Theme::RetroArch ? 6 : 0;
        const int horizontalPadding = 32;
        const int horizontalPaddingRight = thumbWidth > 0 ? 8 : horizontalPadding;

        int dialogBackColorTop = settings3DS.Theme == Setting::Theme::Original ? ui3dsApplyAlphaToColor(dialogBackColor, 0.9f) : ui3dsOverlayBlendColor(Themes[static_cast<int>(settings3DS.Theme)].menuBackColor, 0xaaaaaa);
        ui3dsDrawRect(0, 0, settings3DS.SecondScreenWidth, dialogHeight, dialogBackColorTop);

        int dialogTitleTextColor =
            ui3dsApplyAlphaToColor(dialogBackColorTop, 1.0f - Themes[static_cast<int>(settings3DS.Theme)].dialogTextAlpha) +
            ui3dsApplyAlphaToColor(dialogTextColor, Themes[static_cast<int>(settings3DS.Theme)].dialogTextAlpha);

        ui3dsDrawStringWithWrapping(
            settings3DS.SecondScreen,
            horizontalPadding - offsetX, 10,
            settings3DS.SecondScreenWidth - thumbWidth - horizontalPaddingRight, 25,
            dialogTitleTextColor, HALIGN_LEFT, dialogTab.Title.c_str());

        int nameLines = dialogHeight < 90 ? 2 : 3;

        int bodyX0 = horizontalPadding - offsetX;
        int bodyX1 = settings3DS.SecondScreenWidth - thumbWidth - horizontalPaddingRight;

        const std::string& body = dialogTab.DialogText;
        size_t statusSplit = body.find(UI_TEXT_SECTION_SEPARATOR);

        int nameY0 = 26;
        int nameY1 = nameY0 + nameLines * FONT_HEIGHT;

        if (statusSplit == std::string::npos) {
            ui3dsDrawStringWithWrapping(settings3DS.SecondScreen, bodyX0, nameY0, bodyX1, nameY1 + FONT_HEIGHT * 2,
                dialogTextColor, HALIGN_LEFT, body.c_str());
        } else {
            std::string name = body.substr(0, statusSplit);
            std::string raInfo = body.substr(statusSplit + 1);

            int raColor =
                ui3dsApplyAlphaToColor(dialogBackColorTop, 0.3f) + ui3dsApplyAlphaToColor(dialogTextColor, 0.7f);

            ui3dsDrawStringWithWrapping(settings3DS.SecondScreen, bodyX0, nameY0, bodyX1, nameY1,
                dialogTextColor, HALIGN_LEFT, name.c_str(), nameLines);
            ui3dsDrawStringWithWrapping(settings3DS.SecondScreen, bodyX0, nameY1, bodyX1, nameY1 + FONT_HEIGHT * 2,
                raColor, HALIGN_LEFT, raInfo.c_str());
        }
    }

    ui3dsSetTranslate(0, 0);

    if (thumbWidth > 0) {
        img3dsDrawThumb(offsetRight, offsetBottom - (offsetBottom * openStep / loadingDialogSteps));
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
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, i, 0, false);
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
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, i, 0, false);
            menu3dsSwapBuffersAndWaitForVBlank();
        }
    }
    else if (direction > 0)
    {
        for (int i = -1; i >= -ANIMATE_TAB_STEPS; i--)
        {
            if (!aptMainLoop()) return currentTab;
            menu3dsDrawSplash();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, i, 0, false);
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
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTabs, 0, i, 0, false);
            menu3dsSwapBuffersAndWaitForVBlank();
        }
    }
    if (settings3DS.SaveStateScreenshots && currentMenuTab == TAB_EMULATOR) {
        secondScreenDirty = true;
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

        int maxItems = isDialog
            ? menu3dsGetDialogVisibleItems()
            : menu3dsGetListVisibleItems();

        if (!currentTab->SubTitle.empty())
        {
            maxItems--;
        }

        bool subPageActive = !isDialog && currentTab->IsSubPage();
        bool subPageHasFooter = subPageActive && currentTab->subPage.footerHeight > 0;
        if (subPageHasFooter)
            maxItems = menu3dsGetListVisibleItems(currentTab->subPage.footerHeight);

        if ((thisKeysHeld & KEY_UP) || (thisKeysHeld & KEY_DOWN) || (thisKeysHeld & KEY_LEFT) || (thisKeysHeld & KEY_RIGHT))
            framesDKeyHeld ++;
        else
            framesDKeyHeld = 0;

        if (keysDown & KEY_START && settings3DS.isRomLoaded)
        {
            returnResult = MENU_CONTINUE_GAME;

            break;
        }

        // B exits the sub-page and rebuilds the root list.
        if (subPageActive && (keysDown & KEY_B)) {
            int parentSelectedIndex = currentTab->subPage.parentSelectedIndex;
            int parentFirstItemIndex = currentTab->subPage.parentFirstItemIndex;
            currentTab->subPage = {};
            currentTab->SelectedItemIndex = parentSelectedIndex;
            currentTab->FirstItemIndex = parentFirstItemIndex;
            menu3dsMarkTabDirty(currentMenuTab);
            returnResult = -1;
            break;
        }
        // A toggles the footer layout and stays on the current row.
        if (subPageHasFooter) {
            if (keysDown & KEY_A) {
                currentTab->subPage.textView = !currentTab->subPage.textView;
                secondScreenDirty = true;
            }
            keysDown &= ~KEY_A;
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
                        currentTab->MakeSureSelectionIsOnScreen(menu3dsGetListVisibleItems(currentTab->subPage.footerHeight), 2);

                        break;
                    }
                }

                if (lastSelectedItemIndex == currentTab->SelectedItemIndex && currentMenuTab != TAB_EMULATOR) {
                    currentTab = menu3dsAnimateTab(dialogTab, isDialog, currentMenuTab, menuTabs, -1);
                } else {
                    secondScreenDirty = true;
                }
                
                //returnResult = 0;  
            }
        }
        if (keysDown & KEY_X && menu3dsIsFileTab(currentMenuTab, menuTabs))
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
                SMenuItem& item = currentTab->MenuItems[currentTab->SelectedItemIndex];
                // Set the same value just to fire the callback (e.g. the save).
                // The checkmark follows from the rebuild, not from this press
                item.SetValue(item.Value);
                secondScreenDirty = true;

                if (menu3dsHasDirtyTabs()) {
                    returnResult = -1;
                    break;
                }
            }
            if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Checkbox)
            {
                bool setEnabled = currentTab->MenuItems[currentTab->SelectedItemIndex].Value == 0;
                if (setEnabled)
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(1);
                else
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(0);

                if (currentMenuTab == TAB_CHEATS) {
                    menu3dsSetCheatsCount(*currentTab,
                        setEnabled ? ++cheatsActive : --cheatsActive, cheatsTotal);
                }

                secondScreenDirty = true;

                if (menu3dsHasDirtyTabs()) {
                    returnResult = -1;
                    break;
                }
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

                if (resultValue == MENU_CONTINUE_GAME)
                {
                    returnResult = MENU_CONTINUE_GAME;
                    break;
                }

                if (isDialog) {
                    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
                }

                if (resultValue != -1)
                {
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(resultValue);
                }

                secondScreenDirty = true;

                if (menu3dsHasDirtyTabs()) {
                    returnResult = -1;
                    break;
                }
            }
        }

        wasScrolling = isScrolling;
        isScrolling = framesDKeyHeld > 15 && ((thisKeysHeld & KEY_UP) || (thisKeysHeld & KEY_DOWN));
        
        bool repeatFrame = framesDKeyHeld > 15 && (framesDKeyHeld % 2 == 0);

        if ((keysDown & KEY_UP) || (repeatFrame && (thisKeysHeld & KEY_UP)))
        {
            int itemCount = static_cast<int>(currentTab->MenuItems.size());

            if (thisKeysHeld & KEY_Y)
            {
                // Page up once, clamp, then land on the nearest highlightable item:
                // search up first, fall back to down at the top boundary.
                int idx = currentTab->SelectedItemIndex - maxItems;
                if (idx < 0)
                    idx = 0;
                int scan = idx;
                while (scan >= 0 && !currentTab->MenuItems[scan].IsHighlightable())
                    scan--;
                if (scan < 0)
                {
                    scan = idx;
                    while (scan < itemCount && !currentTab->MenuItems[scan].IsHighlightable())
                        scan++;
                }
                if (scan >= 0 && scan < itemCount)
                    currentTab->SelectedItemIndex = scan;
            }
            else
            {
                size_t moveCursorTimes = 0;
                do
                {
                    currentTab->SelectedItemIndex--;
                    if (currentTab->SelectedItemIndex < 0)
                        currentTab->SelectedItemIndex = itemCount - 1;
                    moveCursorTimes++;
                }
                while (!currentTab->MenuItems[currentTab->SelectedItemIndex].IsHighlightable() &&
                       moveCursorTimes < currentTab->MenuItems.size());
            }

            currentTab->MakeSureSelectionIsOnScreen(maxItems, isDialog ? 1 : 2);
            secondScreenDirty = true;

        }
        if ((keysDown & KEY_DOWN) || (repeatFrame && (thisKeysHeld & KEY_DOWN)))
        {
            int itemCount = static_cast<int>(currentTab->MenuItems.size());

            if (thisKeysHeld & KEY_Y)
            {
                // Page down once, clamp, then land on the nearest highlightable item:
                // search down first, fall back to up at the bottom boundary.
                int idx = currentTab->SelectedItemIndex + maxItems;
                if (idx >= itemCount)
                    idx = itemCount - 1;
                int scan = idx;
                while (scan < itemCount && !currentTab->MenuItems[scan].IsHighlightable())
                    scan++;
                if (scan >= itemCount)
                {
                    scan = idx;
                    while (scan >= 0 && !currentTab->MenuItems[scan].IsHighlightable())
                        scan--;
                }
                if (scan >= 0 && scan < itemCount)
                    currentTab->SelectedItemIndex = scan;
            }
            else
            {
                size_t moveCursorTimes = 0;
                do
                {
                    currentTab->SelectedItemIndex++;
                    if (currentTab->SelectedItemIndex >= itemCount)
                    {
                        currentTab->SelectedItemIndex = 0;
                        currentTab->FirstItemIndex = 0;
                    }
                    moveCursorTimes++;
                }
                while (!currentTab->MenuItems[currentTab->SelectedItemIndex].IsHighlightable() &&
                       moveCursorTimes < currentTab->MenuItems.size());
            }

            currentTab->MakeSureSelectionIsOnScreen(maxItems, isDialog ? 1 : 2);
            secondScreenDirty = true;
        }

        // user just stopped scrolling
        if (wasScrolling && !isScrolling)
        {
            secondScreenDirty = true;
        }

        gpu3dsSetTopMode();

        float iod = gpu3dsGetIOD();

        if (!isDialog && iod != prevIOD) {
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

            int passes = GPU3DS.gameScreenBufferDesync ? 2 : 1;
            for (int pass = 0; pass < passes; pass++) {
                gpu3dsFrameBegin();
                    if (settings3DS.isRomLoaded) {
                        // dim ingame screen
                        notif3dsTrigger(Notif::Event::Paused, Notif::Type::Default, settings3DS.GameScreen);
                        notif3dsSync();
                        impl3dsSceneRender(true, true);
                        notif3dsHide();
                    } else {
                        bool renderRightEye = iod != 0;
                        gpu3dsClearScreen(settings3DS.GameScreen, renderRightEye);
                        img3dsDrawSplash(UI_SPLASH, renderRightEye, iod);
                    }
                gpu3dsFrameEnd();
            }
            GPU3DS.gameScreenBufferDesync = false;

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
            currentTab->MakeSureSelectionIsOnScreen(menu3dsGetListVisibleItems(currentTab->subPage.footerHeight), 2);
            break;
        }
    }
}

void menu3dsSelectRandomGameIndex(SMenuTab& currentTab, int min, int max, int lastSelected) {
    currentTab.SelectedItemIndex = utils3dsGetRandomInt(min, max, lastSelected);
    currentTab.MakeSureSelectionIsOnScreen(menu3dsGetListVisibleItems(currentTab.subPage.footerHeight), 2);
    currentTab.MenuItems[currentTab.SelectedItemIndex].SetValue(1);
}

void menu3dsSetScreenDirty(bool gameScreen, bool secondScreen) {
    if (gameScreen)    gameScreenDirty = true;
    if (secondScreen)  secondScreenDirty = true;
}

void menu3dsMarkTabDirty(int tab) {
    if (tab >= 0 && tab < TAB_DIRTY_COUNT)
        settings3DS.menuTabDirty[tab] = true;
}

bool menu3dsHasDirtyTabs() {
    for (int i = 0; i < TAB_DIRTY_COUNT; i++)
        if (settings3DS.menuTabDirty[i])
            return true;

    return false;
}

void menu3dsHideMenu(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs)
{
    ui3dsSetTranslate(0, 0);
}

int menu3dsShowDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, const std::string& title, const std::string& dialogText, int newDialogBackColor, const std::vector<SMenuItem>& menuItems, int selectedID, bool fadeIn, int textLines)
{
    SMenuTab *currentTab = &dialogTab;

    dialogBackColor = newDialogBackColor;
    dialogTextLines = textLines;

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
            currentTab->MakeSureSelectionIsOnScreen(menu3dsGetDialogVisibleItems(), 1);
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
        if (result == MENU_CONTINUE_GAME) {
            if (isDialog) {
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTabs);
            }
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }

        return result;
    }
    return 0;
}


void menu3dsShowRomLoadingDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, const std::string& title, const std::string& text, int dialogColor, const char* romName)
{
    dialogBackColor = dialogColor;

    SMenuTab *currentTab = &dialogTab;
    currentTab->SetTitle(title);
    currentTab->DialogText.assign(text);
    if (ra3dsIsLoggedIn())
        currentTab->DialogText.append(1, UI_TEXT_SECTION_SEPARATOR).append("Looking for achievements ...");
    currentTab->MenuItems.clear();
    currentTab->FirstItemIndex = 0;
    currentTab->SelectedItemIndex = 0;

    isDialog = true;

    bool showLoadingDialogThumb = settings3DS.GameThumbnailType != Setting::ThumbnailMode::None
        && romName
        && img3dsLoadThumb(romName);

    int thumbHeight = showLoadingDialogThumb ? img3dsGetThumbHeight() : 0;
    int thumbWidth = showLoadingDialogThumb ? img3dsGetThumbWidth() : 0;
    int dialogHeight = thumbHeight > 0 ? thumbHeight : 112;

    int fadeSteps = 24;
    int loadingDialogSteps = ANIMATE_DIALOG_STEPS;

    for (int f = fadeSteps; f >= 0; f--)
    {
        if (!aptMainLoop()) break;

        if (!settings3DS.isRomLoaded) {
            float fade = (float)f / fadeSteps;
            menu3dsDrawSplash(fade);
        }

        int dialogFrame = f - fadeSteps + loadingDialogSteps;
        if (dialogFrame < 0) dialogFrame = 0;
        menu3dsDrawLoadingDialog(dialogTab, currentMenuTab, menuTabs, dialogFrame, dialogHeight, thumbWidth, loadingDialogSteps);
        menu3dsSwapBuffersAndWaitForVBlank();
    }
}

void menu3dsRunBadgeCache(SMenuTab& dialogTab, int currentMenuTab, std::vector<SMenuTab>& menuTabs, const char* romName)
{
    bool showThumb = settings3DS.GameThumbnailType != Setting::ThumbnailMode::None
        && romName && img3dsLoadThumb(romName);
    int thumbWidth = showThumb ? img3dsGetThumbWidth() : 0;
    int thumbHeight = showThumb ? img3dsGetThumbHeight() : 0;
    int dialogHeight = thumbHeight > 0 ? thumbHeight : 112;
    int loadingDialogSteps = ANIMATE_DIALOG_STEPS;

    std::string body = dialogTab.DialogText;
    size_t statusSplit = body.find(UI_TEXT_SECTION_SEPARATOR);
    std::string gameLabel = statusSplit == std::string::npos ? body : body.substr(0, statusSplit);

    auto drawProgress = [&](int pct) {
        dialogTab.DialogText.assign(gameLabel + UI_TEXT_SECTION_SEPARATOR + "Caching Badges: " + std::to_string(pct) + "%\nPress [B] to Cancel.");
        menu3dsDrawLoadingDialog(dialogTab, currentMenuTab, menuTabs,
            0, dialogHeight, thumbWidth, loadingDialogSteps);
        menu3dsSwapBuffersAndWaitForVBlank();
    };


    menu3dsRunBadgeDownload(drawProgress);
}

void menu3dsRunBadgeDownload(const std::function<void(int)>& onProgress)
{
    int total = ra3dsBeginBadgeCache();
    if (total > 0) {
        bool running = true;
        while (running) {
            if (!aptMainLoop()) break;

            hidScanInput();
            if (hidKeysDown() & KEY_B)
                break;

            int done = 0;
            running = ra3dsBadgeCachePoll(&done, &total);
            onProgress(total > 0 ? (done * 100 / total) : 100);
        }

        ra3dsEndBadgeCache();
    }

    // warm the cache so the RA page opens without a first-view fopen. Runs on
    // both the download and the already-complete (total <= 0) paths; no-op if
    // no cache exists (e.g. no achievements). Open refreshes, so a fresh download
    // is picked up without an explicit close.
    ra3dsOpenBadgeCache();
}

void menu3dsHideDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTabs, bool fadeOut)
{
    if (!isDialog) {
        return;
    }

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
            case HOTKEY_FAST_FORWARD_TOGGLE:
                hotkeysData[i][0]= "FastForwardToggle";
                hotkeysData[i][1]= "  Fast-Forward (Toggle)"; 
                hotkeysData[i][2]= "Fast-forward while this hotkey is toggled on.\nMay corrupt/freeze games on Old 3DS.";
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
                hotkeysData[i][2]= "Saves the Game to last used Save Slot";
                break;
            case HOTKEY_QUICK_LOAD: 
                hotkeysData[i][0]= "QuickLoad"; 
                hotkeysData[i][1]= "  Quick Load"; 
                hotkeysData[i][2]= "Loads the Game from last used Save Slot";
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
            case HOTKEY_FAST_FORWARD_HOLD:
                hotkeysData[i][0]= "FastForwardHold";
                hotkeysData[i][1]= "  Fast-Forward (Hold)";
                hotkeysData[i][2]= "Fast-forward while this hotkey is held down.\nMay corrupt/freeze games on Old 3DS.";
                break;
            default: 
                hotkeysData[i][0]= ""; 
                hotkeysData[i][1]= "  <empty>"; 
                hotkeysData[i][2]= ""; 
        }
    }
}

std::string menu3dsGetRomInfo() {
    char line[256];
    std::string text;
    text.reserve(256);

    snprintf(line, sizeof(line), "Cart Name: %s", Memory.ROMName);
    text += line;
    snprintf(line, sizeof(line), "\nGame Code: %s", Memory.ROMId);
    text += line;
    snprintf(line, sizeof(line), "\nContents: %s", Memory.KartContents());
    text += line;
    snprintf(line, sizeof(line), "\nMap: %s", Memory.MapType());
    text += line;
    snprintf(line, sizeof(line), "\nSpeed: 0x%02X (%s)", Memory.ROMSpeed, (Memory.ROMSpeed & 0x10) ? "FastROM" : "SlowROM");
    text += line;
    snprintf(line, sizeof(line), "\nVideo Output: %s", (Memory.ROMRegion > 12 || Memory.ROMRegion < 2) ? "NTSC 60Hz" : "PAL 50Hz");
    text += line;
    snprintf(line, sizeof(line), "\nRegion: %s", Memory.Country());
    text += line;
    snprintf(line, sizeof(line), "\nSize (header): %s", Memory.Size());
    text += line;
    snprintf(line, sizeof(line), "\nSRAM size: %s", Memory.StaticRAMSize());
    text += line;
    snprintf(line, sizeof(line), "\nCRC32: 0x%08X", Memory.ROMCRC32);
    text += line;

    return text;
}
