#include <array>
#include <cmath>
#include <cstring>
#include <string.h>
#include <stdio.h>
#include <3ds.h>

#include "snes9x.h"
#include "memmap.h"
#include "port.h"

#include "3dsexit.h"
#include "3dsmenu.h"
#include "3dsgpu.h"
#include "3dssettings.h"
#include "3dsui.h"
#include "lodepng.h"

#define CONSOLE_WIDTH           40
#define MENU_HEIGHT             (14)
#define DIALOG_HEIGHT           (5)

#define SNES9X_VERSION "v1.30"
#define ANIMATE_TAB_STEPS 3

bool                transferGameScreen = false;
int                 transferGameScreenCount = 0;
bool                swapBuffer = true;
int selectedMenuTab = 0;
int selectedItemIndex = 0;

void menu3dsSetCurrentTabPosition(int currentMenuTab, int index) {
    selectedMenuTab = currentMenuTab;
    selectedItemIndex = index;
}

void menu3dsGetCurrentTabPosition(int& currentMenuTab, int& lastItemIndex) {
    currentMenuTab = selectedMenuTab;
    lastItemIndex = selectedItemIndex;
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
    ui3dsDrawRect(0, 0, screenSettings.SubScreenWidth, SCREEN_HEIGHT, 0x000000, opacity);    
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
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    else
    {
        gspWaitForVBlank();
    }
    
    swapBuffer = false;
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
        ui3dsDrawStringWithNoWrapping(20, menuStartY, 300, menuStartY + fontHeight, 
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
            ui3dsDrawRect(0, y, screenSettings.SubScreenWidth, y + 14, selectedItemBackColor);
        }
        
        if (currentTab->MenuItems[i].Type == MenuItemType::Header1)
        {
            color = headerItemTextColor;
            ui3dsDrawStringWithNoWrapping(horizontalPadding, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            ui3dsDrawRect(horizontalPadding, y + fontHeight - 1, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color);
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Header2)
        {
            color = headerItemTextColor;
            ui3dsDrawStringWithNoWrapping(horizontalPadding, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Disabled)
        {
            color = disabledItemTextColor;
            ui3dsDrawStringWithNoWrapping(horizontalPadding, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Action)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;
            ui3dsDrawStringWithNoWrapping(horizontalPadding, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

            color = normalItemDescriptionTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemDescriptionTextColor;
            if (!currentTab->MenuItems[i].Description.empty())
            {
                ui3dsDrawStringWithNoWrapping(horizontalPadding, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Description.c_str());
            }
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Checkbox || currentTab->MenuItems[i].Type == MenuItemType::Radio)
        {
            color = currentTab->MenuItems[i].Value == 0 ? disabledItemTextColor : normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;
            ui3dsDrawStringWithNoWrapping(horizontalPadding, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());
            ui3dsDrawStringWithNoWrapping(280, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].Value != 1 ? "\xfe" : "\xfd");
        }

        else if (currentTab->MenuItems[i].Type == MenuItemType::Gauge)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;

            ui3dsDrawStringWithNoWrapping(horizontalPadding, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

            const int max = 40;
            int diff = currentTab->MenuItems[i].GaugeMaxValue - currentTab->MenuItems[i].GaugeMinValue;
            int pos = (currentTab->MenuItems[i].Value - currentTab->MenuItems[i].GaugeMinValue) * (max - 1) / diff;

            char gauge[max+1];
            for (int j = 0; j < max; j++)
                gauge[j] = (j == pos) ? '\xfa' : '\xfb';
            gauge[max] = 0;
            ui3dsDrawStringWithNoWrapping(245, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, gauge);
        }
        else if (currentTab->MenuItems[i].Type == MenuItemType::Picker)
        {
            color = normalItemTextColor;
            if (currentTab->SelectedItemIndex == i)
                color = selectedItemTextColor;

            ui3dsDrawStringWithNoWrapping(horizontalPadding, y, 160, y + fontHeight, color, HALIGN_LEFT, currentTab->MenuItems[i].Text.c_str());

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
                    ui3dsDrawStringWithNoWrapping(160, y, screenSettings.SubScreenWidth - horizontalPadding, y + fontHeight, color, HALIGN_RIGHT, currentTab->MenuItems[i].PickerItems[selectedIndex].Text.c_str());
                }
            }
        }

        line ++;
    }


    // Draw the "up arrow" to indicate more options available at top
    //
    if (currentTab->FirstItemIndex != 0)
    {
        ui3dsDrawStringWithNoWrapping(screenSettings.SubScreenWidth - horizontalPadding, menuStartY, screenSettings.SubScreenWidth, menuStartY + fontHeight, disabledItemTextColor, HALIGN_CENTER, "\xf8");
    }

    // Draw the "down arrow" to indicate more options available at bottom
    //
    if (currentTab->FirstItemIndex + maxItems < currentTab->MenuItems.size())
    {
        ui3dsDrawStringWithNoWrapping(screenSettings.SubScreenWidth - horizontalPadding, menuStartY + (maxItems - 1) * fontHeight, screenSettings.SubScreenWidth, menuStartY + maxItems * fontHeight, disabledItemTextColor, HALIGN_CENTER, "\xf9");
    }
    
}

