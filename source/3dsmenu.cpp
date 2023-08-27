#include "snes9x.h"
#include "memmap.h"
#include <chrono>
#include <random>
#include <sstream>

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
int selectedMenuTab = 1;
int selectedItemIndex = 0;
int selectedRomItemIndex = 0;
int cheatsActive = 0;
int cheatsAll = 0;

int lastPercent = 0;
int currentPercent = 0;

u8* tempPixelData;

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

void menu3dsSetCurrentTabPosition(int currentMenuTab, int index) {
    selectedMenuTab = currentMenuTab;
    selectedItemIndex = index;
}

void menu3dsGetCurrentTabPosition(int& currentMenuTab, int& lastItemIndex) {
    currentMenuTab = selectedMenuTab;
    lastItemIndex = selectedItemIndex;
}

int menu3dsGetLastRomItemIndex() {
    return selectedRomItemIndex;
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
    int checkedItemTextColor, 
    int normalItemTextColor,
    int normalItemDescriptionTextColor,
    int disabledItemTextColor, 
    int headerItemTextColor, 
    int subtitleTextColor)
{
    int fontHeight = 13;
    
    // Display the subtitle
    if (!currentTab->SubTitle.empty())
    {
        maxItems--;
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, 20, menuStartY, 300, menuStartY + fontHeight, 
            subtitleTextColor, HALIGN_LEFT, currentTab->SubTitle.c_str());
        menuStartY += fontHeight;
    }

    int line = 0;
    int color = 0xffffff;

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
            ui3dsDrawRect(0, y, screenSettings.SecondScreenWidth, y + 14, selectedItemBackColor);
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
            color = normalItemTextColor;
            int lines = 15; // TODO: set value based on content
            horizontalPadding = 10;
            ui3dsDrawStringWithWrapping(screenSettings.SecondScreen, horizontalPadding, y, screenSettings.SecondScreenWidth - horizontalPadding, y + fontHeight * lines, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
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
                gauge[j] = (j == pos) ? '\xfa' : '\xfb';
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
    if (currentTab->FirstItemIndex != 0)
    {
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, screenSettings.SecondScreenWidth - horizontalPadding, menuStartY, screenSettings.SecondScreenWidth, menuStartY + fontHeight, disabledItemTextColor, HALIGN_CENTER, "\xf8");
    }

    // Draw the "down arrow" to indicate more options available at bottom
    //
    if (currentTab->FirstItemIndex + maxItems < currentTab->MenuItems.size())
    {
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, screenSettings.SecondScreenWidth - horizontalPadding, menuStartY + (maxItems - 1) * fontHeight, screenSettings.SecondScreenWidth, menuStartY + maxItems * fontHeight, disabledItemTextColor, HALIGN_CENTER, "\xf9");
    }
    
}

