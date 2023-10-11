#include "snes9x.h"
#include "memmap.h"
#include <chrono>
#include <random>
#include <sstream>

#include "fast_gaussian_blur_template.h"
#include "3dsexit.h"
#include "3dssettings.h"
#include "3dsfiles.h"
#include "3dsui.h"
#include "3dsmenu.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define CONSOLE_WIDTH           40
#define MENU_HEIGHT             (14)
#define DIALOG_HEIGHT           (5)
#define ANIMATE_TAB_STEPS 3

bool                transferGameScreen = false;
int                 transferGameScreenCount = 0;
bool                swapBuffer = true;

int cheatsActive = 0;
int cheatsAll = 0;

int lastPercent = 0;
int lastSelectedTabIndex = 0;
int currentPercent = 0;

u16* tempBufSecondScreen = nullptr;
u8* tempBufGameScreen = nullptr;

std::unordered_map<std::string, int> selectedItemIndices;

MenuButton bottomMenuButtons[] = {
    {"Select", "\x0cc", 0x800d1d},
    {"Back", "\x0cd", 0x999409},
    {"Options", "\x0ce", 0x0d5280},
    {"Page \x0d1", "\x0cf", 0x0d8014}
};

void menu3dsSetCurrentPercent(int current, int total) {
    // reset state
    if (total == -1) {
        currentPercent = 0;
        lastPercent = 0;
        
        return;
    }

    // no caching required
    if (total == 0) {
        currentPercent = 100;
        
        return;
    } 
    
    currentPercent = (static_cast<float>(current) / static_cast<float>(total)) * 100;
    if (currentPercent > 100) currentPercent = 100;
}

void menu3dsSetCheatsIndicator(std::vector<SMenuItem>& cheatMenu) {
    cheatsAll = 0;
    cheatsActive = 0;

    // looking for any active cheats
    for (const SMenuItem& item : cheatMenu) {
        bool isCheatItem = item.Type == MenuItemType::Checkbox;

        if (isCheatItem) {
            cheatsAll++;

            if (item.Value == 1) {
                cheatsActive++;
            }
        }
    }

    if (cheatsAll == 0) {
        return;
    }

    std::ostringstream cheatHeadline;
    cheatHeadline << "ENABLED CHEAT CODES: " << cheatsActive << "/" << cheatsAll;
    cheatMenu[0].Text = cheatHeadline.str();
}

int menu3dsGetCurrentPercent() {
    return currentPercent;
}

int menu3dsGetLastSelectedTabIndex() {
    return lastSelectedTabIndex;
}

void menu3dsSetLastSelectedTabIndex(int index) {
    lastSelectedTabIndex = index;
}

void menu3dsSetLastSelectedIndexByTab(const std::string& tab, int menuItemIndex) {
    selectedItemIndices[tab] = menuItemIndex;
}

int menu3dsGetLastSelectedIndexByTab(const std::string& tab) {
    if (selectedItemIndices.find(tab) != selectedItemIndices.end()) {
        return selectedItemIndices[tab];
    }

    return -1;
}

void menu3dsClearLastSelectedIndicesByTab() {
    for (const auto& pair : selectedItemIndices) {
        selectedItemIndices[pair.first] = -1;
    }
}

//-------------------------------------------------------
// Sets a flag to tell the menu selector
// to transfer the emulator's rendered frame buffer
// to the actual screen's frame buffer.
//
// Usually you will set this to true during emulation,
// and set this to false when this program first runs.
//-------------------------------------------------------
void menu3dsSetTransferGameScreen(bool transfer)
{
    transferGameScreen = transfer;
    if (transfer)
        transferGameScreenCount = 2;
    else
        transferGameScreenCount = 0;

}

// Draw a black screen.
//
void menu3dsDrawBlackScreen(float opacity)
{
    ui3dsDrawRect(0, 0, screenSettings.SecondScreenWidth, SCREEN_HEIGHT, 0x000000, opacity);    
}

void menu3dsDrawPauseScreen() 
{
    u8* fb = (u8*)gfxGetFramebuffer(screenSettings.GameScreen, GFX_LEFT, NULL, NULL);
    int x0 = 0;
    int y0 = 0;
    int x1 = screenSettings.GameScreenWidth;
    int y1 = SCREEN_HEIGHT;

    int width = x1 - x0;
    int height = y1 - y0;
    int channels = 3;
    int bufferSize = width * height * channels;

    // store current game screen (neccessary when taking an ingame screenshot)
    if (tempBufGameScreen == nullptr) {
        tempBufGameScreen = (u8*)linearAlloc(bufferSize);
        memset(tempBufGameScreen, 0, bufferSize);
        memcpy(tempBufGameScreen, fb, bufferSize);
    }

    u8* tempbuf_old = (u8*)linearAlloc(bufferSize);
    u8* tempbuf_new = (u8*)linearAlloc(bufferSize);
    memset(tempbuf_old, 0, bufferSize);
    memset(tempbuf_new, 0, bufferSize);

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            int si = (((SCREEN_HEIGHT - 1 - y) + (x  * SCREEN_HEIGHT)) * 4);
            int di =((x - x0) + (y - y0) * width) * channels;

            tempbuf_old[di] = fb[si + 3];
            tempbuf_old[di + 1] = fb[si + 2];
            tempbuf_old[di + 2] = fb[si + 1];
        }
        
    // note: both old and new buffer are modified
    fast_gaussian_blur(tempbuf_old, tempbuf_new, width, height, 3, 5, 3, kKernelCrop);


    int bHeight = 50;
    int by0 = SCREEN_HEIGHT / 2 - bHeight / 2;
    int by1 = by0 + bHeight;
    float opacity = 0.7f;

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            int si = (((SCREEN_HEIGHT - 1 - y) + (x  * SCREEN_HEIGHT)) * 4);
            int di =((x - x0) + (y - y0) * width) * 3;

            if (y >= by0 && y < by1) {
                fb[si + 3] = static_cast<unsigned int>(tempbuf_new[di] * (1.0 - opacity));
                fb[si + 2] = static_cast<unsigned int>(tempbuf_new[di + 1] * (1.0 - opacity));
                fb[si + 1] = static_cast<unsigned int>(tempbuf_new[di + 2] * (1.0 - opacity));
            } else {
                fb[si + 1] = tempbuf_new[di + 2];
                fb[si + 2] = tempbuf_new[di + 1];
                fb[si + 3] = tempbuf_new[di];
            }
        }
    
    linearFree(tempbuf_old);
    linearFree(tempbuf_new);

    int textWidth = 150;
    int textCx = screenSettings.GameScreenWidth / 2 - textWidth / 2;
    int textCy = SCREEN_HEIGHT / 2 - 8;
    int shadowColor = 0x111111;

    ui3dsDrawStringWithNoWrapping(screenSettings.GameScreen, textCx + 1, textCy + 1, textCx + textWidth + 1, textCy + 7 + 1, shadowColor, HALIGN_LEFT, 
        "\x13\x14\x15\x16\x16 \x0e\x0f\x10\x11\x12 \x17\x18 \x14\x15\x16\x19\x1a\x15");
    ui3dsDrawStringWithNoWrapping(screenSettings.GameScreen, textCx, textCy, textCx + textWidth, textCy + 7, 0xffffff, HALIGN_LEFT, 
        "\x13\x14\x15\x16\x16 \x0e\x0f\x10\x11\x12 \x17\x18 \x14\x15\x16\x19\x1a\x15");

    gfxScreenSwapBuffers(screenSettings.GameScreen, false);
}