// Display the list of choices for selection
//
void menu3dsDrawMenu(std::vector<SMenuTab>& menuTab, int& currentMenuTab, int menuItemFrame, int translateY)
{
    SMenuTab *currentTab = &menuTab[currentMenuTab];

    char tempBuffer[CONSOLE_WIDTH];

    // Draw the flat background
    //
    ui3dsDrawRect(0, 0, screenSettings.SubScreenWidth, 24, 0x1976D2);
    ui3dsDrawRect(0, 24, screenSettings.SubScreenWidth, 220, 0xFFFFFF);
    ui3dsDrawRect(0, 220, screenSettings.SubScreenWidth, SCREEN_HEIGHT, 0x1976D2);

    // Draw the tabs at the top
    //
    for (int i = 0; i < static_cast<int>(menuTab.size()); i++)
    {
        int color = i == currentMenuTab ? 0xFFFFFF : 0x90CAF9;

        int offsetLeft = 10;
        int offsetRight = 10;

        int availableSpace = screenSettings.SubScreenWidth - ( offsetLeft + offsetRight );
        int pixelPerOption =      availableSpace / static_cast<int>(menuTab.size());
        int extraPixelOnOptions = availableSpace % static_cast<int>(menuTab.size());

        // each tab gains an equal amount of horizontal space
        // if space is not cleanly divisible by tab count, the earlier tabs gain one extra pixel each until we reach the requested space
        int xLeft =  (     i     * pixelPerOption ) + offsetLeft + std::min( i,     extraPixelOnOptions );
        int xRight = ( ( i + 1 ) * pixelPerOption ) + offsetLeft + std::min( i + 1, extraPixelOnOptions );
        int yTextTop = 6;
        int yCurrentTabBoxTop = 21;
        int yCurrentTabBoxBottom = 24;

        ui3dsDrawStringWithNoWrapping(xLeft, yTextTop, xRight, yCurrentTabBoxTop, color, HALIGN_CENTER, menuTab[i].Title.c_str());

        if (i == currentMenuTab) {
            ui3dsDrawRect(xLeft, yCurrentTabBoxTop, xRight, yCurrentTabBoxBottom, 0xFFFFFF);
        }
    }

    ui3dsDrawStringWithNoWrapping(10, SCREEN_HEIGHT - 17, screenSettings.SubScreenWidth / 2, SCREEN_HEIGHT, 0xFFFFFF, HALIGN_LEFT,
        "A:Select  B:Cancel");
    ui3dsDrawStringWithNoWrapping(screenSettings.SubScreenWidth / 2, SCREEN_HEIGHT - 17, screenSettings.SubScreenWidth - 40, SCREEN_HEIGHT, 0xFFFFFF, HALIGN_RIGHT,
        "SNES9x for 3DS " SNES9X_VERSION);

    //battery display
    const int maxBatteryLevel = 5;
    const int battLevelWidth = 3;
    const int battFullLevelWidth = (maxBatteryLevel) * battLevelWidth + 1;
    const int battBorderWidth = 1;
    const int battY1 = 227;
    const int battY2 = 234;
    const int battX2 = screenSettings.SubScreenWidth - 10;
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

    ui3dsSetTranslate(menuItemFrame * 3, translateY);

    if (menuItemFrame == 0)
    {
        menu3dsDrawItems(
            currentTab, 20, menuStartY, maxItems,
            0x333333,       // selectedItemBackColor
            0xffffff,       // selectedItemTextColor
            0x777777,       // selectedItemDescriptionTextColor

            0x000000,       // checkedItemTextColor
            0x333333,       // normalItemTextColor      
            0x777777,       // normalItemDescriptionTextColor      
            0x888888,       // disabledItemTextColor
            0x1E88E5,       // headerItemTextColor
            0x1E88E5);      // subtitleTextColor
    }
    else
    {
        if (menuItemFrame < 0)
            menuItemFrame = -menuItemFrame;
        float alpha = (float)(ANIMATE_TAB_STEPS - menuItemFrame + 1) / (ANIMATE_TAB_STEPS + 1);

        int white = ui3dsApplyAlphaToColor(0xFFFFFF, 1.0f - alpha);
        
         menu3dsDrawItems(
            currentTab, 20, menuStartY, maxItems,
            ui3dsApplyAlphaToColor(0x333333, alpha) + white,
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
    ui3dsDrawRect(0, 0, screenSettings.SubScreenWidth, 75, dialogBackColor2);
    ui3dsDrawRect(0, 75, screenSettings.SubScreenWidth, 160, dialogBackColor);

    // Draw the dialog's title and descriptive text
    int dialogTitleTextColor = 
        ui3dsApplyAlphaToColor(dialogBackColor, 0.5f) + 
        ui3dsApplyAlphaToColor(dialogTextColor, 0.5f);
    ui3dsDrawStringWithNoWrapping(30, 10, 290, 25, dialogTitleTextColor, HALIGN_LEFT, dialogTab.Title.c_str());
    ui3dsDrawStringWithWrapping(30, 30, 290, 70, dialogTextColor, HALIGN_LEFT, dialogTab.DialogText.c_str());

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

        ui3dsSetViewport(0, 0, screenSettings.SubScreenWidth, SCREEN_HEIGHT);
        ui3dsSetTranslate(0, 0);
        ui3dsDrawRect(0, 0, screenSettings.SubScreenWidth, y, 0x000000);
        ui3dsSetTranslate(0, y);
        menu3dsDrawMenu(menuTab, currentMenuTab, menuItemsFrame, y);
    }
    else
    {
        int y = 80 + dialogFrame * dialogFrame * 80 / 32;

        ui3dsSetViewport(0, 0, screenSettings.SubScreenWidth, y);
        //ui3dsBlitToFrameBuffer(savedBuffer, 1.0f - (float)(8 - dialogFrame) / 10);
        ui3dsSetTranslate(0, 0);
        menu3dsDrawMenu(menuTab, currentMenuTab, 0, 0);
        ui3dsDrawRect(0, 0, screenSettings.SubScreenWidth, y, 0x000000, (float)(8 - dialogFrame) / 10);

        ui3dsSetViewport(0, 0, screenSettings.SubScreenWidth, SCREEN_HEIGHT);
        ui3dsSetTranslate(0, y);
        menu3dsDrawDialog(dialogTab);
        ui3dsSetTranslate(0, 0);
    }
    swapBuffer = true;

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

    while (aptMainLoop())
    {   
        if (appExiting)
        {
            returnResult = -1;
            break;
        }

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
                // abuse GaugeMinValue for radio group id to update state
                int groupId = currentTab->MenuItems[currentTab->SelectedItemIndex].GaugeMinValue;
                if (groupId) {
                    for (int i = 0; i < currentTab->MenuItems.size(); i++)
                    {
                        // match related items
                        if (currentTab->MenuItems[i].GaugeMinValue == groupId)
                        {
                            // uncheck active radio item, but don't set it to disabledItemTextColor
                            if (currentTab->MenuItems[i].Type == MenuItemType::Radio && currentTab->MenuItems[i].Value == RADIO_ACTIVE)
                                currentTab->MenuItems[i].SetValue(RADIO_INACTIVE);

                        }
                    }
                }
                currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(RADIO_ACTIVE);
                menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
            }
            if (currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Checkbox)
            {
                if (currentTab->MenuItems[currentTab->SelectedItemIndex].Value == 0)
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(1);
                else
                    currentTab->MenuItems[currentTab->SelectedItemIndex].SetValue(0);
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
                if (thisKeysHeld & KEY_X)
                {
                    currentTab->SelectedItemIndex -= 15;
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
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Header2
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
                if (thisKeysHeld & KEY_X)
                {
                    currentTab->SelectedItemIndex += 15;
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
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type == MenuItemType::Header2
                ) &&
                moveCursorTimes < currentTab->MenuItems.size());

            currentTab->MakeSureSelectionIsOnScreen(maxItems, isDialog ? 1 : 2);
            menu3dsDrawEverything(dialogTab, isDialog, currentMenuTab, menuTab);
        }

        menu3dsSetCurrentTabPosition(currentMenuTab, currentTab->SelectedItemIndex);

        menu3dsSwapBuffersAndWaitForVBlank();
    }

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

int menu3dsShowMenu(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab, bool animateMenu)
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
    u32 buffsize = screenSettings.GameScreenWidth*SCREEN_HEIGHT*3;
    u8* tempbuf = (u8*)linearAlloc(buffsize);
    if (tempbuf == NULL)
        return false;
    memset(tempbuf, 0, buffsize);

    u8* framebuf = (u8*)gfxGetFramebuffer(screenSettings.GameScreen, GFX_LEFT, NULL, NULL);
    for (int y = 0; y < SCREEN_HEIGHT; y++)
        for (int x = 0; x < screenSettings.GameScreenWidth; x++)
        {
            int si = (((SCREEN_HEIGHT - 1 - y) + (x * SCREEN_HEIGHT)) * 4);
            int di =(x + y * screenSettings.GameScreenWidth ) * 3;

            tempbuf[di+0] = framebuf[si+3];
            tempbuf[di+1] = framebuf[si+2];
            tempbuf[di+2] = framebuf[si+1];
        }

    unsigned char* png;
    size_t pngsize;

    unsigned error = lodepng_encode24(&png, &pngsize, tempbuf, screenSettings.GameScreenWidth, SCREEN_HEIGHT);
    if(!error) lodepng_save_file(png, pngsize, path);

    free (png);
    linearFree(tempbuf);
    return true;
};