// Display the list of choices for selection
//
void menu3dsDrawMenu(std::vector<SMenuTab>& menuTab, int& currentMenuTab, int menuItemFrame, int translateY)
{
    SMenuTab *currentTab = &menuTab[currentMenuTab];

    // Draw the flat background
    //
    ui3dsDrawRect(0, 0, screenSettings.SecondScreenWidth, 24, 0x1976D2);
    ui3dsDrawRect(0, 24, screenSettings.SecondScreenWidth, 220, 0xFFFFFF);
    ui3dsDrawRect(0, 220, screenSettings.SecondScreenWidth, SCREEN_HEIGHT, 0x1976D2);

    // Draw the tabs at the top
    //
    for (int i = 0; i < static_cast<int>(menuTab.size()); i++)
    {
        int color = i == currentMenuTab ? 0xFFFFFF : 0x90CAF9;
        int accentColor = i == currentMenuTab ? 0xFF9A01 : 0x998A5E;

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

        if (i == currentMenuTab) {
            ui3dsDrawRect(xLeft, yCurrentTabBoxTop, xRight, yCurrentTabBoxBottom, 0xFFFFFF);
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
        0xFFFFFF, 1.0f);
    // battery body
    ui3dsDrawRect(
        battX2 - battFullLevelWidth - battBorderWidth, 
        battY1 - battBorderWidth, 
        battX2 + battBorderWidth, 
        battY2 + battBorderWidth, 
        0xFFFFFF, 1.0f);
    // battery's empty insides
    ui3dsDrawRect(
        battX2 - battFullLevelWidth, 
        battY1, 
        battX2, 
        battY2, 
        0x1976D2, 1.0f);


    bool hasExtendedMenuControls = currentTab->Title == "Load Game" && file3dsGetCurrentDirRomCount() > 1;

    if (!hasExtendedMenuControls) {
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, 10, SCREEN_HEIGHT - 17, screenSettings.SecondScreenWidth, SCREEN_HEIGHT, 0xFFFFFF, HALIGN_LEFT, 
        "A: Select \x0b7 B: Cancel");
        const int rightEdge = battX2 - battFullLevelWidth - battBorderWidth - 10;
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, 97, SCREEN_HEIGHT - 17, rightEdge, SCREEN_HEIGHT, 0xFFFFFF, HALIGN_RIGHT, getAppVersion("Snes9x for 3DS v"));
    } else {
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, 10, SCREEN_HEIGHT - 17, screenSettings.SecondScreenWidth, SCREEN_HEIGHT, 0xFFFFFF, HALIGN_LEFT, 
        "A: Select \x0b7 B: Cancel \x0b7 X: Page Up/Down \x0b7 Y: Random Game");
    }
    
    ptmuInit();
    
    u8 batteryChargeState = 0;
    u8 batteryLevel = 0;
    if(R_SUCCEEDED(PTMU_GetBatteryChargeState(&batteryChargeState)) && batteryChargeState) {
        ui3dsDrawRect(
            battX2-battFullLevelWidth + 1, battY1 + 1, 
            battX2 - 1, battY2 - 1, 0xFF9900, 1.0f);
    } else if(R_SUCCEEDED(PTMU_GetBatteryLevel(&batteryLevel))) {
        if (batteryLevel > 5)
            batteryLevel = 5;
        for (int i = 0; i < batteryLevel; i++)
        {
            ui3dsDrawRect(
                battX2-battLevelWidth*(i+1), battY1 + 1, 
                battX2-battLevelWidth*(i) - 1, battY2 - 1, 0xFFFFFF, 1.0f);
        }
    } else {
        //ui3dsDrawRect(battX2, battY1, battX2, battY2, 0xFFFFFF, 1.0f);
    }
 
    ptmuExit();

    int line = 0;
    int maxItems = MENU_HEIGHT;
    int menuStartY = 29;
    int selectedItemBackColor = menu3dsHasHighlightableItems(currentTab) ? 0x333333 : 0xFFFFFF;
    
    ui3dsSetTranslate(menuItemFrame * 3, translateY);

    if (menuItemFrame == 0)
    {
        menu3dsDrawItems(
            currentTab, 20, menuStartY, maxItems,
            selectedItemBackColor,  // selectedItemBackColor
            0xffffff,       // selectedItemTextColor
            0x777777,       // selectedItemDescriptionTextColor

            0x000000,       // checkedItemTextColor
            0x333333,       // normalItemTextColor      
            0x777777,       // normalItemDescriptionTextColor      
            0x888888,       // disabledItemTextColor
            0x1E88E5,       // headerItemTextColor
            0x1E88E5);      // subtitleTextColor


        if (currentTab->Title != "Load Game") {
            return;
        }

        selectedRomItemIndex = currentTab->SelectedItemIndex;

        // looking for available game thumbnail
        StoredFile file = file3dsGetStoredFileById(currentTab->MenuItems[selectedRomItemIndex].Text);

        if (!file.Buffer.empty()) {
           ui3dsRenderImage(screenSettings.SecondScreen, file.Filename.c_str(), file.Buffer.data(), file.Buffer.size(), IMAGE_TYPE::PREVIEW);
        }
    }
    else
    {
        if (menuItemFrame < 0)
            menuItemFrame = -menuItemFrame;
        float alpha = (float)(ANIMATE_TAB_STEPS - menuItemFrame + 1) / (ANIMATE_TAB_STEPS + 1);

        int white = ui3dsApplyAlphaToColor(0xFFFFFF, 1.0f - alpha);
        
         menu3dsDrawItems(
            currentTab, 20, menuStartY, maxItems,
            ui3dsApplyAlphaToColor(selectedItemBackColor, alpha) + white,
            ui3dsApplyAlphaToColor(0xffffff, alpha) + white,       // selectedItemTextColor
            ui3dsApplyAlphaToColor(0x777777, alpha) + white,       // selectedItemDescriptionTextColor

            ui3dsApplyAlphaToColor(0x000000, alpha) + white,       // checkedItemTextColor
            ui3dsApplyAlphaToColor(0x333333, alpha) + white,       // normalItemTextColor      
            ui3dsApplyAlphaToColor(0x777777, alpha) + white,       // normalItemDescriptionTextColor      
            ui3dsApplyAlphaToColor(0x888888, alpha) + white,       // disabledItemTextColor
            ui3dsApplyAlphaToColor(0x1E88E5, alpha) + white,       // headerItemTextColor
            ui3dsApplyAlphaToColor(0x1E88E5, alpha) + white);      // subtitleTextColor       
        //svcSleepThread((long)(1000000.0f * 1000.0f));
    }

      
}