void menu3dsClearPauseScreen() {
    if (tempBufGameScreen != nullptr) {
        linearFree(tempBufGameScreen);
        tempBufGameScreen = nullptr;
    }
}

void menu3dsSwapBuffersAndWaitForVBlank()
{
    if (transferGameScreenCount)
    {
        gpu3dsTransferToScreenBuffer(screenSettings.GameScreen);
        transferGameScreenCount --;
    }
    if (swapBuffer)
    {
        gfxFlushBuffers(); // when missing, menu scrolling doesn't look good
        gfxScreenSwapBuffers(screenSettings.SecondScreen, false);
    }

    gspWaitForVBlank();
    swapBuffer = false;
}

bool menu3dsGaugeIsDisabled(SMenuTab *currentTab, int index)
{
    SMenuItem& item = currentTab->MenuItems[index];
    return item.GaugeMaxValue == GAUGE_DISABLED_VALUE;
}

bool menu3dsHasHighlightableItems(SMenuTab *currentTab) {
    bool hasSelectableItems = false;

    for (int i = 0; i < currentTab->MenuItems.size(); i++) {
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
    int gi;
    for (int i = 0; i < currentTab->MenuItems.size(); i++)
    {
        // assumption: gauge item follows related menu item
        // (e.g. SecondScreenOpacity gauge follows SecondScreenContent picker)
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
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, menuStartY, screenSettings.SecondScreenWidth - horizontalPadding, menuStartY + fontHeight, 
            subtitleTextColor, HALIGN_LEFT, currentTab->SubTitle.c_str());
        ui3dsDrawRect(horizontalPadding, menuStartY + fontHeight - 1, screenSettings.SecondScreenWidth - horizontalPadding, menuStartY + fontHeight, subtitleTextColor);
        menuStartY += fontHeight;
    }

    int line = 0;
    int color = Themes[settings3DS.Theme].selectedTabTextColor;

    // Draw all the individual items
    //
    for (int i = currentTab->FirstItemIndex;
        i < currentTab->MenuItems.size() && i < currentTab->FirstItemIndex + maxItems; i++)
    {
        int y = line * fontHeight + menuStartY;

        // Draw the selected background 
        //
        if (currentTab->SelectedItemIndex == i)
        {
            if (selectedItemBackColor != -1) {
                ui3dsDrawRect(0, y, screenSettings.SecondScreenWidth, y + 14, selectedItemBackColor);
            }

            if (settings3DS.Theme == THEME_RETROARCH && currentTab->MenuItems[i].IsHighlightable()) {
                int xi = horizontalPadding - offsetX;
                ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, xi, y, xi + 10, y + 14, selectedItemTextColor, HALIGN_LEFT, ">");
            }
        }
        
        if (currentTab->MenuItems[i].Type == MenuItemType::Header1)
        {
            color = headerItemTextColor;
            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            ui3dsDrawRect(horizontalPadding, y + fontHeight - 1, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color);
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Header2)
        {
            color = headerItemTextColor;
            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Textarea)
        {
            color = normalItemDescriptionTextColor;
            int maxLines = 15; // TODO: set value based on content
            ui3dsDrawStringWithWrapping(screenSettings.SecondScreen, horizontalPadding, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight * maxLines, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Disabled)
        {
            color = disabledItemTextColor;
            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Action)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;
            
            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

            color = normalItemDescriptionTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemDescriptionTextColor;
            if (!currentTab->MenuItems[i].Description.empty())
            {
                ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Description.c_str());
            }
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Checkbox)
        {
            color = currentTab->MenuItems[i].Value == 0 ? disabledItemTextColor : normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;
               
            
            int checkboxOffsetX = screenSettings.SecondScreenWidth - horizontalPadding - 20;
            int descriptionOffsetX = checkboxOffsetX;
            if (!currentTab->MenuItems[i].Description.empty()) {
                descriptionOffsetX = checkboxOffsetX - currentTab->MenuItems[i].Description.size() * 8;
                ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, descriptionOffsetX, y, checkboxOffsetX, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Description.c_str());
            }

            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, y, descriptionOffsetX, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, checkboxOffsetX, y, checkboxOffsetX + 20, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Value == 1 ? "\xfd" : "\xfe");    
        }
        
        else if (currentTab->MenuItems[i].Type == MenuItemType::Radio)
        {
            radio_state val = static_cast<radio_state>(currentTab->MenuItems[i].Value);

            color = (val == RADIO_INACTIVE || val == RADIO_INACTIVE_CHECKED) ? disabledItemTextColor : normalItemTextColor;
            bool isSelected = val == RADIO_ACTIVE_CHECKED || val == RADIO_INACTIVE_CHECKED;
            if (currentTab->SelectedItemIndex == i) {
                color = selectedItemTextColor;
            }
            
            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, screenSettings.SecondScreenWidth - 100 + 60, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, isSelected ? "\xfd" : "\xfe");
        }

        else if (currentTab->MenuItems[i].Type == MenuItemType::Gauge)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;
            
            if (menu3dsGaugeIsDisabled(currentTab, i)) {
                color = disabledItemTextColor;
            }

            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

            const int max = 40;
            int diff = currentTab->MenuItems[i].GaugeMaxValue - currentTab->MenuItems[i].GaugeMinValue;
            int pos = (currentTab->MenuItems[i].Value - currentTab->MenuItems[i].GaugeMinValue) * (max - 1) / diff;

            char gauge[max+1];
            for (int j = 0; j < max; j++)
            if (j == pos) {
                gauge[j] = settings3DS.Theme == THEME_ORIGINAL ? '\xfa' :  '\xfc';
            } else {
                gauge[j] = '\xfb';
            }

            gauge[max] = 0;
            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, 245, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, gauge);            
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Picker)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;

            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding, y, 160, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

            if (!currentTab->MenuItems[i].PickerItems.empty() && currentTab->MenuItems[i].GaugeMinValue)
            {
                int selectedIndex = -1;
                for (int j = 0; j < currentTab->MenuItems[i].PickerItems.size(); j++)
                {
                    std::vector<SMenuItem>& pickerItems = currentTab->MenuItems[i].PickerItems;
                    if (pickerItems[j].Value == currentTab->MenuItems[i].Value)
                    {
                        selectedIndex = j;
                    }
                }
                if (selectedIndex > -1)
                {
                    ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, 160, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].PickerItems[selectedIndex].Text.c_str());
                }
            }
        }

        line ++;
    }


    // Draw the "up arrow" to indicate more options available at top
    //
    if (settings3DS.Theme == THEME_ORIGINAL && currentTab->FirstItemIndex != 0)
    {
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, screenSettings.SecondScreenWidth - horizontalPadding, menuStartY, screenSettings.SecondScreenWidth, menuStartY + fontHeight, disabledItemTextColor, HALIGN_CENTER, "\xf8");
    }

    // Draw the "down arrow" to indicate more options available at bottom
    //
    if (settings3DS.Theme == THEME_ORIGINAL && currentTab->FirstItemIndex + maxItems < currentTab->MenuItems.size())
    {
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, screenSettings.SecondScreenWidth - horizontalPadding, menuStartY + (maxItems - 1) * fontHeight, screenSettings.SecondScreenWidth, menuStartY + maxItems * fontHeight, disabledItemTextColor, HALIGN_CENTER, "\xf9");
    }
    
}