void menu3dsPrintRomInfo(char* txt)
{
    char temp[100];
    sprintf(txt,"ROM Name: %s",Memory.ROMName);
    strcat(txt, "\nROM Version: ");
    sprintf(temp, "1.%d", (Memory.HiROM)?Memory.ROM[0x0FFDB]:Memory.ROM[0x7FDB]);
    strcat(txt, temp);
    sprintf(temp, "\nROM Map: %s\nROM Speed: %02X/%s\n",(Memory.HiROM)?"HiROM":"LoROM", Memory.ROMSpeed, ((Memory.ROMSpeed&0x10)!=0)?"FastROM":"SlowROM");
    strcat(txt, temp);
    strcat(txt, "\n\nOutput: ");
    if(Memory.ROMRegion>12||Memory.ROMRegion<2)
        strcat(txt, "NTSC 60Hz");
    else 
        strcat(txt, "PAL 50Hz");

    strcat(txt, "\nRegion: ");
    switch(Memory.ROMRegion)
    {
        case 0:
            strcat(txt, "Japan");
            break;
        case 1:
            strcat(txt, "USA/Canada");
            break;
        case 2:
            strcat(txt, "Oceania, Europe, and Asia");
            break;
        case 3:
            strcat(txt, "Sweden");
            break;
        case 4:
            strcat(txt, "Finland");
            break;
        case 5:
            strcat(txt, "Denmark");
            break;
        case 6:
            strcat(txt, "France");
            break;
        case 7:
            strcat(txt, "Holland");
            break;
        case 8:
            strcat(txt, "Spain");
            break;
        case 9:
            strcat(txt, "Germany, Austria, and Switzerland");
            break;
        case 10:
            strcat(txt, "Italy");
            break;
        case 11:
            strcat(txt, "Hong Kong and China");
            break;
        case 12:
            strcat(txt, "Indonesia");
            break;
        case 13:
            strcat(txt, "South Korea");
            break;
        case 14:strcat(txt, "Unknown region 14");break;
        default:strcat(txt, "Unknown region 15");break;
    }
#define NOTKNOWN "Unknown Company "
    strcat(txt, "\nLicensee: ");
    int tmp=atoi(Memory.CompanyId);
    if(tmp==0)
        tmp=(Memory.HiROM)?Memory.ROM[0x0FFDA]:Memory.ROM[0x7FDA];
    switch(tmp)
        //				switch(((Memory.ROMSpeed&0x0F)!=0)?Memory.ROM[0x0FFDA]:Memory.ROM[0x7FDA])
        //				switch(atoi(Memory.CompanyId))
        //				switch(((Memory.CompanyId[0]-'0')*16)+(Memory.CompanyId[1]-'0'))
    {
        case 0:strcat(txt, "INVALID COMPANY");break;
        case 1:strcat(txt, "Nintendo");break;
        case 2:strcat(txt, "Ajinomoto");break;
        case 3:strcat(txt, "Imagineer-Zoom");break;
        case 4:strcat(txt, "Chris Gray Enterprises Inc.");break;
        case 5:strcat(txt, "Zamuse");break;
        case 6:strcat(txt, "Falcom");break;
        case 7:strcat(txt, NOTKNOWN "7");break;
        case 8:strcat(txt, "Capcom");break;
        case 9:strcat(txt, "HOT-B");break;
        case 10:strcat(txt, "Jaleco");break;
        case 11:strcat(txt, "Coconuts");break;
        case 12:strcat(txt, "Rage Software");break;
        case 13:strcat(txt, "Micronet"); break; //Acc. ZFE
        case 14:strcat(txt, "Technos");break;
        case 15:strcat(txt, "Mebio Software");break;
        case 16:strcat(txt, "SHOUEi System"); break; //Acc. ZFE
        case 17:strcat(txt, "Starfish");break; //UCON 64
        case 18:strcat(txt, "Gremlin Graphics");break;
        case 19:strcat(txt, "Electronic Arts");break;
        case 20:strcat(txt, "NCS / Masaya"); break; //Acc. ZFE
        case 21:strcat(txt, "COBRA Team");break;
        case 22:strcat(txt, "Human/Field");break;
        case 23:strcat(txt, "KOEI");break;
        case 24:strcat(txt, "Hudson Soft");break;
        case 25:strcat(txt, "Game Village");break;//uCON64
        case 26:strcat(txt, "Yanoman");break;
        case 27:strcat(txt, NOTKNOWN "27");break;
        case 28:strcat(txt, "Tecmo");break;
        case 29:strcat(txt, NOTKNOWN "29");break;
        case 30:strcat(txt, "Open System");break;
        case 31:strcat(txt, "Virgin Games");break;
        case 32:strcat(txt, "KSS");break;
        case 33:strcat(txt, "Sunsoft");break;
        case 34:strcat(txt, "POW");break;
        case 35:strcat(txt, "Micro World");break;
        case 36:strcat(txt, NOTKNOWN "36");break;
        case 37:strcat(txt, NOTKNOWN "37");break;
        case 38:strcat(txt, "Enix");break;
        case 39:strcat(txt, "Loriciel/Electro Brain");break;//uCON64
        case 40:strcat(txt, "Kemco");break;
        case 41:strcat(txt, "Seta Co.,Ltd.");break;
        case 42:strcat(txt, "Culture Brain"); break; //Acc. ZFE
        case 43:strcat(txt, "Irem Japan");break;//Irem? Gun Force J
        case 44:strcat(txt, "Pal Soft"); break; //Acc. ZFE
        case 45:strcat(txt, "Visit Co.,Ltd.");break;
        case 46:strcat(txt, "INTEC Inc."); break; //Acc. ZFE
        case 47:strcat(txt, "System Sacom Corp."); break; //Acc. ZFE
        case 48:strcat(txt, "Viacom New Media");break; //Zoop!
        case 49:strcat(txt, "Carrozzeria");break;
        case 50:strcat(txt, "Dynamic");break;
        case 51:strcat(txt, "Nintendo");break;
        case 52:strcat(txt, "Magifact");break;
        case 53:strcat(txt, "Hect");break;
        case 54:strcat(txt, NOTKNOWN "54");break;
        case 55:strcat(txt, NOTKNOWN "55");break;
        case 56:strcat(txt, "Capcom Europe");break;//Capcom? BOF2(E) MM7 (E)
        case 57:strcat(txt, "Accolade Europe");break;//Accolade?Bubsy 2 (E)
        case 58:strcat(txt, NOTKNOWN "58");break;
        case 59:strcat(txt, "Arcade Zone");break;//uCON64
        case 60:strcat(txt, "Empire Software");break;
        case 61:strcat(txt, "Loriciel");break;
        case 62:strcat(txt, "Gremlin Graphics"); break; //Acc. ZFE
        case 63:strcat(txt, NOTKNOWN "63");break;
        case 64:strcat(txt, "Seika Corp.");break;
        case 65:strcat(txt, "UBI Soft");break;
        case 66:strcat(txt, NOTKNOWN "66");break;
        case 67:strcat(txt, NOTKNOWN "67");break;
        case 68:strcat(txt, "LifeFitness Exertainment");break;//?? Exertainment Mountain Bike Rally (U).zip
        case 69:strcat(txt, NOTKNOWN "69");break;
        case 70:strcat(txt, "System 3");break;
        case 71:strcat(txt, "Spectrum Holobyte");break;
        case 72:strcat(txt, NOTKNOWN "72");break;
        case 73:strcat(txt, "Irem");break;
        case 74:strcat(txt, NOTKNOWN "74");break;
        case 75:strcat(txt, "Raya Systems/Sculptured Software");break;
        case 76:strcat(txt, "Renovation Products");break;
        case 77:strcat(txt, "Malibu Games/Black Pearl");break;
        case 78:strcat(txt, NOTKNOWN "78");break;
        case 79:strcat(txt, "U.S. Gold");break;
        case 80:strcat(txt, "Absolute Entertainment");break;
        case 81:strcat(txt, "Acclaim");break;
        case 82:strcat(txt, "Activision");break;
        case 83:strcat(txt, "American Sammy");break;
        case 84:strcat(txt, "GameTek");break;
        case 85:strcat(txt, "Hi Tech Expressions");break;
        case 86:strcat(txt, "LJN Toys");break;
        case 87:strcat(txt, NOTKNOWN "87");break;
        case 88:strcat(txt, NOTKNOWN "88");break;
        case 89:strcat(txt, NOTKNOWN "89");break;
        case 90:strcat(txt, "Mindscape");break;
        case 91:strcat(txt, "Romstar, Inc."); break; //Acc. ZFE
        case 92:strcat(txt, NOTKNOWN "92");break;
        case 93:strcat(txt, "Tradewest");break;
        case 94:strcat(txt, NOTKNOWN "94");break;
        case 95:strcat(txt, "American Softworks Corp.");break;
        case 96:strcat(txt, "Titus");break;
        case 97:strcat(txt, "Virgin Interactive Entertainment");break;
        case 98:strcat(txt, "Maxis");break;
        case 99:strcat(txt, "Origin/FCI/Pony Canyon");break;//uCON64
        case 100:strcat(txt, NOTKNOWN "100");break;
        case 101:strcat(txt, NOTKNOWN "101");break;
        case 102:strcat(txt, NOTKNOWN "102");break;
        case 103:strcat(txt, "Ocean");break;
        case 104:strcat(txt, NOTKNOWN "104");break;
        case 105:strcat(txt, "Electronic Arts");break;
        case 106:strcat(txt, NOTKNOWN "106");break;
        case 107:strcat(txt, "Laser Beam");break;
        case 108:strcat(txt, NOTKNOWN "108");break;
        case 109:strcat(txt, NOTKNOWN "109");break;
        case 110:strcat(txt, "Elite");break;
        case 111:strcat(txt, "Electro Brain");break;
        case 112:strcat(txt, "Infogrames");break;
        case 113:strcat(txt, "Interplay");break;
        case 114:strcat(txt, "LucasArts");break;
        case 115:strcat(txt, "Parker Brothers");break;
        case 116:strcat(txt, "Konami");break;//uCON64
        case 117:strcat(txt, "STORM");break;
        case 118:strcat(txt, NOTKNOWN "118");break;
        case 119:strcat(txt, NOTKNOWN "119");break;
        case 120:strcat(txt, "THQ Software");break;
        case 121:strcat(txt, "Accolade Inc.");break;
        case 122:strcat(txt, "Triffix Entertainment");break;
        case 123:strcat(txt, NOTKNOWN "123");break;
        case 124:strcat(txt, "Microprose");break;
        case 125:strcat(txt, NOTKNOWN "125");break;
        case 126:strcat(txt, NOTKNOWN "126");break;
        case 127:strcat(txt, "Kemco");break;
        case 128:strcat(txt, "Misawa");break;
        case 129:strcat(txt, "Teichio");break;
        case 130:strcat(txt, "Namco Ltd.");break;
        case 131:strcat(txt, "Lozc");break;
        case 132:strcat(txt, "Koei");break;
        case 133:strcat(txt, NOTKNOWN "133");break;
        case 134:strcat(txt, "Tokuma Shoten Intermedia");break;
        case 135:strcat(txt, "Tsukuda Original"); break; //Acc. ZFE
        case 136:strcat(txt, "DATAM-Polystar");break;
        case 137:strcat(txt, NOTKNOWN "137");break;
        case 138:strcat(txt, NOTKNOWN "138");break;
        case 139:strcat(txt, "Bullet-Proof Software");break;
        case 140:strcat(txt, "Vic Tokai");break;
        case 141:strcat(txt, NOTKNOWN "141");break;
        case 142:strcat(txt, "Character Soft");break;
        case 143:strcat(txt, "I\'\'Max");break;
        case 144:strcat(txt, "Takara");break;
        case 145:strcat(txt, "CHUN Soft");break;
        case 146:strcat(txt, "Video System Co., Ltd.");break;
        case 147:strcat(txt, "BEC");break;
        case 148:strcat(txt, NOTKNOWN "148");break;
        case 149:strcat(txt, "Varie");break;
        case 150:strcat(txt, "Yonezawa / S'Pal Corp."); break; //Acc. ZFE
        case 151:strcat(txt, "Kaneco");break;
        case 152:strcat(txt, NOTKNOWN "152");break;
        case 153:strcat(txt, "Pack in Video");break;
        case 154:strcat(txt, "Nichibutsu");break;
        case 155:strcat(txt, "TECMO");break;
        case 156:strcat(txt, "Imagineer Co.");break;
        case 157:strcat(txt, NOTKNOWN "157");break;
        case 158:strcat(txt, NOTKNOWN "158");break;
        case 159:strcat(txt, NOTKNOWN "159");break;
        case 160:strcat(txt, "Telenet");break;
        case 161:strcat(txt, "Hori"); break; //Acc. uCON64
        case 162:strcat(txt, NOTKNOWN "162");break;
        case 163:strcat(txt, NOTKNOWN "163");break;
        case 164:strcat(txt, "Konami");break;
        case 165:strcat(txt, "K.Amusement Leasing Co.");break;
        case 166:strcat(txt, NOTKNOWN "166");break;
        case 167:strcat(txt, "Takara");break;
        case 168:strcat(txt, NOTKNOWN "168");break;
        case 169:strcat(txt, "Technos Jap.");break;
        case 170:strcat(txt, "JVC");break;
        case 171:strcat(txt, NOTKNOWN "171");break;
        case 172:strcat(txt, "Toei Animation");break;
        case 173:strcat(txt, "Toho");break;
        case 174:strcat(txt, NOTKNOWN "174");break;
        case 175:strcat(txt, "Namco Ltd.");break;
        case 176:strcat(txt, "Media Rings Corp."); break; //Acc. ZFE
        case 177:strcat(txt, "ASCII Co. Activison");break;
        case 178:strcat(txt, "Bandai");break;
        case 179:strcat(txt, NOTKNOWN "179");break;
        case 180:strcat(txt, "Enix America");break;
        case 181:strcat(txt, NOTKNOWN "181");break;
        case 182:strcat(txt, "Halken");break;
        case 183:strcat(txt, NOTKNOWN "183");break;
        case 184:strcat(txt, NOTKNOWN "184");break;
        case 185:strcat(txt, NOTKNOWN "185");break;
        case 186:strcat(txt, "Culture Brain");break;
        case 187:strcat(txt, "Sunsoft");break;
        case 188:strcat(txt, "Toshiba EMI");break;
        case 189:strcat(txt, "Sony Imagesoft");break;
        case 190:strcat(txt, NOTKNOWN "190");break;
        case 191:strcat(txt, "Sammy");break;
        case 192:strcat(txt, "Taito");break;
        case 193:strcat(txt, NOTKNOWN "193");break;
        case 194:strcat(txt, "Kemco");break;
        case 195:strcat(txt, "Square");break;
        case 196:strcat(txt, "Tokuma Soft");break;
        case 197:strcat(txt, "Data East");break;
        case 198:strcat(txt, "Tonkin House");break;
        case 199:strcat(txt, NOTKNOWN "199");break;
        case 200:strcat(txt, "KOEI");break;
        case 201:strcat(txt, NOTKNOWN "201");break;
        case 202:strcat(txt, "Konami USA");break;
        case 203:strcat(txt, "NTVIC");break;
        case 204:strcat(txt, NOTKNOWN "204");break;
        case 205:strcat(txt, "Meldac");break;
        case 206:strcat(txt, "Pony Canyon");break;
        case 207:strcat(txt, "Sotsu Agency/Sunrise");break;
        case 208:strcat(txt, "Disco/Taito");break;
        case 209:strcat(txt, "Sofel");break;
        case 210:strcat(txt, "Quest Corp.");break;
        case 211:strcat(txt, "Sigma");break;
        case 212:strcat(txt, "Ask Kodansha Co., Ltd."); break; //Acc. ZFE
        case 213:strcat(txt, NOTKNOWN "213");break;
        case 214:strcat(txt, "Naxat");break;
        case 215:strcat(txt, NOTKNOWN "215");break;
        case 216:strcat(txt, "Capcom Co., Ltd.");break;
        case 217:strcat(txt, "Banpresto");break;
        case 218:strcat(txt, "Tomy");break;
        case 219:strcat(txt, "Acclaim");break;
        case 220:strcat(txt, NOTKNOWN "220");break;
        case 221:strcat(txt, "NCS");break;
        case 222:strcat(txt, "Human Entertainment");break;
        case 223:strcat(txt, "Altron");break;
        case 224:strcat(txt, "Jaleco");break;
        case 225:strcat(txt, NOTKNOWN "225");break;
        case 226:strcat(txt, "Yutaka");break;
        case 227:strcat(txt, NOTKNOWN "227");break;
        case 228:strcat(txt, "T&ESoft");break;
        case 229:strcat(txt, "EPOCH Co.,Ltd.");break;
        case 230:strcat(txt, NOTKNOWN "230");break;
        case 231:strcat(txt, "Athena");break;
        case 232:strcat(txt, "Asmik");break;
        case 233:strcat(txt, "Natsume");break;
        case 234:strcat(txt, "King Records");break;
        case 235:strcat(txt, "Atlus");break;
        case 236:strcat(txt, "Sony Music Entertainment");break;
        case 237:strcat(txt, NOTKNOWN "237");break;
        case 238:strcat(txt, "IGS");break;
        case 239:strcat(txt, NOTKNOWN "239");break;
        case 240:strcat(txt, NOTKNOWN "240");break;
        case 241:strcat(txt, "Motown Software");break;
        case 242:strcat(txt, "Left Field Entertainment");break;
        case 243:strcat(txt, "Beam Software");break;
        case 244:strcat(txt, "Tec Magik");break;
        case 245:strcat(txt, NOTKNOWN "245");break;
        case 246:strcat(txt, NOTKNOWN "246");break;
        case 247:strcat(txt, NOTKNOWN "247");break;
        case 248:strcat(txt, NOTKNOWN "248");break;
        case 249:strcat(txt, "Cybersoft");break;
        case 250:strcat(txt, NOTKNOWN "250");break;
        case 251:strcat(txt, "Psygnosis"); break; //Acc. ZFE
        case 252:strcat(txt, NOTKNOWN "252");break;
        case 253:strcat(txt, NOTKNOWN "253");break;
        case 254:strcat(txt, "Davidson"); break; //Acc. uCON64
        case 255:strcat(txt, NOTKNOWN "255");break;
        default:strcat(txt, NOTKNOWN);break;
    }
    strcat(txt, "\n\n\nKart contents: ");
    strcat(txt, Memory.KartContents ());
    strcat(txt, "\nHeader ROM Size: ");
    strcat(txt, Memory.Size());
    strcat(txt, "\nHeader Checksum: ");
    sprintf(temp, "%04X", Memory.ROMChecksum);
    strcat(txt, temp);
    sprintf(temp, "\nCRC32:\t%08X", Memory.ROMCRC32);
    strcat(txt, temp);
}