int dialogBackColor = 0xEC407A;
int dialogTextColor = 0xffffff;
int dialogItemTextColor = 0xffffff;
int dialogSelectedItemTextColor = 0xffffff;
int dialogSelectedItemBackColor = 0x000000;

void menu3dsDrawDialog(SMenuTab& dialogTab)
{
    // Dialog's Background
    int dialogBackColor2 = ui3dsApplyAlphaToColor(dialogBackColor, 0.9f);
    ui3dsDrawRect(0, 0, screenSettings.SecondScreenWidth, 75, dialogBackColor2);
    ui3dsDrawRect(0, 75, screenSettings.SecondScreenWidth, 160, dialogBackColor);

    // Draw the dialog's title and descriptive text
    int dialogTitleTextColor = 
        ui3dsApplyAlphaToColor(dialogBackColor, 0.5f) + 
        ui3dsApplyAlphaToColor(dialogTextColor, 0.5f);
    ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, 30, 10, 290, 25, dialogTitleTextColor, HALIGN_LEFT, dialogTab.Title.c_str());
    ui3dsDrawStringWithWrapping(screenSettings.SecondScreen, 30, 30, 290, 70, dialogTextColor, HALIGN_LEFT, dialogTab.DialogText.c_str());

    // Draw the selectable items.
    int dialogItemDescriptionTextColor = dialogTitleTextColor;
    menu3dsDrawItems(
        &dialogTab, 30, 80, DIALOG_HEIGHT,
        dialogSelectedItemBackColor,        // selectedItemBackColor
        dialogSelectedItemTextColor,        // selectedItemTextColor
        dialogItemDescriptionTextColor,     // selectedItemDescriptionColor

        dialogItemTextColor,                // checkedItemTextColor
        dialogItemTextColor,                // normalItemTextColor
        dialogItemDescriptionTextColor,     // normalItemDescriptionTextColor
        dialogItemDescriptionTextColor,     // disabledItemTextColor
        dialogItemTextColor,                // headerItemTextColor
        dialogItemTextColor                 // subtitleTextColor
        );
}