// Display the list of choices for selection
//
void menu3dsDrawMenu(std::vector<SMenuTab>& menuTab, int& currentMenuTab, int menuItemFrame, int translateY)
{
    SMenuTab *currentTab = &menuTab[currentMenuTab];

    // Draw the background
    if (settings3DS.Theme != THEME_RETROARCH) {
        ui3dsDrawRect(0, 0, screenSettings.SecondScreenWidth, 24, Themes[settings3DS.Theme].menuTopBarColor);
        ui3dsDrawRect(0, 24, screenSettings.SecondScreenWidth, 220, Themes[settings3DS.Theme].menuBackColor);
        ui3dsDrawRect(0, 220, screenSettings.SecondScreenWidth, SCREEN_HEIGHT, Themes[settings3DS.Theme].menuBottomBarColor);
    } else {
        // draw checkerboard background for retroarch theme
        int cb1 = Themes[settings3DS.Theme].menuBackColor;
        int cb2 = ui3dsOverlayBlendColor(cb1, 0xededed); 
        ui3dsDrawCheckerboard(0, 0, screenSettings.SecondScreenWidth, SCREEN_HEIGHT, cb1, cb2);

        // draw frame
        int cwidth = 4;
        int cx0 = 8;
        int cy0 = 20;
        int cx1 = screenSettings.SecondScreenWidth - cx0;
        int cy1 = 222;

        int cf1 = ui3dsOverlayBlendColor(cb1, Themes[settings3DS.Theme].accentColor);
        int cf2 = ui3dsOverlayBlendColor(cb2, Themes[settings3DS.Theme].accentColor);

        // horizontal
        ui3dsDrawCheckerboard(cx0, cy0, cx1, cy0 + cwidth, cf1, cf2);
        ui3dsDrawCheckerboard(cx0, cy1 - cwidth, cx1, cy1, cf1, cf2);
        
        // vertical
        ui3dsDrawCheckerboard(cx0, cy0 + cwidth, cx0 + cwidth, cy1 - cwidth, cf1, cf2);
        ui3dsDrawCheckerboard(cx1 - cwidth, cy0 + cwidth, cx1, cy1 - cwidth, cf1, cf2);
    }

    // Draw the tabs at the top
    //
    for (int i = 0; i < static_cast<int>(menuTab.size()); i++)
    {
        int color = i == currentMenuTab ?  Themes[settings3DS.Theme].selectedTabTextColor :  Themes[settings3DS.Theme].tabTextColor;
        int accentColor = i == currentMenuTab ? Themes[settings3DS.Theme].accentColor : Themes[settings3DS.Theme].accentUnselectedColor;

        int offsetLeft = 10;
        int offsetRight = 10;

        int availableSpace = screenSettings.SecondScreenWidth - ( offsetLeft + offsetRight );
        int pixelPerOption =      availableSpace / static_cast<int>(menuTab.size());
        int extraPixelOnOptions = availableSpace % static_cast<int>(menuTab.size());

        // each tab gains an equal amount of horizontal space
        // if space is not cleanly divisible by tab count, the earlier tabs gain one extra pixel each until we reach the requested space
        int xLeft =  (     i     * pixelPerOption ) + offsetLeft + std::min( i,     extraPixelOnOptions );
        int xRight = ( ( i + 1 ) * pixelPerOption ) + offsetLeft + std::min( i + 1, extraPixelOnOptions );
        int yTextTop = 6;
        int yCurrentTabBoxTop = 21;
        int yCurrentTabBoxBottom = 24;

        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, xLeft, yTextTop, xRight, yCurrentTabBoxTop, color, HALIGN_CENTER, menuTab[i].Title.c_str());

        if (i == currentMenuTab && Themes[settings3DS.Theme].selectedTabIndicatorColor != -1) {
            ui3dsDrawRect(xLeft, yCurrentTabBoxTop, xRight, yCurrentTabBoxBottom, Themes[settings3DS.Theme].selectedTabIndicatorColor);
        }

        // draw indicator when game has (active) cheats
        if (menuTab[i].Title == "Cheats" && cheatsAll > 0) {
            int xOffset = screenSettings.SecondScreen == GFX_TOP ? 19 : 14;
            ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, xRight - xOffset, yTextTop - 3, xRight, yCurrentTabBoxTop, cheatsActive > 0 ? accentColor : color, HALIGN_LEFT, "\x95");        
        }
    }

    //battery display
    const int maxBatteryLevel = 5;
    const int battLevelWidth = 3;
    const int battFullLevelWidth = (maxBatteryLevel) * battLevelWidth + 1;
    const int battBorderWidth = 1;
    const int battY1 = 227;
    const int battY2 = 234;
    const int battX2 = screenSettings.SecondScreenWidth - 10;
    const int battYHeight = battY2 - battY1;
    const int battHeadWidth = 2;
    const int battHeadSpacing = 1;

    // battery positive end
    ui3dsDrawRect(
        battX2 - battFullLevelWidth - battBorderWidth - battHeadWidth, 
        battY1 + battHeadSpacing, 
        battX2 - battFullLevelWidth - battBorderWidth, 
        battY2 - battHeadSpacing, 
        Themes[settings3DS.Theme].selectedTabTextColor, 1.0f);
    // battery body
    ui3dsDrawRect(
        battX2 - battFullLevelWidth - battBorderWidth, 
        battY1 - battBorderWidth, 
        battX2 + battBorderWidth, 
        battY2 + battBorderWidth, 
        Themes[settings3DS.Theme].selectedTabTextColor, 1.0f);
    // battery's empty insides
    ui3dsDrawRect(
        battX2 - battFullLevelWidth, 
        battY1, 
        battX2, 
        battY2, 
        Themes[settings3DS.Theme].menuBottomBarColor, 1.0f);
        
    ptmuInit();
    
    u8 batteryChargeState = 0;
    u8 batteryLevel = 0;
    if(R_SUCCEEDED(PTMU_GetBatteryChargeState(&batteryChargeState)) && batteryChargeState) {
        ui3dsDrawRect(
            battX2-battFullLevelWidth + 1, battY1 + 1, 
            battX2 - 1, battY2 - 1, Themes[settings3DS.Theme].accentColor, 1.0f);
    } else if(R_SUCCEEDED(PTMU_GetBatteryLevel(&batteryLevel))) {
        if (batteryLevel > 5)
            batteryLevel = 5;
        for (int i = 0; i < batteryLevel; i++)
        {
            ui3dsDrawRect(
                battX2-battLevelWidth*(i+1), battY1 + 1, 
                battX2-battLevelWidth*(i) - 1, battY2 - 1, Themes[settings3DS.Theme].accentColor, 1.0f);
        }
    }
 
    ptmuExit();
    
    bool romLoaded = menuTab.size() > 2;
    int buttonRightMargin = 5;
    int buttonLeftMargin = 10;
    int bottomMenuPosX = 10;
    int buttonColor = settings3DS.Theme == THEME_ORIGINAL ? 0x529eeb : 0x555555;

    for (const auto& button : bottomMenuButtons) {
        if (settings3DS.Theme == THEME_DARK_MODE) {
            // multi color buttons for dark mode theme
            buttonColor = button.color;
        }
        
        if ((button.label != "Options" && button.label != "Page \x0d1") || currentTab->Title == "Load Game") {
            ui3dsDrawRect(bottomMenuPosX + 2, SCREEN_HEIGHT - 13, bottomMenuPosX + 9, SCREEN_HEIGHT - 6,0xffffff);
            bottomMenuPosX = ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, bottomMenuPosX, SCREEN_HEIGHT - 16, bottomMenuPosX + 12, SCREEN_HEIGHT, buttonColor, HALIGN_LEFT,  button.icon) + buttonRightMargin;
            bottomMenuPosX = ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, bottomMenuPosX, SCREEN_HEIGHT - 17, bottomMenuPosX + 100, SCREEN_HEIGHT, Themes[settings3DS.Theme].menuBottomBarTextColor, HALIGN_LEFT, button.label) + buttonLeftMargin;
        }
    }

    const int rightEdge = battX2 - battFullLevelWidth - battBorderWidth - 6;
    ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, 97, SCREEN_HEIGHT - 17, rightEdge, SCREEN_HEIGHT, Themes[settings3DS.Theme].menuBottomBarTextColor, HALIGN_RIGHT, getAppVersion("v"));
    
    int line = 0;
    int maxItems = MENU_HEIGHT;
    int menuStartY = 29;

    int menuBackColor = Themes[settings3DS.Theme].menuBackColor;
    int selectedItemBackColor = menu3dsHasHighlightableItems(currentTab) ? Themes[settings3DS.Theme].selectedItemBackColor : -1;
    
    ui3dsSetTranslate(menuItemFrame * 3, translateY);

    if (menuItemFrame == 0)
    {
        menu3dsDrawItems(
            currentTab, 20, menuStartY, maxItems,
            selectedItemBackColor,
            Themes[settings3DS.Theme].selectedItemTextColor,
            Themes[settings3DS.Theme].selectedItemDescriptionTextColor,
            Themes[settings3DS.Theme].normalItemTextColor,
            Themes[settings3DS.Theme].normalItemDescriptionTextColor,
            Themes[settings3DS.Theme].disabledItemTextColor,
            Themes[settings3DS.Theme].headerItemTextColor,
            Themes[settings3DS.Theme].subtitleTextColor);


        menu3dsSetLastSelectedIndexByTab(currentTab->Title, currentTab->SelectedItemIndex);

        if (currentTab->Title != "Load Game") {
            return;
        }

        // looking for available game thumbnail
        std::string filename = currentTab->MenuItems[currentTab->SelectedItemIndex].Text;
        size_t offs = filename.find_first_not_of(' ');
        filename.assign(offs != filename.npos ? filename.substr(offs) : filename);
        StoredFile file = file3dsGetStoredFileById(filename);

        if (!file.Buffer.empty()) {
           ui3dsRenderImage(screenSettings.SecondScreen, file.Filename.c_str(), file.Buffer.data(), file.Buffer.size(), IMAGE_TYPE::PREVIEW);
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
            ui3dsApplyAlphaToColor(Themes[settings3DS.Theme].selectedItemTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[settings3DS.Theme].selectedItemDescriptionTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[settings3DS.Theme].normalItemTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[settings3DS.Theme].normalItemDescriptionTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[settings3DS.Theme].disabledItemTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[settings3DS.Theme].headerItemTextColor, alpha) + menuBackColorAlpha,
            ui3dsApplyAlphaToColor(Themes[settings3DS.Theme].subtitleTextColor, alpha) + menuBackColorAlpha);
    } 
}


int dialogBackColor = 0x000000;

void menu3dsDrawDialog(SMenuTab& dialogTab)
{
    int dialogTextColor = 0xffffff;
    int selectedItemBackColor = 0x000000;
    int dialogSelectedItemTextColor = Themes[settings3DS.Theme].selectedItemTextColor;
    int offsetX = settings3DS.Theme == THEME_RETROARCH ? 6 : 0;
    int horizontalPadding = 32;
    int topHeight = 76;
    int bottomHeight = 84;
    
    int dialogBackColorBottom = settings3DS.Theme == THEME_ORIGINAL ? dialogBackColor : Themes[settings3DS.Theme].menuBackColor;
    int dialogBackColorTop = settings3DS.Theme == THEME_ORIGINAL ? ui3dsApplyAlphaToColor(dialogBackColorBottom, 0.9f) : ui3dsOverlayBlendColor(dialogBackColorBottom, 0xaaaaaa);
    ui3dsDrawRect(0, 0, screenSettings.SecondScreenWidth, topHeight, dialogBackColorTop);
    ui3dsDrawRect(0, topHeight, screenSettings.SecondScreenWidth, topHeight + bottomHeight, dialogBackColorBottom);

    int dialogTitleTextColor = 
        ui3dsApplyAlphaToColor(dialogBackColorTop, 1.0f - Themes[settings3DS.Theme].dialogTextAlpha) + 
        ui3dsApplyAlphaToColor(dialogTextColor, Themes[settings3DS.Theme].dialogTextAlpha);
    
    int dialogItemDescriptionTextColor = 
        ui3dsApplyAlphaToColor(dialogBackColorBottom, 1.0f - Themes[settings3DS.Theme].dialogTextAlpha) + 
        ui3dsApplyAlphaToColor(dialogTextColor, Themes[settings3DS.Theme].dialogTextAlpha);

    int dialogSelectedItemBackColor;

    if (settings3DS.Theme == THEME_DARK_MODE) {    
        ui3dsDrawRect(0, topHeight - 2, screenSettings.SecondScreenWidth, topHeight, dialogBackColor);
        ui3dsDrawRect(0, topHeight, screenSettings.SecondScreenWidth, topHeight + 2, dialogBackColor);
        dialogSelectedItemBackColor = Themes[settings3DS.Theme].selectedItemBackColor;
    }
    else if (settings3DS.Theme == THEME_RETROARCH) {   
        int cb1 = ui3dsOverlayBlendColor(dialogBackColorTop, dialogBackColor);
        int cb2 = ui3dsOverlayBlendColor(dialogBackColorBottom, dialogBackColor);
        int cb3 = ui3dsOverlayBlendColor(ui3dsApplyAlphaToColor(dialogBackColorBottom, 0.85f), dialogBackColor);
        ui3dsDrawCheckerboard(0, topHeight - 2, screenSettings.SecondScreenWidth, topHeight, cb1, cb3);
        ui3dsDrawCheckerboard(0, topHeight, screenSettings.SecondScreenWidth, topHeight + 2, cb1, cb3);
        dialogSelectedItemBackColor = -1;
    } else {
        dialogSelectedItemBackColor = Themes[settings3DS.Theme].selectedItemBackColor == -1 ? -1 :
        ui3dsApplyAlphaToColor(dialogBackColorBottom, 1.0f - Themes[settings3DS.Theme].dialogSelectedItemBackAlpha) + 
        ui3dsApplyAlphaToColor(selectedItemBackColor, Themes[settings3DS.Theme].dialogSelectedItemBackAlpha);
    }

    ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, horizontalPadding - offsetX, 10, screenSettings.SecondScreenWidth - horizontalPadding, 25, dialogTitleTextColor, HALIGN_LEFT, dialogTab.Title.c_str());
    ui3dsDrawStringWithWrapping(screenSettings.SecondScreen, horizontalPadding - offsetX, 30, screenSettings.SecondScreenWidth - horizontalPadding, 70, dialogTextColor, HALIGN_LEFT, dialogTab.DialogText.c_str());

    int menuStartY = settings3DS.Theme == THEME_RETROARCH ? bottomHeight + 1 : bottomHeight + 3;
    menu3dsDrawItems(
        &dialogTab, horizontalPadding, menuStartY, DIALOG_HEIGHT,
        dialogSelectedItemBackColor,
        Themes[settings3DS.Theme].selectedItemTextColor,
        dialogItemDescriptionTextColor,
        dialogTextColor,
        dialogItemDescriptionTextColor,
        dialogItemDescriptionTextColor,
        dialogTextColor,
        dialogTextColor,
        offsetX);
}