void menu3dsDrawEverything(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab, int menuFrame = 0, int menuItemsFrame = 0, int dialogFrame = 0)
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
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, screenSettings.SecondScreenWidth - 120, 24, screenSettings.SecondScreenWidth - 8, 24 + 13, 
        0x999999, HALIGN_LEFT, s);
        swapBuffer = true;
    } else {
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, screenSettings.SecondScreenWidth - 120, 24, screenSettings.SecondScreenWidth - 8, 24 + 13, 
        0x999999, HALIGN_LEFT, s);
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
    std::string currentDir = std::string(file3dsGetCurrentDir());

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

        // exit loop when game thumbnail type has been updated or game screen has been swapped
        if (file3dsGetThumbnailsUpdated() || ui3dsGetScreenSwapped())
        {
            returnResult = -2;
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
        if (keysDown & KEY_B)
        {
            returnResult = -1;
            break;
        }
        if (keysDown & KEY_Y && currentTab->Title == "Load Game" && file3dsGetCurrentDirRomCount() > 1)
        {
            int randomRetry = 0;
            int lastSelectedItemIndex = currentTab->SelectedItemIndex;
            while (randomRetry < 10) {
                int randomIndex = getRandomInt(0, (currentTab->MenuItems.size() - 1));

                if (currentTab->MenuItems[randomIndex].Type == MenuItemType::Action &&
                    file3dsIsValidFilename(currentTab->MenuItems[randomIndex].Text.c_str(), {".smc", ".sfc", ".fig"})) {
                    currentTab->SelectedItemIndex = randomIndex;
                    returnResult = currentTab->MenuItems[currentTab->SelectedItemIndex].Value;
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(1);
                    break;
                }

                randomRetry++;
            }

            if (currentTab->SelectedItemIndex != lastSelectedItemIndex) {
                break;
            }
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
        if (keysDown & KEY_START || keysDown & KEY_A)
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
                snprintf(menuTextBuffer, 511, "%s", currentTab->MenuItems[currentTab->SelectedItemIndex].Text.c_str());
                int resultValue = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, menuTextBuffer,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].PickerDescription,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].PickerBackColor,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].PickerItems,
                    currentTab->MenuItems[currentTab->SelectedItemIndex].Value
                    );
                if (resultValue != -1)
                {
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(resultValue);
                }
                menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);


            }
        }
        if (keysDown & KEY_UP || ((thisKeysHeld & KEY_UP) && (framesDKeyHeld > 15) && (framesDKeyHeld % 2 == 0)))
        {
            int moveCursorTimes = 0;

            do
            {
                if ((thisKeysHeld & KEY_X) && currentTab->Title == "Load Game")
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
                if ((thisKeysHeld & KEY_X) && currentTab->Title == "Load Game")
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

    menu3dsSetCurrentTabPosition(currentMenuTab, currentTab->SelectedItemIndex);

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

    u8* framebuf = (u8*)gfxGetFramebuffer(screenSettings.GameScreen, GFX_LEFT, NULL, NULL);
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
        {
            // multiply by 4 because of rgba format
            int si = (((SCREEN_HEIGHT - 1 - y) + (x  * SCREEN_HEIGHT)) * 4);
            int di =((x - x0) + (y - y0) * width) * channels;

            tempbuf[di] = framebuf[si + 3];
            tempbuf[di + 1] = framebuf[si + 2];
            tempbuf[di + 2] = framebuf[si + 1];
        }

    int result = stbi_write_png(path, width, height, channels, tempbuf, width * channels);
    linearFree(tempbuf);
    
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
            case HOTKEY_SCREENSHOT: 
                hotkeysData[i][0]= "TakeScreenshot"; 
                hotkeysData[i][1]= "  Screenshot"; 
                hotkeysData[i][2]= "Takes a Screenshot from the current game";
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
    int width = 100 - padding * 2;
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
// therefore we globally store it inside `tempPixelData` and repaint the background when dialog disappears
// this is less expensive than just redrawing the whole second screen + avoids frame skips during gameplay
// (though defining tempPixelData globally is clearly not the best approach here)
bool menu3dsHandleDialogBackground(bool save, int x0, int y0, int x1, int y1) {
    int width = x1 - x0;
    int height = y1 - y0;
    int channels = 3;

    u16* fb = (u16*)gfxGetFramebuffer(screenSettings.SecondScreen, GFX_LEFT, NULL, NULL);
    
    if (save) {
        u32 bufferSize =  width * height * channels;

        // using u16* for tempPixelData would actually make the whole color conversion part unnecessarily
        // but seems to break when game border opacity changes (probably triggered by faulty memory allocation or similar?)
        tempPixelData = (u8*)linearAlloc(bufferSize);
        memset(tempPixelData, 0, bufferSize);
        
        for (int x = x0; x < x1; x++) {
            int si = (x) * SCREEN_HEIGHT + (239 - y0);

            for (int y = y0; y < y1; y++) {
                int di =((x - x0) + (y - y0) * width) * channels;

                // Convert 16-bit color to 8-bit color values
                uint16_t color = fb[si--];
                tempPixelData[di] = (color >> 11) << 3;
                tempPixelData[di + 1] = (color >> 5) << 2;
                tempPixelData[di + 2] = color << 3;
            }
        }

        return true;
    }

    // restore background from tempPixelData
    for (int x = x0; x < x1; x++) {
        int si = (x) * SCREEN_HEIGHT + (239 - y0);
        
        for (int y = y0; y < y1; y++) {
            int di =((x - x0) + (y - y0) * width) * channels;
            unsigned char r = tempPixelData[di];
            unsigned char g = tempPixelData[di+1];
            unsigned char b = tempPixelData[di+2];

            // Convert 8-bit color values to 16-bit color (RGB565)
            uint16_t color16 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

            fb[si--] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    }

    linearFree(tempPixelData);
    
    return true;
}

void menu3dsSetSecondScreenContent(const char *dialogMessage, int dialogBackgroundColor, float dialogAlpha) {
    bool hasDialog = ui3dsGetSecondScreenDialogState() != HIDDEN;
    
    if (dialogMessage || hasDialog) {
        int padding = 4;
        int screenWidth = ui3dsGetScreenWidth(screenSettings.SecondScreen);
        int dialogWidth = 320 - padding * 2;
        int dialogHeight = FONT_HEIGHT * 2 + padding * 2;
        Bounds b = ui3dsGetBounds(screenWidth, dialogWidth, dialogHeight, Position::BC, 0, padding);

        // hide old dialog message
        if (hasDialog) {
            if (tempPixelData) {
                menu3dsHandleDialogBackground(false, b.left, b.top, b.right, b.bottom);
            }
            
            ui3dsSetSecondScreenDialogState(HIDDEN);
        }

        // draw new dialog message
        if (dialogMessage) {        
            menu3dsHandleDialogBackground(true, b.left, b.top, b.right, b.bottom);
            ui3dsDrawRect(b.left, b.top, b.right, b.bottom, dialogBackgroundColor, dialogAlpha);
            ui3dsDrawStringWithWrapping(screenSettings.SecondScreen, b.left + padding  + 2, b.top + padding, b.right - padding + 2, b.bottom - padding, 0xffffff, HALIGN_LEFT, dialogMessage);
            ui3dsSetSecondScreenDialogState(VISIBLE);
        }

        return;           
    }

    if (tempPixelData) {
        linearFree(tempPixelData);
    }

    gfxSetDoubleBuffering(screenSettings.SecondScreen, true);

    StoredFile cover;

    if (settings3DS.SecondScreenContent == CONTENT_IMAGE) {
        std::string coverFilename = file3dsGetAssociatedFilename(Memory.ROMFilename, ".png", "covers", true);
        cover = file3dsAddFileBufferToMemory("gameCover", coverFilename);

        // show fallback cover
        if (cover.Buffer.empty()) {
            if (settings3DS.RomFsLoaded) {
                coverFilename = "romfs:/cover.png";
            } else {
                coverFilename = std::string(settings3DS.RootDir) + "/assets/cover.png";
		    }

            cover = file3dsAddFileBufferToMemory("gameCover", coverFilename);
        }
    }

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