void menu3dsDrawEverything(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab, int menuFrame, int menuItemsFrame, int dialogFrame)
{
    if (!isDialog)
    {
        int y = 0 + menuFrame * menuFrame * 120 / 32;

        ui3dsSetViewport(0, 0, screenSettings.SecondScreenWidth, SCREEN_HEIGHT);
        ui3dsSetTranslate(0, 0);
        ui3dsDrawRect(0, 0, screenSettings.SecondScreenWidth, y, 0x000000);
        ui3dsSetTranslate(0, y);
        menu3dsDrawMenu(menuTab, currentMenuTab, menuItemsFrame, y);
    }
    else
    {
        int y = 80 + dialogFrame * dialogFrame * 80 / 32;

        ui3dsSetViewport(0, 0, screenSettings.SecondScreenWidth, y);
        //ui3dsBlitToFrameBuffer(savedBuffer, 1.0f - (float)(8 - dialogFrame) / 10);
        ui3dsSetTranslate(0, 0);
        menu3dsDrawMenu(menuTab, currentMenuTab, 0, 0);
        ui3dsDrawRect(0, 0, screenSettings.SecondScreenWidth, y, 0x000000, (float)(8 - dialogFrame) / 10);

        ui3dsSetViewport(0, 0, screenSettings.SecondScreenWidth, SCREEN_HEIGHT);
        ui3dsSetTranslate(0, y);
        menu3dsDrawDialog(dialogTab);
        ui3dsSetTranslate(0, 0);
    }
    swapBuffer = true;

}

void menu3dsDrawThumbnailCacheStatus(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab) {
    if (currentPercent == 0) {
        return;
    }

    if (currentPercent == 100) {
        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
        lastPercent = currentPercent;

        return;
    }

    char s[64];
    sprintf(s, "caching thumbnails -> %d%%", currentPercent);

    if (currentPercent > lastPercent + 5) {
        lastPercent = currentPercent;
        menu3dsDrawMenu(menuTab, currentMenuTab, 0, 0);
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, screenSettings.SecondScreenWidth - 130, 29, screenSettings.SecondScreenWidth - 16, 29 + FONT_HEIGHT, 
        Themes[settings3DS.Theme].normalItemDescriptionTextColor, HALIGN_LEFT, s);
        swapBuffer = true;
    } else {
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, screenSettings.SecondScreenWidth - 130, 29, screenSettings.SecondScreenWidth - 16, 29 + FONT_HEIGHT, 
        Themes[settings3DS.Theme].normalItemDescriptionTextColor, HALIGN_LEFT, s);
    }
}


SMenuTab *menu3dsAnimateTab(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab, int direction)
{
    SMenuTab *currentTab = &menuTab[currentMenuTab];

    if (direction < 0)
    {
        for (int i = 1; i <= ANIMATE_TAB_STEPS; i++)
        {
            aptMainLoop();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab, 0, i, 0);
            menu3dsSwapBuffersAndWaitForVBlank();
        }

        currentMenuTab--;
        if (currentMenuTab < 0)
            currentMenuTab = static_cast<int>(menuTab.size() - 1);
        currentTab = &menuTab[currentMenuTab];
        
        for (int i = -ANIMATE_TAB_STEPS; i <= 0; i++)
        {
            aptMainLoop();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab, 0, i, 0);
            menu3dsSwapBuffersAndWaitForVBlank();
        }
    }
    else if (direction > 0)
    {
        for (int i = -1; i >= -ANIMATE_TAB_STEPS; i--)
        {
            aptMainLoop();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab, 0, i, 0);
            menu3dsSwapBuffersAndWaitForVBlank();
        }

        currentMenuTab++;
        if (currentMenuTab >= static_cast<int>(menuTab.size()))
            currentMenuTab = 0;
        currentTab = &menuTab[currentMenuTab];
        
        for (int i = ANIMATE_TAB_STEPS; i >= 0; i--)
        {
            aptMainLoop();
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab, 0, i, 0);
            menu3dsSwapBuffersAndWaitForVBlank();
        }
    }
    return currentTab;
}


int getRandomInt(int min, int max) {
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> distribution(min, max);
    
    return distribution(generator);
}


static u32 lastKeysHeld = 0xffffff;
static u32 thisKeysHeld = 0;

// Displays the menu and allows the user to select from
// a list of choices.
//
int menu3dsMenuSelectItem(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab)
{
    int framesDKeyHeld = 0;
    int returnResult = -1;
    char menuTextBuffer[512];

    SMenuTab *currentTab = &menuTab[currentMenuTab];

    if (isDialog)
        currentTab = &dialogTab;

    for (int i = 0; i < 2; i ++)
    {
        aptMainLoop();
        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
        menu3dsSwapBuffersAndWaitForVBlank();

        hidScanInput();
        lastKeysHeld = hidKeysHeld();
    }

    if (currentTab->Title == "Load Game") {
        swapBuffer = true;
        menu3dsSetCurrentPercent(0, -1);
    }

    while (aptMainLoop())
    {   
        if (GPU3DS.emulatorState == EMUSTATE_END)
        {
            returnResult = -1;
            break;
        }

        if(!settings3DS.Disable3DSlider)
        {
            gfxSet3D(true);
            gpu3dsCheckSlider();
        }
        else
            gfxSet3D(false);

        hidScanInput();
        thisKeysHeld = hidKeysHeld();

        u32 keysDown = (~lastKeysHeld) & thisKeysHeld;
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

        // close pause menu on start button
        if (keysDown & KEY_START)
        {
            returnResult = -1;
            break;
        }

        if (keysDown & KEY_B)
        {
            if (isDialog) {
                returnResult = -1;
            }
            else if (currentTab->MenuItems[0].Text == PARENT_DIRECTORY_LABEL) { 
                // if current tab has parent directory, navigate to parent directory
                currentTab->MenuItems[0].SetValue(1);
                returnResult = currentTab->MenuItems[0].Value;
            }
            else {
                // scroll to top
                int lastSelectedItemIndex = currentTab->SelectedItemIndex;
                for (int i = 0; i < currentTab->MenuItems.size(); i++) {
                    if (currentTab->MenuItems[i].IsHighlightable()) {
                        currentTab->SelectedItemIndex = i;
                        currentTab->MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);

                        break;
                    }
                }

                if (lastSelectedItemIndex == currentTab->SelectedItemIndex && currentTab->Title != "Emulator") {
                    currentTab = menu3dsAnimateTab(dialogTab, isDialog, currentMenuTab, menuTab, -1);
                } 
                
                returnResult = 0;  
            }

            break; 
        }
        if (keysDown & KEY_X && currentTab->Title == "Load Game")
        {
            returnResult = FILE_MENU_SHOW_OPTIONS;
            break;
        }
        
        if ((keysDown & KEY_RIGHT) || (keysDown & KEY_R) || ((thisKeysHeld & KEY_RIGHT) && (framesDKeyHeld > 15) && (framesDKeyHeld % 2 == 0)))
        {
            if (!isDialog)
            {
                if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Gauge)
                {
                    if (keysDown & KEY_RIGHT || ((thisKeysHeld & KEY_RIGHT) && (framesDKeyHeld > 15) && (framesDKeyHeld % 2 == 0)))
                    {
                        if (currentTab->MenuItems[currentTab->SelectedItemIndex].Value <
                            currentTab->MenuItems[currentTab->SelectedItemIndex].GaugeMaxValue)
                        {
                            currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(currentTab->MenuItems[currentTab->SelectedItemIndex].Value + 1);
                        }
                        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
                    }
                }
                else
                {
                    currentTab = menu3dsAnimateTab(dialogTab, isDialog, currentMenuTab, menuTab, +1);
                }
            }
        }
        if ((keysDown & KEY_LEFT) || (keysDown & KEY_L)|| ((thisKeysHeld & KEY_LEFT) && (framesDKeyHeld > 15) && (framesDKeyHeld % 2 == 0)))
        {
            if (!isDialog)
            {
                if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Gauge)
                {
                    if (keysDown & KEY_LEFT || ((thisKeysHeld & KEY_LEFT) && (framesDKeyHeld > 15) && (framesDKeyHeld % 2 == 0)))
                    {
                        // Gauge adjustment
                        if (currentTab->MenuItems[currentTab->SelectedItemIndex].Value >
                            currentTab->MenuItems[currentTab->SelectedItemIndex].GaugeMinValue)
                        {
                            currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(currentTab->MenuItems[currentTab->SelectedItemIndex].Value - 1);
                        }
                        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
                    }
                }
                else
                {
                    currentTab = menu3dsAnimateTab(dialogTab, isDialog, currentMenuTab, menuTab, -1);
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
                    for (int i = 0; i < currentTab->MenuItems.size(); i++)
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
                menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
            }
            if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Checkbox)
            {
                if (currentTab->MenuItems[currentTab->SelectedItemIndex].Value == 0)
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(1);
                else
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(0);

                if (currentTab->Title == "Cheats") {
                    menu3dsSetCheatsIndicator(currentTab->MenuItems);
                }    

                menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
            }
            if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Picker)
            {
                int pickerDialogBackground;

                switch (currentTab->MenuItems[currentTab->SelectedItemIndex].PickerDialogType) {
                    case DIALOG_TYPE_SUCCESS:
                        pickerDialogBackground = Themes[settings3DS.Theme].dialogColorSuccess;
                        break;
                    case DIALOG_TYPE_WARN:
                        pickerDialogBackground = Themes[settings3DS.Theme].dialogColorWarn;
                        break;
                    default:
                        pickerDialogBackground = Themes[settings3DS.Theme].dialogColorInfo;
                        break;
                }

                snprintf(menuTextBuffer, 511, "%s", currentTab->MenuItems[currentTab->SelectedItemIndex].Text.c_str());
                int lastValue = currentTab->MenuItems[currentTab->SelectedItemIndex].Value;
                int resultValue = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, menuTextBuffer,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].PickerDescription,
                    pickerDialogBackground,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].PickerItems,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].Value
                    );

                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

                if (resultValue != -1)
                {
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(resultValue);
                }

                menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);

                // when game screen has been swapped we want to exit the menu loop to close the menu 
                // TODO: provide keys for text labels (game screen menu item shouldn't be detected by text label value)
                bool closeMenu = resultValue != -1 && resultValue != lastValue && currentTab->MenuItems[currentTab->SelectedItemIndex].Text == "  Game Screen";
                
                if (closeMenu) {
                    returnResult = -1;
                    break;
                }        
            }
        }
        if (keysDown & KEY_UP || ((thisKeysHeld & KEY_UP) && (framesDKeyHeld > 15) && (framesDKeyHeld % 2 == 0)))
        {
            int moveCursorTimes = 0;

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
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Gauge && menu3dsGaugeIsDisabled(currentTab, currentTab->SelectedItemIndex)
                ) &&
                moveCursorTimes < currentTab->MenuItems.size());

            currentTab->MakeSureSelectionIsOnScreen(maxItems, isDialog ? 1 : 2);
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);

        }
        if (keysDown & KEY_DOWN || ((thisKeysHeld & KEY_DOWN) && (framesDKeyHeld > 15) && (framesDKeyHeld % 2 == 0)))
        {
            int moveCursorTimes = 0;
            do
            {
                if (thisKeysHeld & KEY_Y)
                {
                    currentTab->SelectedItemIndex += 13;
                    if (currentTab->SelectedItemIndex >= currentTab->MenuItems.size())
                        currentTab->SelectedItemIndex = currentTab->MenuItems.size() - 1;
                }
                else
                {
                    currentTab->SelectedItemIndex++;
                    if (currentTab->SelectedItemIndex >= currentTab->MenuItems.size())
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
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Gauge && menu3dsGaugeIsDisabled(currentTab, currentTab->SelectedItemIndex)
                ) &&
                moveCursorTimes < currentTab->MenuItems.size());

            currentTab->MakeSureSelectionIsOnScreen(maxItems, isDialog ? 1 : 2);
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
        }

        if (lastPercent < 100 && !isDialog && currentTab->Title == "Load Game") {    
            menu3dsDrawThumbnailCacheStatus(dialogTab, isDialog, currentMenuTab, menuTab);
        }

        menu3dsSwapBuffersAndWaitForVBlank();
    }

    menu3dsSetLastSelectedTabIndex(currentMenuTab);

    return returnResult;
}



void menu3dsAddTab(std::vector<SMenuTab>& menuTab, char *title, const std::vector<SMenuItem>& menuItems)
{
    menuTab.emplace_back();
    SMenuTab *currentTab = &menuTab.back();

    currentTab->SetTitle(title);
    currentTab->MenuItems = menuItems;

    currentTab->FirstItemIndex = 0;
    currentTab->SelectedItemIndex = 0;
    for (int i = 0; i < currentTab->MenuItems.size(); i++)
    {
        if (menuItems[i].IsHighlightable())
        {
            currentTab->SelectedItemIndex = i;
            currentTab->MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);
            break;
        }
    }
}

void menu3dsSelectRandomGame(SMenuTab *currentTab) {
    int randomRetry = 0;
    while (randomRetry < 10) {
        int randomIndex = getRandomInt(0, (currentTab->MenuItems.size() - 1));

        if (currentTab->MenuItems[randomIndex].Type == MenuItemType::Action &&
            file3dsIsValidFilename(currentTab->MenuItems[randomIndex].Text.c_str(), {".smc", ".sfc", ".fig"})) {
            currentTab->SelectedItemIndex = randomIndex;
            currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(1);
            break;
        }

        randomRetry++;
    }
}

void menu3dsSetSelectedItemByIndex(SMenuTab& tab, int index)
{
    if (index >= 0 && index < tab.MenuItems.size()) {
        tab.SelectedItemIndex = index;

        int maxItems = MENU_HEIGHT;
        if (!tab.SubTitle.empty()) {
            maxItems--;
        }
        tab.MakeSureSelectionIsOnScreen(maxItems, 2);
    }
}

int menu3dsShowMenu(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab)
{
    isDialog = false;
    return menu3dsMenuSelectItem(dialogTab, isDialog, currentMenuTab, menuTab);

}

void menu3dsHideMenu(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab)
{
    ui3dsSetTranslate(0, 0);
}

int menu3dsShowDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab, const std::string& title, const std::string& dialogText, int newDialogBackColor, const std::vector<SMenuItem>& menuItems, int selectedID)
{
    SMenuTab *currentTab = &dialogTab;

    dialogBackColor = newDialogBackColor;

    currentTab->SetTitle(title);
    currentTab->DialogText.assign(dialogText);
    currentTab->MenuItems = menuItems;

    currentTab->FirstItemIndex = 0;
    currentTab->SelectedItemIndex = 0;

    for (int i = 0; i < currentTab->MenuItems.size(); i++)
    {
        if ((selectedID == -1 && menuItems[i].IsHighlightable()) || 
            menuItems[i].Value == selectedID)
        {
            currentTab->SelectedItemIndex = i;
            currentTab->MakeSureSelectionIsOnScreen(DIALOG_HEIGHT, 1);
            break;
        }
    }

    // fade the dialog fade in
    //
    aptMainLoop();
    menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
    menu3dsSwapBuffersAndWaitForVBlank();  
    //ui3dsCopyFromFrameBuffer(savedBuffer);

    isDialog = true;
    for (int f = 8; f >= 0; f--)
    {
        aptMainLoop();
        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab, 0, 0, f);
        menu3dsSwapBuffersAndWaitForVBlank();  
    }

    // Execute the dialog and return result.
    //
    if (currentTab->MenuItems.size() > 0)
    {
        int result = menu3dsMenuSelectItem(dialogTab, isDialog, currentMenuTab, menuTab);

        return result;
    }
    return 0;
}


void menu3dsHideDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab)
{
    // fade the dialog out
    //
    for (int f = 0; f <= 8; f++)
    {
        aptMainLoop();
        menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab, 0, 0, f);
        menu3dsSwapBuffersAndWaitForVBlank();    
    }

    isDialog = false;
    
    // draw the updated menu
    //
    aptMainLoop();
    menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
    menu3dsSwapBuffersAndWaitForVBlank();  
    
}

bool menu3dsTakeScreenshot(const char* path)
{
    int width = settings3DS.StretchWidth;
    int height = (settings3DS.StretchHeight == -1 ? PPU.ScreenHeight : settings3DS.StretchHeight);

	int x0 = (screenSettings.GameScreenWidth - width) / 2;
	int x1 = x0 + width;
	int y0 = (SCREEN_HEIGHT - height) / 2;
	int y1 = y0 + height;

    int channels = 3;
    u32 bufferSize = width * height * channels;
    
    u8* tempbuf = (u8*)linearAlloc(bufferSize);
    if (tempbuf == NULL)
        return false;
    memset(tempbuf, 0, bufferSize);

    u8* fb = (u8*)gfxGetFramebuffer(screenSettings.GameScreen, GFX_LEFT, NULL, NULL);

    // when taking screenshot via menu item we want to restore actual game screen first
    // otherwise we would save the pause screen
    if (tempBufGameScreen) {
        for (int x = x0; x < x1; x++) {
            int si = (x) * SCREEN_HEIGHT + (239 - y0);
            
            for (int y = y0; y < y1; y++) {
                fb[si] = tempBufGameScreen[si];

                si--;
            }
        }

        gfxScreenSwapBuffers(screenSettings.GameScreen, false);
    }

    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            // multiply by 4 because of rgba format
            int si = (((SCREEN_HEIGHT - 1 - y) + (x  * SCREEN_HEIGHT)) * 4);
            int di =((x - x0) + (y - y0) * width) * channels;

            tempbuf[di] = fb[si + 3];
            tempbuf[di + 1] = fb[si + 2];
            tempbuf[di + 2] = fb[si + 1];
        }

    int result = stbi_write_png(path, width, height, channels, tempbuf, width * channels);
    linearFree(tempbuf);

    if (tempBufGameScreen) {
        menu3dsDrawPauseScreen();
    }
    
    return result != 0;
};

void menu3dsSetHotkeysData(char* hotkeysData[HOTKEYS_COUNT][3]) {
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

void menu3dsSetFpsInfo(int color, float alpha, char *message) {
    int padding = 6;
    int screenWidth = ui3dsGetScreenWidth(screenSettings.SecondScreen);
    int width = 120 - padding * 2;
    int height = FONT_HEIGHT + padding * 2;

    if (alpha < 0.5f) {
        alpha = 0.5f;
    }

    Bounds b = ui3dsGetBounds(screenWidth, width, height, Position::BL, padding, padding);

    ui3dsDrawRect(b.left, b.top, b.right, b.bottom, 0x000000);
    ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, b.left, b.top, b.right, b.bottom, ui3dsApplyAlphaToColor(color, alpha), HALIGN_LEFT, message);
}

void menu3dsSetRomInfo() {
    int padding = 6;
    int screenWidth = ui3dsGetScreenWidth(screenSettings.SecondScreen);
    int width = screenWidth / 2;
    int height = padding + 200;

    Bounds b = ui3dsGetBounds(screenWidth, width, height, Position::TL, padding, padding * 3);

    char info[1024];
    char s[64];
    int color = 0xffffff;
    float alpha = (float)(settings3DS.SecondScreenOpacity) / OPACITY_STEPS;
    sprintf(s, "FPS: %d.9", Memory.ROMFramesPerSecond - 1);
    menu3dsSetFpsInfo(0xFFFFFF, alpha, s);

    Memory.MakeRomInfoText(info);
    ui3dsDrawStringWithWrapping(screenSettings.SecondScreen, b.left, b.top, b.right, b.bottom, ui3dsApplyAlphaToColor(color, alpha), HALIGN_LEFT, info);
}

// we only want to restore the pixel data behind the dialog
// therefore we globally store it inside `tempBufSecondScreen` and repaint the background when dialog disappears
// this is less expensive than redrawing the whole second screen + avoids frame skips during gameplay
// (though defining tempBufSecondScreen globally is probably not the best approach here)
void menu3dsHandleDialogBackground(bool storePixelData, int x0, int y0, int x1, int y1) {
    // only store/restore when necessary
    if ((!storePixelData && tempBufSecondScreen == nullptr) || (storePixelData && tempBufSecondScreen != nullptr)) {
        return;
    }
    
    uint16* fb = (uint16 *) gfxGetFramebuffer(screenSettings.SecondScreen, GFX_LEFT, NULL, NULL);
    
    if (storePixelData) {
        u32 bufferSize = screenSettings.SecondScreenWidth * SCREEN_HEIGHT * 2;
        tempBufSecondScreen = (u16*)linearAlloc(bufferSize);
        memset(tempBufSecondScreen, 0, bufferSize);
    } 
    
    for (int x = x0; x < x1; x++) {
        int si = (x) * SCREEN_HEIGHT + (239 - y0);
        
        for (int y = y0; y < y1; y++) {
            if (storePixelData)
                tempBufSecondScreen[si] = fb[si];
            else
                fb[si] = tempBufSecondScreen[si];

            si--;
        }
    }
}

void menu3dsSetSecondScreenContent(const char *dialogMessage, int dialogBackgroundColor, float dialogAlpha) {
    bool dialogVisible = ui3dsGetSecondScreenDialogState() != HIDDEN;
    
    if (dialogMessage || dialogVisible) {
        int padding = 4;
        int screenWidth = ui3dsGetScreenWidth(screenSettings.SecondScreen);
        int dialogWidth = 320 - padding * 2;
        int dialogHeight = FONT_HEIGHT * 2 + padding * 2;
        Bounds b = ui3dsGetBounds(screenWidth, dialogWidth, dialogHeight, Position::BC, 0, padding);

        // hide old dialog message
        if (dialogVisible) {
            ui3dsSetSecondScreenDialogState(HIDDEN);
        } else {
            // store current background before displaying dialog
            menu3dsHandleDialogBackground(true, b.left, b.top, b.right, b.bottom);
        }

        gfxSetDoubleBuffering(screenSettings.SecondScreen, false);
        gspWaitForVBlank(); // ensures dialog is drawn properly
        
        // restore background
        menu3dsHandleDialogBackground(false, b.left, b.top, b.right, b.bottom);
        
        // show new dialog
        if (dialogMessage) {
            ui3dsDrawRect(b.left, b.top, b.right, b.bottom, ui3dsOverlayBlendColor(0x555555, dialogBackgroundColor), dialogAlpha);
            ui3dsDrawStringWithWrapping(screenSettings.SecondScreen, b.left + padding  + 2, b.top + padding, b.right - padding + 2, b.bottom - padding, 0xffffff, HALIGN_LEFT, dialogMessage);
            ui3dsSetSecondScreenDialogState(VISIBLE);
        } else {
            // no dialog visible -> clear tempBufSecondScreen
            if (tempBufSecondScreen != nullptr) {
                linearFree(tempBufSecondScreen);
                tempBufSecondScreen = nullptr;
            }
        }

        return;           
    }

    // make sure tempBufSecondScreen is always cleared when second screen content has been set
    if (tempBufSecondScreen != nullptr) {
        linearFree(tempBufSecondScreen);
        tempBufSecondScreen = nullptr;
    }

    StoredFile cover;

    if (settings3DS.SecondScreenContent == CONTENT_IMAGE) {
        std::string coverFilename = file3dsGetAssociatedFilename(Memory.ROMFilename, ".png", "covers", true);

        if (!coverFilename.empty()) {
            cover = file3dsAddFileBufferToMemory("gameCover", coverFilename);

            // show fallback cover
            if (cover.Buffer.empty() && settings3DS.RomFsLoaded) {
                coverFilename = "romfs:/cover.png";
                cover = file3dsAddFileBufferToMemory("gameCover", coverFilename);
            }
        }
    }
    
    // better for game screen swapping
    gfxSetDoubleBuffering(screenSettings.SecondScreen, true); 

    for (int i = 0; i < 2; i ++) {
        aptMainLoop();
        menu3dsDrawBlackScreen();
        
        if (settings3DS.SecondScreenContent == CONTENT_IMAGE) {
            ui3dsRenderImage(screenSettings.SecondScreen, cover.Filename.c_str(), cover.Buffer.data(), cover.Buffer.size(), IMAGE_TYPE::COVER);          
        } 
        else if (settings3DS.SecondScreenContent == CONTENT_INFO) {
            menu3dsSetRomInfo();
        }

        gfxScreenSwapBuffers(screenSettings.SecondScreen, false);
        gspWaitForVBlank();
    }
}