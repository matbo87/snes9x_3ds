#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include <iostream>
#include <sstream>

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <3ds.h>

#include <dirent.h>
#include "snes9x.h"
#include "memmap.h"
#include "apu.h"
#include "gfx.h"
#include "snapshot.h"
#include "cheats.h"
#include "soundux.h"

#include "3dsexit.h"
#include "3dsgpu.h"
#include "3dsopt.h"
#include "3dssound.h"
#include "3dsmenu.h"
#include "3dsui.h"
#include "3dsfont.h"
#include "3dsconfig.h"
#include "3dsfiles.h"
#include "3dsinput.h"
#include "3dssettings.h"
#include "3dsimpl.h"
#include "3dsimpl_tilecache.h"
#include "3dsimpl_gpu.h"

#include "lodepng.h"

inline std::string operator "" s(const char* s, unsigned int length) {
    return std::string(s, length);
}

S9xSettings3DS settings3DS;
ScreenSettings screenSettings;

#define TICKS_PER_SEC (268123480)
#define TICKS_PER_FRAME_NTSC (4468724)
#define TICKS_PER_FRAME_PAL (5362469)

int frameCount60 = 60;
u64 frameCountTick = 0;
int framesSkippedCount = 0;
char romFileName[_MAX_PATH];
char romFileNameLastSelected[_MAX_PATH];
bool screenSwapped = false;
bool screenImageHidden;

char* hotkeysData[HOTKEYS_COUNT][3];

void setSecondScreenContent(bool newRomLoaded) {
    if (settings3DS.SecondScreenContent == CONTENT_IMAGE) {
        ui3dsRenderScreenImage(screenSettings.SecondScreen, S9xGetFilename("/cover.png"), newRomLoaded || screenImageHidden);
        screenImageHidden = false;
    } 
    else {
        screenImageHidden = true;
        menu3dsDrawBlackScreen();
        if (settings3DS.SecondScreenContent == CONTENT_INFO)
            menu3dsSetRomInfo();
            
        gfxSwapBuffers();
    }
}

void LoadDefaultSettings() {
    settings3DS.PaletteFix = 1;
    settings3DS.SRAMSaveInterval = 2;
    settings3DS.ForceSRAMWriteOnPause = 0;
    for (int i = 0; i < HOTKEYS_COUNT; ++i)
        settings3DS.ButtonHotkeys[i].SetSingleMapping(0);
}

bool ResetHotkeyIfNecessary(int index, bool cpadBindingEnabled) {
    if (!cpadBindingEnabled)
        return false;

    ::ButtonMapping<1>& val = settings3DS.UseGlobalEmuControlKeys ? settings3DS.GlobalButtonHotkeys[index] : settings3DS.ButtonHotkeys[index];
    if (val.MappingBitmasks[0] == static_cast<int>(KEY_CPAD_UP) ||
        val.MappingBitmasks[0] == static_cast<int>(KEY_CPAD_DOWN) ||
        val.MappingBitmasks[0] == static_cast<int>(KEY_CPAD_LEFT) ||
        val.MappingBitmasks[0] == static_cast<int>(KEY_CPAD_RIGHT)) {
        val.SetSingleMapping(0);
        return true;
    }
    return false;
}


//----------------------------------------------------------------------
// Menu options
//----------------------------------------------------------------------

namespace {
    template <typename T>
    bool CheckAndUpdate( T& oldValue, const T& newValue, bool& changed ) {
        if ( oldValue != newValue ) {
            oldValue = newValue;
            changed = true;
            return true;
        }
        return false;
    }

    void AddMenuAction(std::vector<SMenuItem>& items, const std::string& text, std::function<void(int)> callback) {
        items.emplace_back(callback, MenuItemType::Action, text, ""s);
    }

    void AddMenuDialogOption(std::vector<SMenuItem>& items, int value, const std::string& text, const std::string& description = ""s) {
        items.emplace_back(nullptr, MenuItemType::Action, text, description, value);
    }

    void AddMenuDisabledOption(std::vector<SMenuItem>& items, const std::string& text) {
        items.emplace_back(nullptr, MenuItemType::Disabled, text, ""s);
    }

    void AddMenuHeader1(std::vector<SMenuItem>& items, const std::string& text) {
        items.emplace_back(nullptr, MenuItemType::Header1, text, ""s);
    }

    void AddMenuHeader2(std::vector<SMenuItem>& items, const std::string& text) {
        items.emplace_back(nullptr, MenuItemType::Header2, text, ""s);
    }

    void AddMenuCheckbox(std::vector<SMenuItem>& items, const std::string& text, int value, std::function<void(int)> callback, int elementId = -1) {
        items.emplace_back(callback, MenuItemType::Checkbox, text, ""s, value, 0, elementId);
    }

    void AddMenuRadio(std::vector<SMenuItem>& items, const std::string& text, int value, int radioGroupId, int elementId, std::function<void(int)> callback) {
        items.emplace_back(callback, MenuItemType::Radio, text, ""s, value, radioGroupId, elementId);
    }

    void AddMenuGauge(std::vector<SMenuItem>& items, const std::string& text, int min, int max, int value, std::function<void(int)> callback) {
        items.emplace_back(callback, MenuItemType::Gauge, text, ""s, value, min, max);
    }

    void AddMenuPicker(std::vector<SMenuItem>& items, const std::string& text, const std::string& description, const std::vector<SMenuItem>& options, int value, int backgroundColor, bool showSelectedOptionInMenu, std::function<void(int)> callback, int id = -1) {
        items.emplace_back(callback, MenuItemType::Picker, text, ""s, value, showSelectedOptionInMenu ? 1 : 0, id, description, options, backgroundColor);
    }
}

void exitEmulatorOptionSelected( int val ) {
    if ( val == 1 ) {
        GPU3DS.emulatorState = EMUSTATE_END;
        appExiting = 1;
    }
}

std::vector<SMenuItem> makeOptionsForNoYes() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0, "No"s, ""s);
    AddMenuDialogOption(items, 1, "Yes"s, ""s);
    return items;
}

std::vector<SMenuItem> makeOptionsForOk() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0, "OK"s, ""s);
    return items;
}

std::vector<SMenuItem> makeEmulatorMenu(std::vector<SMenuTab>& menuTab, int& currentMenuTab, bool& closeMenu) {
    std::vector<SMenuItem> items;
    AddMenuHeader2(items, "Resume"s);
    items.emplace_back([&closeMenu](int val) {
        closeMenu = true;
    }, MenuItemType::Action, "  Resume Game"s, ""s);
    AddMenuHeader2(items, ""s);

    int groupId = 500; // necessary for radio group

    AddMenuHeader2(items, "Savestates"s);

    for (int slot = 1; slot <= SAVESLOTS_MAX; ++slot) {
        std::ostringstream optionText;
        int state = impl3dsGetSlotState(slot);
        optionText << "  Save Slot #" << slot;

        AddMenuRadio(items, optionText.str(), state, groupId, groupId + slot,
            [slot, groupId, &menuTab, &currentMenuTab](int val) {
                SMenuTab dialogTab;
                SMenuTab *currentTab = &menuTab[currentMenuTab];
                bool isDialog = false;
                bool result;

                if (val != RADIO_ACTIVE_CHECKED)
                    return;
                
                std::ostringstream oss;
                oss << "Saving into slot #" << slot << "...";
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Savestates", oss.str(), DIALOGCOLOR_CYAN, std::vector<SMenuItem>());
                result = impl3dsSaveStateSlot(slot);
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

                if (!result) {
                    std::ostringstream oss;
                    oss << "Unable to save into #" << slot << "!";
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Savestate failure", oss.str(), DIALOGCOLOR_RED, makeOptionsForOk());
                    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
                }
                else
                {
                    std::ostringstream oss;
                    oss << "Slot " << slot << " save completed.";
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Savestate complete.", oss.str(), DIALOGCOLOR_GREEN, makeOptionsForOk());
                    if (CheckAndUpdate( settings3DS.CurrentSaveSlot, slot, settings3DS.Changed )) {
                        for (int i = 0; i < currentTab->MenuItems.size(); i++)
                        {
                            // abuse GaugeMaxValue for element id to update state
                            // load slot: change MenuItemType::Disabled to Action
                            if (currentTab->MenuItems[i].Type == MenuItemType::Disabled && currentTab->MenuItems[i].GaugeMaxValue == groupId + slot) 
                            {
                                currentTab->MenuItems[i].Type = MenuItemType::Action;
                                break;
                            }
                        }
                    }
                }
            }
        );
    }
    AddMenuHeader2(items, ""s);
    
    for (int slot = 1; slot <= SAVESLOTS_MAX; ++slot) {
        std::ostringstream optionText;
        int state = impl3dsGetSlotState(slot);

        optionText << "  Load Slot #" << slot;
        items.emplace_back([slot, &menuTab, &currentMenuTab, &closeMenu](int val) {
            bool result = impl3dsLoadStateSlot(slot);
            if (!result) {
                SMenuTab dialogTab;
                bool isDialog = false;
                std::ostringstream oss;
                oss << "Unable to load slot #" << slot << "!";
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Savestate failure", oss.str(), DIALOGCOLOR_RED, makeOptionsForOk());
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
            } else {
                CheckAndUpdate( settings3DS.CurrentSaveSlot, slot, settings3DS.Changed );
                closeMenu = true;
            }
        }, (state == RADIO_INACTIVE || state == RADIO_INACTIVE_CHECKED) ? MenuItemType::Disabled : MenuItemType::Action, optionText.str(), ""s, -1, groupId, groupId + slot);
    }
    AddMenuHeader2(items, ""s);

    AddMenuHeader2(items, "Others"s);
    
    items.emplace_back([&menuTab, &currentMenuTab, &closeMenu](int val) {
        SMenuTab dialogTab;
        bool isDialog = false;
        int result = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Swap Game Screen", "Are you sure?", DIALOGCOLOR_RED, makeOptionsForNoYes());
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
        if (result == 1) {
            ui3dsUpdateScreenSettings(screenSettings.GameScreen == GFX_TOP ? GFX_BOTTOM : GFX_TOP);
            settings3DS.Changed = true;
            screenSwapped = true;
            closeMenu = true;
        }
    }, MenuItemType::Action, "  Swap Game Screen"s, ""s);

    items.emplace_back([&menuTab, &currentMenuTab](int val) {
        SMenuTab dialogTab;
        bool isDialog = false;
        menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Screenshot", "Now taking a screenshot...\nThis may take a while.", DIALOGCOLOR_CYAN, std::vector<SMenuItem>());

        char ext[256];
        const char *path = NULL;

        // Loop through and look for an non-existing
        // file name.
        //
        int i = 1;
        while (i <= 999)
        {
            snprintf(ext, 255, "/img%03d.png", i);
            path = S9xGetFilename(ext);
            if (!IsFileExists(path))
                break;
            path = NULL;
            i++;
        }

        bool success = false;
        if (path)
        {
            success = menu3dsTakeScreenshot(path);
        }
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

        if (success)
        {
            char text[600];
            snprintf(text, 600, "Done! File saved to %s", path);
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Screenshot", text, DIALOGCOLOR_GREEN, makeOptionsForOk());
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
        }
        else 
        {
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Screenshot", "Oops. Unable to take screenshot!", DIALOGCOLOR_RED, makeOptionsForOk());
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
        }
    }, MenuItemType::Action, "  Screenshot"s, ""s);

    items.emplace_back([&menuTab, &currentMenuTab, &closeMenu](int val) {
        SMenuTab dialogTab;
        bool isDialog = false;
        int result = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Reset Console", "Are you sure?", DIALOGCOLOR_RED, makeOptionsForNoYes());
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

        if (result == 1) {
            impl3dsResetConsole();
            closeMenu = true;
        }
    }, MenuItemType::Action, "  Reset"s, ""s);

    AddMenuPicker(items, "  Exit"s, "Leaving so soon?", makeOptionsForNoYes(), 0, DIALOGCOLOR_RED, false, exitEmulatorOptionSelected);

    return items;
}

std::vector<SMenuItem> makeOptionsForFont() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0, "Tempesta"s, ""s);
    AddMenuDialogOption(items, 1, "Ronda"s,    ""s);
    AddMenuDialogOption(items, 2, "Arial"s,    ""s);
    return items;
}

std::vector<SMenuItem> makeOptionsForStretch() {
    std::vector<SMenuItem> items;
    std::string fsDescription = "Stretch to 400x240";
    std::string croppedFsDescription = "Crop & Stretch to 400x240";
    int fsIndex = 2;
    int cfsIndex = 4;

    AddMenuDialogOption(items, 0, "No Stretch"s,              "'Pixel Perfect'"s);
    // AddMenuDialogOption(items, 7, "Expand to Fit"s,           "'Pixel Perfect' fit"s);
    AddMenuDialogOption(items, 6, "TV-style"s,                "Stretch width only to 292px"s);
    if (screenSettings.GameScreen == GFX_TOP) {
        AddMenuDialogOption(items, 1, "4:3 Fit"s,                 "Stretch to 320x240"s);
        AddMenuDialogOption(items, 3, "Cropped 4:3 Fit"s,         "Crop & Stretch to 320x240"s);
    } 
    else {
        fsDescription = "Stretch to 320x240";
        croppedFsDescription = "Crop & Stretch to 320x240";
        if (settings3DS.ScreenStretch == 1)
            fsIndex =  1;
        if (settings3DS.ScreenStretch == 3)
            cfsIndex = 3;
    }
    AddMenuDialogOption(items, fsIndex, "Fullscreen"s,              fsDescription);
    AddMenuDialogOption(items, cfsIndex, "Cropped Fullscreen"s,      croppedFsDescription);
    
    return items;
}

std::vector<SMenuItem> makeOptionsforSecondScreen() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 1, "Game Image"s, ""s);
    AddMenuDialogOption(items, 2, "Game Info"s,    ""s);
    AddMenuDialogOption(items, 0, "None"s,    ""s);
    return items;
}

std::vector<SMenuItem> makeOptionsForButtonMapping() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0,                      "-"s);
    AddMenuDialogOption(items, SNES_A_MASK,            "SNES A Button"s);
    AddMenuDialogOption(items, SNES_B_MASK,            "SNES B Button"s);
    AddMenuDialogOption(items, SNES_X_MASK,            "SNES X Button"s);
    AddMenuDialogOption(items, SNES_Y_MASK,            "SNES Y Button"s);
    AddMenuDialogOption(items, SNES_TL_MASK,           "SNES L Button"s);
    AddMenuDialogOption(items, SNES_TR_MASK,           "SNES R Button"s);
    AddMenuDialogOption(items, SNES_SELECT_MASK,       "SNES SELECT Button"s);
    AddMenuDialogOption(items, SNES_START_MASK,        "SNES START Button"s);
    
    return items;
}

std::vector<SMenuItem> makeOptionsFor3DSButtonMapping() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0,                                   "-"s);

    
	if(GPU3DS.isNew3DS) {        
        AddMenuDialogOption(items, static_cast<int>(KEY_ZL),            "ZL Button"s);
        AddMenuDialogOption(items, static_cast<int>(KEY_ZR),            "ZR Button"s);
    }

    if ((!settings3DS.UseGlobalButtonMappings && !settings3DS.BindCirclePad || (settings3DS.UseGlobalButtonMappings && !settings3DS.GlobalBindCirclePad))) {
        AddMenuDialogOption(items, static_cast<int>(KEY_CPAD_UP),            "Circle Pad Up"s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CPAD_DOWN),            "Circle Pad Down"s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CPAD_LEFT),            "Circle Pad Left"s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CPAD_RIGHT),            "Circle Pad Right"s);
    }

	if(GPU3DS.isNew3DS) {
        AddMenuDialogOption(items, static_cast<int>(KEY_CSTICK_UP),            "C-stick Up"s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CSTICK_DOWN),            "C-stick Down"s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CSTICK_LEFT),            "C-stick Left"s);
        AddMenuDialogOption(items, static_cast<int>(KEY_CSTICK_RIGHT),            "C-stick Right"s);
    }

    AddMenuDialogOption(items, static_cast<int>(KEY_A),             "3DS A Button"s);
    AddMenuDialogOption(items, static_cast<int>(KEY_B),             "3DS B Button"s);
    AddMenuDialogOption(items, static_cast<int>(KEY_X),             "3DS X Button"s);
    AddMenuDialogOption(items, static_cast<int>(KEY_Y),             "3DS Y Button"s);
    AddMenuDialogOption(items, static_cast<int>(KEY_L),             "3DS L Button"s);
    AddMenuDialogOption(items, static_cast<int>(KEY_R),             "3DS R Button"s);

    return items;
}

std::vector<SMenuItem> makeOptionsForFrameskip() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0, "Disabled"s,                ""s);
    AddMenuDialogOption(items, 1, "Enabled (max 1 frame)"s,   ""s);
    AddMenuDialogOption(items, 2, "Enabled (max 2 frames)"s,   ""s);
    AddMenuDialogOption(items, 3, "Enabled (max 3 frames)"s,   ""s);
    AddMenuDialogOption(items, 4, "Enabled (max 4 frames)"s,   ""s);
    return items;
};

std::vector<SMenuItem> makeOptionsForCirclePad() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0, "Disabled"s,                ""s);
    AddMenuDialogOption(items, 1, "Enabled"s,   ""s);
    return items;
};


std::vector<SMenuItem> makeOptionsForFrameRate() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, static_cast<int>(EmulatedFramerate::UseRomRegion), "Default based on ROM region"s, ""s);
    AddMenuDialogOption(items, static_cast<int>(EmulatedFramerate::ForceFps50),   "50 FPS"s,                      ""s);
    AddMenuDialogOption(items, static_cast<int>(EmulatedFramerate::ForceFps60),   "60 FPS"s,                      ""s);
    AddMenuDialogOption(items, static_cast<int>(EmulatedFramerate::Match3DS),     "Match 3DS refresh rate"s,      ""s);
    return items;
};

std::vector<SMenuItem> makeOptionsForAutoSaveSRAMDelay() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 1, "1 second"s,    "May result in sound and frame skips"s);
    AddMenuDialogOption(items, 2, "10 seconds"s,  ""s);
    AddMenuDialogOption(items, 3, "60 seconds"s,  ""s);
    AddMenuDialogOption(items, 4, "Disabled"s,    "Open Emulator menu to save"s);
    return items;
};

std::vector<SMenuItem> makeOptionsForInFramePaletteChanges() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 1, "Enabled"s,          "Best (not 100% accurate); slower"s);
    AddMenuDialogOption(items, 2, "Disabled Style 1"s, "Faster than \"Enabled\""s);
    AddMenuDialogOption(items, 3, "Disabled Style 2"s, "Faster than \"Enabled\""s);
    return items;
};

std::vector<SMenuItem> makeEmulatorNewMenu() {
    std::vector<SMenuItem> items;
    AddMenuPicker(items, "  Exit"s, "Leaving so soon?", makeOptionsForNoYes(), 0, DIALOGCOLOR_RED, false, exitEmulatorOptionSelected);

    return items;
}

std::vector<SMenuItem> makeOptionMenu(std::vector<SMenuTab>& menuTab, int& currentMenuTab, bool& closeMenu) {
    std::vector<SMenuItem> items;

    AddMenuHeader1(items, "EMULATOR SETTINGS"s);
    AddMenuPicker(items, "  Screen Stretch"s, "How would you like the actual game screen to appear?"s, makeOptionsForStretch(), settings3DS.ScreenStretch, DIALOGCOLOR_CYAN, true,
                  []( int val ) { CheckAndUpdate( settings3DS.ScreenStretch, val, settings3DS.Changed ); });
    
    
    int secondScreenPickerId = 1000;
    AddMenuPicker(items, "  Second Screen Content"s, "What would you like to see on the second screen"s, makeOptionsforSecondScreen(), settings3DS.SecondScreenContent, DIALOGCOLOR_CYAN, true,
                    [secondScreenPickerId, &menuTab, &currentMenuTab]( int val ) { 
                        if (CheckAndUpdate(settings3DS.SecondScreenContent, val, settings3DS.Changed)) {
                            SMenuTab *currentTab = &menuTab[currentMenuTab]; 
                            menu3dsUpdateGaugeVisibility(currentTab, secondScreenPickerId, val != CONTENT_NONE ? OPACITY_STEPS : 0);
                        }
                    }, secondScreenPickerId
                );

    AddMenuGauge(items, "  Second Screen Opacity"s, 1, settings3DS.SecondScreenContent !=  CONTENT_NONE ? OPACITY_STEPS : 0, settings3DS.SecondScreenOpacity,
                    []( int val ) { CheckAndUpdate( settings3DS.SecondScreenOpacity, val, settings3DS.Changed ); });


    int borderCheckboxId = 1500;
    AddMenuCheckbox(items, "  Show Game Border"s, settings3DS.ShowGameBorder,
                    [borderCheckboxId, &menuTab, &currentMenuTab]( int val ) {
                        if (CheckAndUpdate(settings3DS.ShowGameBorder, val, settings3DS.Changed)) {
                            SMenuTab *currentTab = &menuTab[currentMenuTab]; 
                            menu3dsUpdateGaugeVisibility(currentTab, borderCheckboxId, val == 1 ? OPACITY_STEPS : 0);
                        }
                    }, borderCheckboxId
                );

    AddMenuGauge(items, "  Game Border Opacity"s, 1, settings3DS.ShowGameBorder ? OPACITY_STEPS : 0, settings3DS.GameBorderOpacity,
                    []( int val ) { CheckAndUpdate( settings3DS.GameBorderOpacity, val, settings3DS.Changed ); });
    AddMenuCheckbox(items, "  Automatically save state on exit and load state on start"s, settings3DS.AutoSavestate,
                         []( int val ) { CheckAndUpdate( settings3DS.AutoSavestate, val, settings3DS.Changed ); });
    AddMenuDisabledOption(items, ""s);

    AddMenuHeader1(items, "GAME-SPECIFIC SETTINGS"s);
    AddMenuHeader2(items, "Graphics"s);
    AddMenuPicker(items, "  Frameskip"s, "Try changing this if the game runs slow. Skipping frames helps it run faster, but less smooth."s, makeOptionsForFrameskip(), settings3DS.MaxFrameSkips, DIALOGCOLOR_CYAN, true,
                  []( int val ) { CheckAndUpdate( settings3DS.MaxFrameSkips, val, settings3DS.Changed ); });
    AddMenuPicker(items, "  Framerate"s, "Some games run at 50 or 60 FPS by default. Override if required."s, makeOptionsForFrameRate(), static_cast<int>(settings3DS.ForceFrameRate), DIALOGCOLOR_CYAN, true,
                  []( int val ) { CheckAndUpdate( settings3DS.ForceFrameRate, static_cast<EmulatedFramerate>(val), settings3DS.Changed ); });
    AddMenuPicker(items, "  In-Frame Palette Changes"s, "Try changing this if some colours in the game look off."s, makeOptionsForInFramePaletteChanges(), settings3DS.PaletteFix, DIALOGCOLOR_CYAN, true,
                  []( int val ) { CheckAndUpdate( settings3DS.PaletteFix, val, settings3DS.Changed ); });
    AddMenuDisabledOption(items, ""s);

    AddMenuHeader2(items, "SRAM (Save Data)"s);
    AddMenuPicker(items, "  SRAM Auto-Save Delay"s, "Try setting to 60 seconds or Disabled this if the game saves SRAM (Save Data) to SD card too frequently."s, makeOptionsForAutoSaveSRAMDelay(), settings3DS.SRAMSaveInterval, DIALOGCOLOR_CYAN, true,
                  []( int val ) { CheckAndUpdate( settings3DS.SRAMSaveInterval, val, settings3DS.Changed ); });
    AddMenuCheckbox(items, "  Force SRAM Write on Pause"s, settings3DS.ForceSRAMWriteOnPause,
                    []( int val ) { CheckAndUpdate( settings3DS.ForceSRAMWriteOnPause, val, settings3DS.Changed ); });
    AddMenuDisabledOption(items, "    (some games like Yoshi's Island require this)"s);

    AddMenuHeader2(items, ""s);

    AddMenuHeader1(items, "AUDIO"s);
    AddMenuGauge(items, "  Volume Amplification"s, 0, 8, 
                settings3DS.UseGlobalVolume ? settings3DS.GlobalVolume : settings3DS.Volume,
                []( int val ) { 
                    if (settings3DS.UseGlobalVolume)
                        CheckAndUpdate( settings3DS.GlobalVolume, val, settings3DS.Changed ); 
                    else
                        CheckAndUpdate( settings3DS.Volume, val, settings3DS.Changed ); 
                });
    AddMenuCheckbox(items, "  Apply volume to all games"s, settings3DS.UseGlobalVolume,
                []( int val ) 
                { 
                    CheckAndUpdate( settings3DS.UseGlobalVolume, val, settings3DS.Changed ); 
                    if (settings3DS.UseGlobalVolume)
                        settings3DS.GlobalVolume = settings3DS.Volume; 
                    else
                        settings3DS.Volume = settings3DS.GlobalVolume; 
                });
    
    AddMenuDisabledOption(items, ""s);
    AddMenuHeader1(items, "MENU"s);
    AddMenuPicker(items, "  Font"s, "The font used for the user interface."s, makeOptionsForFont(), settings3DS.Font, DIALOGCOLOR_CYAN, true,
                  []( int val ) { if ( CheckAndUpdate( settings3DS.Font, val, settings3DS.Changed ) ) { ui3dsSetFont(val); } });

    return items;
};

std::vector<SMenuItem> makeControlsMenu(std::vector<SMenuTab>& menuTab, int& currentMenuTab, bool& closeMenu) {
    std::vector<SMenuItem> items;
    char *t3dsButtonNames[10];
    t3dsButtonNames[BTN3DS_A] = "3DS A Button";
    t3dsButtonNames[BTN3DS_B] = "3DS B Button";
    t3dsButtonNames[BTN3DS_X] = "3DS X Button";
    t3dsButtonNames[BTN3DS_Y] = "3DS Y Button";
    t3dsButtonNames[BTN3DS_L] = "3DS L Button";
    t3dsButtonNames[BTN3DS_R] = "3DS R Button";

    if (GPU3DS.isNew3DS) {
        t3dsButtonNames[BTN3DS_ZL] = "3DS ZL Button";
        t3dsButtonNames[BTN3DS_ZR] = "3DS ZR Button";
    }

    t3dsButtonNames[BTN3DS_SELECT] = "3DS SELECT Button";
    t3dsButtonNames[BTN3DS_START] = "3DS START Button";

    AddMenuHeader1(items, "EMULATOR INGAME FUNCTIONS"s);


    AddMenuCheckbox(items, "Apply hotkey mappings to all games"s, settings3DS.UseGlobalEmuControlKeys,
                []( int val ) 
                { 
                    CheckAndUpdate( settings3DS.UseGlobalEmuControlKeys, val, settings3DS.Changed ); 
                    if (settings3DS.UseGlobalEmuControlKeys) {
                        for (int i = 0; i < HOTKEYS_COUNT; ++i)
                            settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0] = settings3DS.ButtonHotkeys[i].MappingBitmasks[0];
                    }
                    else {
                        for (int i = 0; i < HOTKEYS_COUNT; ++i)
                            settings3DS.ButtonHotkeys[i].MappingBitmasks[0] = settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0];
                    }
                });

    AddMenuDisabledOption(items, ""s);

    int hotkeyPickerGroupId = 2000;
    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        AddMenuPicker( items,  hotkeysData[i][1], hotkeysData[i][2], makeOptionsFor3DSButtonMapping(), 
            settings3DS.UseGlobalEmuControlKeys ? settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0] : settings3DS.ButtonHotkeys[i].MappingBitmasks[0], DIALOGCOLOR_CYAN, true, 
            [i]( int val ) {
                uint32 v = static_cast<uint32>(val);
                if (settings3DS.UseGlobalEmuControlKeys)
                    CheckAndUpdate( settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0], v, settings3DS.Changed );
                else
                    CheckAndUpdate( settings3DS.ButtonHotkeys[i].MappingBitmasks[0], v, settings3DS.Changed );
            }, hotkeyPickerGroupId
        );
    }

    AddMenuDisabledOption(items, ""s);

    AddMenuHeader1(items, "BUTTON CONFIGURATION"s);
    AddMenuCheckbox(items, "Apply button mappings to all games"s, settings3DS.UseGlobalButtonMappings,
                []( int val ) 
                { 
                    CheckAndUpdate( settings3DS.UseGlobalButtonMappings, val, settings3DS.Changed ); 
                    
                    if (settings3DS.UseGlobalButtonMappings) {
                        for (int i = 0; i < 10; i++)
                            for (int j = 0; j < 4; j++)
                                settings3DS.GlobalButtonMapping[i][j] = settings3DS.ButtonMapping[i][j];
                        settings3DS.GlobalBindCirclePad = settings3DS.BindCirclePad;
                    }
                    else {
                        for (int i = 0; i < 10; i++)
                            for (int j = 0; j < 4; j++)
                                settings3DS.ButtonMapping[i][j] = settings3DS.GlobalButtonMapping[i][j];
                        settings3DS.BindCirclePad = settings3DS.GlobalBindCirclePad;
                    }

                });
    AddMenuCheckbox(items, "Apply rapid fire settings to all games"s, settings3DS.UseGlobalTurbo,
                []( int val ) 
                { 
                    CheckAndUpdate( settings3DS.UseGlobalTurbo, val, settings3DS.Changed ); 
                    if (settings3DS.UseGlobalTurbo) {
                        for (int i = 0; i < 8; i++)
                            settings3DS.GlobalTurbo[i] = settings3DS.Turbo[i];
                    }
                    else {
                        for (int i = 0; i < 8; i++)
                            settings3DS.Turbo[i] = settings3DS.GlobalTurbo[i];
                    }
                });
    
    
    AddMenuHeader2(items, "");
    AddMenuHeader2(items, "Analog to Digital Type"s);
    AddMenuPicker(items, "  Bind Circle Pad to D-Pad"s, "You might disable this option if you're only using the D-Pad for gaming. Circle Pad directions will be available for hotkeys after unbinding."s, 
                makeOptionsForCirclePad(), settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalBindCirclePad : settings3DS.BindCirclePad, DIALOGCOLOR_CYAN, true,
                  [hotkeyPickerGroupId, &closeMenu, &menuTab, &currentMenuTab]( int val ) { 
                    if (CheckAndUpdate(settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalBindCirclePad : settings3DS.BindCirclePad, val, settings3DS.Changed)) {
                        SMenuTab *currentTab = &menuTab[currentMenuTab];
                        int j = 0;
                        for (int i = 0; i < currentTab->MenuItems.size(); i++)
                        {
                            // update/reset hotkey options if bindCirclePad value has changed
                            if (currentTab->MenuItems[i].GaugeMaxValue == hotkeyPickerGroupId) {
                                currentTab->MenuItems[i].PickerItems = makeOptionsFor3DSButtonMapping();
                                if (ResetHotkeyIfNecessary(j, val)) {
                                    currentTab->MenuItems[i].Value = 0;
                                }
                                if (++j > HOTKEYS_COUNT) 
                                    break;
                            }
                        }
                    }
                });
                
    for (size_t i = 0; i < 10; ++i) {
        std::ostringstream optionButtonName;
        optionButtonName << t3dsButtonNames[i];
        AddMenuHeader2(items, "");
        AddMenuHeader2(items, optionButtonName.str());

        for (size_t j = 0; j < 3; ++j) {
            std::ostringstream optionName;
            optionName << "  Maps to";

            AddMenuPicker( items, optionName.str(), ""s, makeOptionsForButtonMapping(), 
                settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalButtonMapping[i][j] : settings3DS.ButtonMapping[i][j], 
                DIALOGCOLOR_CYAN, true,
                [i, j]( int val ) {
                    if (settings3DS.UseGlobalButtonMappings)
                        CheckAndUpdate( settings3DS.GlobalButtonMapping[i][j], val, settings3DS.Changed );
                    else
                        CheckAndUpdate( settings3DS.ButtonMapping[i][j], val, settings3DS.Changed );
                }
            );
        }

        if (i < 8)
            AddMenuGauge(items, "  Rapid-Fire Speed"s, 0, 10, 
                settings3DS.UseGlobalTurbo ? settings3DS.GlobalTurbo[i] : settings3DS.Turbo[i], 
                [i]( int val ) 
                { 
                    if (settings3DS.UseGlobalTurbo)
                        CheckAndUpdate( settings3DS.GlobalTurbo[i], val, settings3DS.Changed ); 
                    else
                        CheckAndUpdate( settings3DS.Turbo[i], val, settings3DS.Changed ); 
                });
        
    }
    return items;
}

void menuSetupCheats(std::vector<SMenuItem>& cheatMenu);

std::vector<SMenuItem> makeCheatMenu() {
    std::vector<SMenuItem> items;
    AddMenuHeader2(items, "Cheats"s);
    menuSetupCheats(items);
    return items;
};


//----------------------------------------------------------------------
// Update settings.
//----------------------------------------------------------------------

bool settingsUpdateAllSettings(bool updateGameSettings = true)
{
    bool settingsChanged = false;

    // update screen stretch
    //
    if (settings3DS.ScreenStretch == 0)
    {
        settings3DS.StretchWidth = 256;
        settings3DS.StretchHeight = -1;    // Actual height
        settings3DS.CropPixels = 0;
    }
    else if (settings3DS.ScreenStretch == 1)
    {
        settings3DS.StretchWidth = 320;
        settings3DS.StretchHeight = SCREEN_HEIGHT;
        settings3DS.CropPixels = 0;
    }
    else if (settings3DS.ScreenStretch == 2)
    {
        settings3DS.StretchWidth = screenSettings.GameScreenWidth;
        settings3DS.StretchHeight = SCREEN_HEIGHT;
        settings3DS.CropPixels = 0;
    }
    else if (settings3DS.ScreenStretch == 3)
    {
        settings3DS.StretchWidth = 320;
        settings3DS.StretchHeight = SCREEN_HEIGHT;
        settings3DS.CropPixels = 8;
    }
    else if (settings3DS.ScreenStretch == 4)
    {
        settings3DS.StretchWidth = screenSettings.GameScreenWidth;
        settings3DS.StretchHeight = SCREEN_HEIGHT;
        settings3DS.CropPixels = 8;
    }
    else if (settings3DS.ScreenStretch == 5)
    {
        settings3DS.StretchWidth = 04030000;       // Stretch width only to 4/3
        settings3DS.StretchHeight = -1;
        settings3DS.CropPixels = 0;
    }
    else if (settings3DS.ScreenStretch == 6)    // TV
    {
        settings3DS.StretchWidth = 292;       
        settings3DS.StretchHeight = -1;
        settings3DS.CropPixels = 0;
    }
    else if (settings3DS.ScreenStretch == 7)    // Stretch h/w but keep 1:1 ratio
    {
        settings3DS.StretchWidth = 01010000;       
        settings3DS.StretchHeight = SCREEN_HEIGHT;
        settings3DS.CropPixels = 0;
    }

    // Update the screen font
    //
    ui3dsSetFont(settings3DS.Font);

    if (updateGameSettings)
    {
        // Update frame rate
        //
        if (Settings.PAL)
            settings3DS.TicksPerFrame = TICKS_PER_FRAME_PAL;
        else
            settings3DS.TicksPerFrame = TICKS_PER_FRAME_NTSC;

        if (settings3DS.ForceFrameRate == EmulatedFramerate::ForceFps50) {
            settings3DS.TicksPerFrame = TICKS_PER_FRAME_PAL;
        } else if (settings3DS.ForceFrameRate == EmulatedFramerate::ForceFps60) {
            settings3DS.TicksPerFrame = TICKS_PER_FRAME_NTSC;
        }

        // update global volume
        //
        if (settings3DS.Volume < 0)
            settings3DS.Volume = 0;
        if (settings3DS.Volume > 8)
            settings3DS.Volume = 8;

        Settings.VolumeMultiplyMul4 = (settings3DS.Volume + 4);
        if (settings3DS.UseGlobalVolume)
        {
            Settings.VolumeMultiplyMul4 = (settings3DS.GlobalVolume + 4);
        }

        // update in-frame palette fix
        //
        if (settings3DS.PaletteFix == 1)
            SNESGameFixes.PaletteCommitLine = -2;
        else if (settings3DS.PaletteFix == 2)
            SNESGameFixes.PaletteCommitLine = 1;
        else if (settings3DS.PaletteFix == 3)
            SNESGameFixes.PaletteCommitLine = -1;
        else
        {
            if (SNESGameFixes.PaletteCommitLine == -2)
                settings3DS.PaletteFix = 1;
            else if (SNESGameFixes.PaletteCommitLine == 1)
                settings3DS.PaletteFix = 2;
            else if (SNESGameFixes.PaletteCommitLine == -1)
                settings3DS.PaletteFix = 3;
            settingsChanged = true;
        }

        if (settings3DS.SRAMSaveInterval == 1)
            Settings.AutoSaveDelay = 60;
        else if (settings3DS.SRAMSaveInterval == 2)
            Settings.AutoSaveDelay = 600;
        else if (settings3DS.SRAMSaveInterval == 3)
            Settings.AutoSaveDelay = 3600;
        else if (settings3DS.SRAMSaveInterval == 4)
            Settings.AutoSaveDelay = -1;
        else
        {
            if (Settings.AutoSaveDelay == 60)
                settings3DS.SRAMSaveInterval = 1;
            else if (Settings.AutoSaveDelay == 600)
                settings3DS.SRAMSaveInterval = 2;
            else if (Settings.AutoSaveDelay == 3600)
                settings3DS.SRAMSaveInterval = 3;
            settingsChanged = true;
        }
        
        // Fixes the Auto-Save timer bug that causes
        // the SRAM to be saved once when the settings were
        // changed to Disabled.
        //
        if (Settings.AutoSaveDelay == -1)
            CPU.AutoSaveTimer = -1;
        else
            CPU.AutoSaveTimer = 0;
    }

    return settingsChanged;
}

namespace {
    void config3dsReadWriteBitmask(const char* name, uint32* bitmask) {
        int tmp = static_cast<int>(*bitmask);
        config3dsReadWriteInt32(name, &tmp, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        *bitmask = static_cast<uint32>(tmp);
    }
}

//----------------------------------------------------------------------
// Read/write all possible game specific settings.
//----------------------------------------------------------------------
bool settingsReadWriteFullListByGame(bool writeMode)
{
    if (!writeMode) {
        // set default values first.
        LoadDefaultSettings();
    }

    bool success = config3dsOpenFile(S9xGetFilename("/rom.cfg"), writeMode);
    if (!success)
        return false;

    config3dsReadWriteInt32("#v1\n", NULL, 0, 0);
    config3dsReadWriteInt32("# Do not modify this file or risk losing your settings.\n", NULL, 0, 0);

    config3dsReadWriteInt32("Frameskips=%d\n", &settings3DS.MaxFrameSkips, 0, 4);
    int tmp = static_cast<int>(settings3DS.ForceFrameRate);
    config3dsReadWriteInt32("Framerate=%d\n", &tmp, 0, static_cast<int>(EmulatedFramerate::Count) - 1);
    settings3DS.ForceFrameRate = static_cast<EmulatedFramerate>(tmp);
    
    config3dsReadWriteInt32("Vol=%d\n", &settings3DS.Volume, 0, 8);
    config3dsReadWriteInt32("PalFix=%d\n", &settings3DS.PaletteFix, 0, 3);
    config3dsReadWriteInt32("SRAMInterval=%d\n", &settings3DS.SRAMSaveInterval, 0, 4);
    config3dsReadWriteInt32("ForceSRAMWrite=%d\n", &settings3DS.ForceSRAMWriteOnPause, 0, 1);
    config3dsReadWriteInt32("BindCirclePad=%d\n", &settings3DS.BindCirclePad, 0, 1);
    config3dsReadWriteInt32("LastSaveSlot=%d\n", &settings3DS.CurrentSaveSlot, 0, 5);

    static char *buttonName[10] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "SELECT","START"};
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::ostringstream oss;
            oss << "ButtonMap" << buttonName[i] << "_" << j << "=%d\n";
            config3dsReadWriteInt32(oss.str().c_str(), &settings3DS.ButtonMapping[i][j]);
        }
    }

    static char *turboButtonName[8] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    for (int i = 0; i < 8; ++i) {
        std::ostringstream oss;
        oss << "Turbo" << turboButtonName[i] << "=%d\n";
        config3dsReadWriteInt32(oss.str().c_str(), &settings3DS.Turbo[i], 0, 10);
    }

    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        if (strlen(hotkeysData[i][0])) {
            std::ostringstream oss;
            oss << "ButtonMapping" << hotkeysData[i][0] << "_0" << "=%d\n";
            config3dsReadWriteBitmask(oss.str().c_str(), &settings3DS.ButtonHotkeys[i].MappingBitmasks[0]);
        }
    }

    config3dsCloseFile();
    return true;
}


//----------------------------------------------------------------------
// Read/write all possible global settings.
//----------------------------------------------------------------------
bool settingsReadWriteFullListGlobal(bool writeMode)
{
    const char *emulatorConfig = "sdmc:/snes9x_3ds_data/snes9x_3ds.cfg";
    bool success = config3dsOpenFile(emulatorConfig, writeMode);
    if (!success)
        return false;
    
    config3dsReadWriteInt32("#v1\n", NULL, 0, 0);
    config3dsReadWriteInt32("# Do not modify this file or risk losing your settings.\n", NULL, 0, 0);
    int screen = static_cast<int>(screenSettings.GameScreen);
    config3dsReadWriteInt32("GameScreen=%d\n", &screen, 0, 1);
    screenSettings.GameScreen = static_cast<gfxScreen_t>(screen);
    config3dsReadWriteInt32("ScreenStretch=%d\n", &settings3DS.ScreenStretch, 0, 7);
    config3dsReadWriteInt32("SecondScreenContent=%d\n", &settings3DS.SecondScreenContent, 0, 2);
    config3dsReadWriteInt32("SecondScreenOpacity=%d\n", &settings3DS.SecondScreenOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteInt32("ShowGameBorder=%d\n", &settings3DS.ShowGameBorder, 0, 1);
    config3dsReadWriteInt32("GameBorderOpacity=%d\n", &settings3DS.GameBorderOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteInt32("Font=%d\n", &settings3DS.Font, 0, 2);

    // Fixes the bug where we have spaces in the directory name
    config3dsReadWriteString("Dir=%s\n", "Dir=%1000[^\n]s\n", file3dsGetCurrentDir());
    config3dsReadWriteString("ROM=%s\n", "ROM=%1000[^\n]s\n", romFileNameLastSelected);

    config3dsReadWriteInt32("AutoSavestate=%d\n", &settings3DS.AutoSavestate, 0, 1);
    config3dsReadWriteInt32("Vol=%d\n", &settings3DS.GlobalVolume, 0, 8);
    config3dsReadWriteInt32("GlobalBindCirclePad=%d\n", &settings3DS.GlobalBindCirclePad, 0, 1);

    static char *buttonName[10] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "SELECT","START"};
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::ostringstream oss;
            oss << "ButtonMap" << buttonName[i] << "_" << j << "=%d\n";
            config3dsReadWriteInt32(oss.str().c_str(), &settings3DS.GlobalButtonMapping[i][j]);
        }
    }
    
    static char *turboButtonName[8] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    for (int i = 0; i < 8; ++i) {
        std::ostringstream oss;
        oss << "Turbo" << turboButtonName[i] << "=%d\n";
        config3dsReadWriteInt32(oss.str().c_str(), &settings3DS.GlobalTurbo[i], 0, 10);
    }

    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        if (strlen(hotkeysData[i][0])) {
            std::ostringstream oss;
            oss << "ButtonMapping" << hotkeysData[i][0] << "_0" << "=%d\n";
            config3dsReadWriteBitmask(oss.str().c_str(), &settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0]);
        }
    }

    config3dsReadWriteInt32("UseGlobalButtonMappings=%d\n", &settings3DS.UseGlobalButtonMappings, 0, 1);
    config3dsReadWriteInt32("UseGlobalTurbo=%d\n", &settings3DS.UseGlobalTurbo, 0, 1);
    config3dsReadWriteInt32("UseGlobalVolume=%d\n", &settings3DS.UseGlobalVolume, 0, 1);
    config3dsReadWriteInt32("UseGlobalEmuControlKeys=%d\n", &settings3DS.UseGlobalEmuControlKeys, 0, 1);

    config3dsCloseFile();
    return true;
}

//----------------------------------------------------------------------
// Save settings by game.
//----------------------------------------------------------------------
bool settingsSave(bool includeGameSettings = true)
{
    //consoleClear();
    //ui3dsDrawRect(50, 140, 270, 154, 0x000000);
    //ui3dsDrawStringWithNoWrapping(50, 140, 270, 154, 0x3f7fff, HALIGN_CENTER, "Saving settings to SD card...");

    if (includeGameSettings)
        settingsReadWriteFullListByGame(true);

    settingsReadWriteFullListGlobal(true);
    //ui3dsDrawRect(50, 140, 270, 154, 0x000000);

    settings3DS.Changed = false;
    return true;
}


//----------------------------------------------------------------------
// Set default buttons mapping
//----------------------------------------------------------------------
void settingsDefaultButtonMapping(int buttonMapping[8][4])
{
    uint32 defaultButtons[] = 
    { SNES_A_MASK, SNES_B_MASK, SNES_X_MASK, SNES_Y_MASK, SNES_TL_MASK, SNES_TR_MASK, 0, 0, SNES_SELECT_MASK, SNES_START_MASK };

    for (int i = 0; i < 10; i++)
    {
        bool allZero = true;

        for (int j = 0; j < 4; j++)
        {
            // Validates all button mapping input,
            // assign to zero, if invalid.
            //
            if (buttonMapping[i][j] != SNES_A_MASK &&
                buttonMapping[i][j] != SNES_B_MASK &&
                buttonMapping[i][j] != SNES_X_MASK &&
                buttonMapping[i][j] != SNES_Y_MASK &&
                buttonMapping[i][j] != SNES_TL_MASK &&
                buttonMapping[i][j] != SNES_TR_MASK &&
                buttonMapping[i][j] != SNES_SELECT_MASK &&
                buttonMapping[i][j] != SNES_START_MASK &&
                buttonMapping[i][j] != 0)
                buttonMapping[i][j] = 0;

            if (buttonMapping[i][j])
                allZero = false;
        }
        if (allZero)
            buttonMapping[i][0] = defaultButtons[i];
    }

}

//----------------------------------------------------------------------
// Load settings by game.
//----------------------------------------------------------------------
bool settingsLoad(bool includeGameSettings = true)
{
    settings3DS.Changed = false;
    bool success = settingsReadWriteFullListGlobal(false);
    if (!success)
        return false;
    settingsUpdateAllSettings(false);

    if (includeGameSettings)
    {
        success = settingsReadWriteFullListByGame(false);

        // Set default button configuration
        //
        settingsDefaultButtonMapping(settings3DS.ButtonMapping);
        settingsDefaultButtonMapping(settings3DS.GlobalButtonMapping);

        if (success)
        {
            if (settingsUpdateAllSettings())
                settingsSave();
            return true;
        }
        else
        {
            // If we can't find the saved settings, always
            // set the frame rate to be based on the ROM's region.
            // For the rest of the settings, we use whatever has been
            // set in the previous game.
            //
            settings3DS.MaxFrameSkips = 1;
            settings3DS.ForceFrameRate = EmulatedFramerate::UseRomRegion;
            settings3DS.Volume = 4;

            for (int i = 0; i < 8; i++)     // and clear all turbo buttons.
                settings3DS.Turbo[i] = 0;

            if (SNESGameFixes.PaletteCommitLine == -2)
                settings3DS.PaletteFix = 1;
            else if (SNESGameFixes.PaletteCommitLine == 1)
                settings3DS.PaletteFix = 2;
            else if (SNESGameFixes.PaletteCommitLine == -1)
                settings3DS.PaletteFix = 3;

            if (Settings.AutoSaveDelay == 60)
                settings3DS.SRAMSaveInterval = 1;
            else if (Settings.AutoSaveDelay == 600)
                settings3DS.SRAMSaveInterval = 2;
            else if (Settings.AutoSaveDelay == 3600)
                settings3DS.SRAMSaveInterval = 3;

            settingsUpdateAllSettings();

            return settingsSave();
        }
    }

    return true;
}




//-------------------------------------------------------
// Load the ROM and reset the CPU.
//-------------------------------------------------------

extern SCheatData Cheat;

void emulatorLoadRom()
{
    char romFileNameFullPath[_MAX_PATH];
    snprintf(romFileNameFullPath, _MAX_PATH, "%s%s", file3dsGetCurrentDir(), romFileName);
    //snprintf(romFileNameFullPath, _MAX_PATH, "%s%s", file3dsGetCurrentDir(), "Donkey Kong Country (E) (G).smc");
    
    bool loaded=impl3dsLoadROM(romFileNameFullPath);
    if(loaded)
    {
        snd3DS.generateSilence = true;
        settingsSave(false);

        GPU3DS.emulatorState = EMUSTATE_EMULATE;
        settingsLoad();
        settingsUpdateAllSettings();
        menu3dsSetCurrentTabPosition(0, 1);
    
        // check for valid hotkeys if circle pad binding is enabled
        if ((!settings3DS.UseGlobalButtonMappings && settings3DS.BindCirclePad) || 
            (settings3DS.UseGlobalButtonMappings && settings3DS.GlobalBindCirclePad))
            for (int i = 0; i < HOTKEYS_COUNT; ++i)
                ResetHotkeyIfNecessary(i, true);
        
        // set proper state (radio_state) for every save slot of loaded game
        for (int slot = 1; slot <= SAVESLOTS_MAX; ++slot)
            impl3dsUpdateSlotState(slot, true);
        
        setSecondScreenContent(true);
        impl3dsSetBorderImage(true);

        if (settings3DS.AutoSavestate)
            impl3dsLoadStateAuto();

        snd3DS.generateSilence = false;

    } else {
        consoleInit(screenSettings.GameScreen, NULL); 
        printf("\n  can't read file:\n  %s", romFileNameFullPath);
    }
}


//----------------------------------------------------------------------
// Load all ROM file names
//----------------------------------------------------------------------
void fileGetAllFiles(std::vector<DirectoryEntry>& romFileNames)
{
    file3dsGetFiles(romFileNames, {"smc", "sfc", "fig"});
}


//----------------------------------------------------------------------
// Find the ID of the last selected file in the file list.
//----------------------------------------------------------------------
int fileFindLastSelectedFile(std::vector<SMenuItem>& fileMenu)
{
    for (int i = 0; i < fileMenu.size() && i < 1000; i++)
    {
        if (strncmp(fileMenu[i].Text.c_str(), romFileNameLastSelected, _MAX_PATH) == 0)
            return i;
    }
    return -1;
}


//----------------------------------------------------------------------
// Handle menu cheats.
//----------------------------------------------------------------------
bool menuCopyCheats(std::vector<SMenuItem>& cheatMenu, bool copyMenuToSettings)
{
    bool cheatsUpdated = false;
    for (int i = 0; (i+1) < cheatMenu.size() && i < MAX_CHEATS && i < Cheat.num_cheats; i++)
    {
        cheatMenu[i+1].Type = MenuItemType::Checkbox;

        //capitalize only first character of words
        for(int j = 0; Cheat.c[i].name[j] != '\0'; j++)
        {
            if(j==0)
            {
                if((Cheat.c[i].name[j]>='a' && Cheat.c[i].name[j]<='z'))
                    Cheat.c[i].name[j]=Cheat.c[i].name[j]-32;
                continue;
            }
            if(Cheat.c[i].name[j]==' ') {
                ++j;
                if(Cheat.c[i].name[j]>='a' && Cheat.c[i].name[j]<='z')
                {
                    Cheat.c[i].name[j]=Cheat.c[i].name[j]-32;
                    continue;
                }
            }
            else
            {
                if(Cheat.c[i].name[j]>='A' && Cheat.c[i].name[j]<='Z')
                    Cheat.c[i].name[j]=Cheat.c[i].name[j]+32;
            }
        }
        
        cheatMenu[i+1].Text = Cheat.c[i].name;

        if (copyMenuToSettings)
        {
            if (Cheat.c[i].enabled != cheatMenu[i+1].Value)
            {
                Cheat.c[i].enabled = cheatMenu[i+1].Value;
                if (Cheat.c[i].enabled)
                    S9xEnableCheat(i);
                else
                    S9xDisableCheat(i);
                cheatsUpdated = true;
            }
        }
        else
            cheatMenu[i+1].SetValue(Cheat.c[i].enabled);
    }
    
    return cheatsUpdated;
}


void fillFileMenuFromFileNames(std::vector<SMenuItem>& fileMenu, const std::vector<DirectoryEntry>& romFileNames, const DirectoryEntry*& selectedEntry) {
    fileMenu.clear();
    fileMenu.reserve(romFileNames.size());

    for (size_t i = 0; i < romFileNames.size(); ++i) {
        const DirectoryEntry& entry = romFileNames[i];
        fileMenu.emplace_back( [&entry, &selectedEntry]( int val ) {
            selectedEntry = &entry;
        }, MenuItemType::Action, entry.Filename, ""s, 99999);
    }
}

//----------------------------------------------------------------------
// Start up menu.
//----------------------------------------------------------------------
void setupBootupMenu(std::vector<SMenuTab>& menuTab, std::vector<DirectoryEntry>& romFileNames, const DirectoryEntry*& selectedDirectoryEntry, bool selectPreviousFile) {
    menuTab.clear();
    menuTab.reserve(2);

    {
        menu3dsAddTab(menuTab, "Emulator", makeEmulatorNewMenu());
        menuTab.back().SubTitle.clear();
    }

    {
        std::vector<SMenuItem> fileMenu;
        fileGetAllFiles(romFileNames);
        fillFileMenuFromFileNames(fileMenu, romFileNames, selectedDirectoryEntry);
        menu3dsAddTab(menuTab, "Select ROM", fileMenu);
        menuTab.back().SubTitle.assign(file3dsGetCurrentDir());
        if (selectPreviousFile) {
            int previousFileID = fileFindLastSelectedFile(menuTab.back().MenuItems);
            menu3dsSetSelectedItemByIndex(menuTab.back(), previousFileID);
        }
    }
}

std::vector<DirectoryEntry> romFileNames; // needs to stay in scope, is there a better way?

void menuSelectFile(void)
{
    std::vector<SMenuTab> menuTab;
    const DirectoryEntry* selectedDirectoryEntry = nullptr;
    setupBootupMenu(menuTab, romFileNames, selectedDirectoryEntry, true);

    int currentMenuTab = 1;
    bool isDialog = false;
    SMenuTab dialogTab;
    
    gfxSetDoubleBuffering(screenSettings.SecondScreen, true);
    menu3dsSetTransferGameScreen(false);

    bool animateMenu = true;
    while (!appExiting) {
        menu3dsShowMenu(dialogTab, isDialog, currentMenuTab, menuTab, animateMenu);
        animateMenu = false;

        if (selectedDirectoryEntry) {
            if (selectedDirectoryEntry->Type == FileEntryType::File) {
                strncpy(romFileName, selectedDirectoryEntry->Filename.c_str(), _MAX_PATH);
                strncpy(romFileNameLastSelected, romFileName, _MAX_PATH);
                menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTab);
                emulatorLoadRom();
                return;
            } else if (selectedDirectoryEntry->Type == FileEntryType::ParentDirectory || selectedDirectoryEntry->Type == FileEntryType::ChildDirectory) {
                file3dsGoUpOrDownDirectory(*selectedDirectoryEntry);
                setupBootupMenu(menuTab, romFileNames, selectedDirectoryEntry, false);
            }
            selectedDirectoryEntry = nullptr;
        }
    }

    menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTab);
}


//----------------------------------------------------------------------
// Menu when the emulator is paused in-game.
//----------------------------------------------------------------------
void setupPauseMenu(std::vector<SMenuTab>& menuTab, std::vector<DirectoryEntry>& romFileNames, const DirectoryEntry*& selectedDirectoryEntry, bool selectPreviousFile, int& currentMenuTab, bool& closeMenu, bool refreshFileList) {
    menuTab.clear();
    menuTab.reserve(4);

    {
        menu3dsAddTab(menuTab, "Emulator", makeEmulatorMenu(menuTab, currentMenuTab, closeMenu));
        menuTab.back().SubTitle.clear();
    }

    {
        menu3dsAddTab(menuTab, "Options", makeOptionMenu(menuTab, currentMenuTab, closeMenu));
        menuTab.back().SubTitle.clear();
    }

    {
        menu3dsAddTab(menuTab, "Controls", makeControlsMenu(menuTab, currentMenuTab, closeMenu));
        menuTab.back().SubTitle.clear();
    }

    {
        menu3dsAddTab(menuTab, "Cheats", makeCheatMenu());
        menuTab.back().SubTitle.clear();
    }

    {
        std::vector<SMenuItem> fileMenu;
        if (refreshFileList)
            fileGetAllFiles(romFileNames);
        fillFileMenuFromFileNames(fileMenu, romFileNames, selectedDirectoryEntry);
        menu3dsAddTab(menuTab, "Select ROM", fileMenu);
        menuTab.back().SubTitle.assign(file3dsGetCurrentDir());
        if (selectPreviousFile) {
            int previousFileID = fileFindLastSelectedFile(menuTab.back().MenuItems);
            menu3dsSetSelectedItemByIndex(menuTab.back(), previousFileID);
        }
    }
}

void menuPause()
{
    gspWaitForVBlank();
    gfxSetScreenFormat(screenSettings.SecondScreen, GSP_RGB565_OES);
    
    int currentMenuTab;
    int lastItemIndex;
    menu3dsGetCurrentTabPosition(currentMenuTab, lastItemIndex);

    bool closeMenu = false;
    std::vector<SMenuTab> menuTab;

    const DirectoryEntry* selectedDirectoryEntry = nullptr;
    setupPauseMenu(menuTab, romFileNames, selectedDirectoryEntry, true, currentMenuTab, closeMenu, false);

    if (menuTab[currentMenuTab].Title != "Select ROM")
        menu3dsSetSelectedItemByIndex(menuTab[currentMenuTab], lastItemIndex);

    bool isDialog = false;
    SMenuTab dialogTab;

    gfxSetDoubleBuffering(screenSettings.SecondScreen, true);
    menu3dsSetTransferGameScreen(true);

    bool loadRomBeforeExit = false;

    std::vector<SMenuItem>& cheatMenu = menuTab[3].MenuItems;
    menuCopyCheats(cheatMenu, false);

    bool animateMenu = true;
    while (!appExiting && !closeMenu) {
        if (menu3dsShowMenu(dialogTab, isDialog, currentMenuTab, menuTab, animateMenu) < 0) {
            // user pressed B, close menu
            closeMenu = true;
        }
        animateMenu = false;

        if (selectedDirectoryEntry) {
            // Load ROM
            if (selectedDirectoryEntry->Type == FileEntryType::File) {
                bool loadRom = true;
                if (settings3DS.AutoSavestate) {
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Save State", "Autosaving...", DIALOGCOLOR_CYAN, std::vector<SMenuItem>());
                    bool result = impl3dsSaveStateAuto();
                    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

                    if (!result) {
                        int choice = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Autosave failure", "Automatic savestate writing failed.\nLoad chosen game anyway?", DIALOGCOLOR_RED, makeOptionsForNoYes());
                        if (choice != 1) {
                            loadRom = false;
                        }
                    }
                }

                if (loadRom) {
                    strncpy(romFileName, selectedDirectoryEntry->Filename.c_str(), _MAX_PATH);
                    strncpy(romFileNameLastSelected, romFileName, _MAX_PATH);
                    loadRomBeforeExit = true;
                    break;
                }
            } else if (selectedDirectoryEntry->Type == FileEntryType::ParentDirectory || selectedDirectoryEntry->Type == FileEntryType::ChildDirectory) {
                file3dsGoUpOrDownDirectory(*selectedDirectoryEntry);
                setupPauseMenu(menuTab, romFileNames, selectedDirectoryEntry, false, currentMenuTab, closeMenu, true);
            }
            selectedDirectoryEntry = nullptr;
        }
    }
    
    // don't hide menu before user releases key
    // this is necessary to prevent input reading from the game
    
    u32 thisKeysUp = 0;
    while (aptMainLoop())
    {   
        hidScanInput();
        thisKeysUp = hidKeysUp();
        if (thisKeysUp)
            break;
        gspWaitForVBlank();
    }

    menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTab);

    // Save settings and cheats
    //
    if (settings3DS.Changed)
        settingsSave();
    settingsUpdateAllSettings();

    if (menuCopyCheats(cheatMenu, true))
    {
        // Only one of these will succeeed.
        S9xSaveCheatFile (S9xGetFilename("/rom.cht"));
        S9xSaveCheatTextFile (S9xGetFilename("/rom.chx"));
    }

    if (closeMenu) {
        GPU3DS.emulatorState = EMUSTATE_EMULATE;
        
        if (screenSwapped) {
            gfxSetScreenFormat(screenSettings.GameScreen, GSP_RGBA8_OES);
            screenSwapped = false;
        }
        if (!loadRomBeforeExit) {
            setSecondScreenContent(false);
            impl3dsSetBorderImage(false);
        }
    }

    // Loads the new ROM if a ROM was selected.
    //
    if (loadRomBeforeExit)
        emulatorLoadRom();

}

//-------------------------------------------------------
// Sets up all the cheats to be displayed in the menu.
//-------------------------------------------------------
char *noCheatsText[] {
    "",
    "No cheats available for this game. ",
    "",
    "To enable cheats:  ",
    "copy your rom.cht or rom.chx file to the path"
     };

void menuSetupCheats(std::vector<SMenuItem>& cheatMenu)
{
    if (Cheat.num_cheats > 0)
    {
        for (int i = 0; i < MAX_CHEATS && i < Cheat.num_cheats; i++)
        {
            cheatMenu.emplace_back(nullptr, MenuItemType::Checkbox, std::string(Cheat.c[i].name), ""s, Cheat.c[i].enabled ? 1 : 0);
        }
    }
    else
    {
        for (int i = 0; i < 5; i++)
        {
            cheatMenu.emplace_back(nullptr, MenuItemType::Disabled, std::string(noCheatsText[i]), ""s);
        }
        
        static char message[PATH_MAX + 1];
        snprintf(message, PATH_MAX + 1, S9xGetFilename("/"));
        cheatMenu.emplace_back(nullptr, MenuItemType::Disabled, std::string(message), ""s);   
    }
}

//--------------------------------------------------------
// Initialize the emulator engine and everything else.
// This calls the impl3dsInitializeCore, which executes
// initialization code specific to the emulation core.
//--------------------------------------------------------
void emulatorInitialize()
{
    //printf ("\n  Initializing...\n\n");
    DIR* d = opendir("sdmc:/snes9x_3ds_data");
    if (d)
        closedir(d);
    else
        mkdir("sdmc:/snes9x_3ds_data", 0777);

    file3dsInitialize();

    menu3dsSetHotkeysData(hotkeysData);
    settingsLoad(false);
    ui3dsUpdateScreenSettings(screenSettings.GameScreen);

    romFileNameLastSelected[0] = 0;

    if (!gpu3dsInitialize())
    {
        printf ("Unable to initialize GPU\n");
        exit(0);
    }

    if (!impl3dsInitializeCore())
    {
        printf ("Unable to initialize emulator core\n");
        exit(0);
    }

    if (!snd3dsInitialize())
    {
        printf ("Unable to initialize CSND\n");
        exit (0);
    }

    ui3dsInitialize();

    if (romfsInit()!=0)
    {
        //printf ("Unable to initialize romfs\n");
        settings3DS.RomFsLoaded = false;
    }
    else
    {
        settings3DS.RomFsLoaded = true;
    }
    
    //printf ("  Initialization complete\n");

    osSetSpeedupEnable(1);    // Performance: use the higher clock speed for new 3DS.

    enableAptHooks();

    // Do this one more time.
    if (file3dsGetCurrentDir()[0] == 0)
        file3dsInitialize();

    srvInit();
    
}
//--------------------------------------------------------
// Finalize the emulator.
//--------------------------------------------------------
void emulatorFinalize()
{
    consoleClear();
    impl3dsFinalize();
	ui3dsResetScreenImage();

#ifndef RELEASE
    printf("gspWaitForP3D:\n");
#endif
    gspWaitForVBlank();
    gpu3dsWaitForPreviousFlush();
    gspWaitForVBlank();

#ifndef RELEASE
    printf("snd3dsFinalize:\n");
#endif
    snd3dsFinalize();

#ifndef RELEASE
    printf("gpu3dsFinalize:\n");
#endif
    gpu3dsFinalize();

#ifndef RELEASE
    printf("ptmSysmExit:\n");
#endif
    ptmSysmExit ();

    if (settings3DS.RomFsLoaded)
    {
        //printf("romfsExit:\n");
        romfsExit();
    }
    
#ifndef RELEASE
    printf("hidExit:\n");
#endif
	hidExit();
    
#ifndef RELEASE
    printf("aptExit:\n");
#endif
	aptExit();
    
#ifndef RELEASE
    printf("srvExit:\n");
#endif
	srvExit();
}


//---------------------------------------------------------
// Counts the number of frames per second, and prints
// it to the second screen every 60 frames.
//---------------------------------------------------------

char frameCountBuffer[70];
int secondsCount = 0;

void updateSecondScreenContent()
{
    if (frameCountTick == 0)
        frameCountTick = svcGetSystemTick();

    if (frameCount60 == 0)
    {
        u64 newTick = svcGetSystemTick();
        float timeDelta = ((float)(newTick - frameCountTick))/TICKS_PER_SEC;
        int fpsmul10 = (int)((float)600 / timeDelta);

        if (framesSkippedCount)
            snprintf (frameCountBuffer, 69, "FPS: %2d.%1d (%d skipped)", fpsmul10 / 10, fpsmul10 % 10, framesSkippedCount);
        else
            snprintf (frameCountBuffer, 69, "FPS: %2d.%1d", fpsmul10 / 10, fpsmul10 % 10);

        if (settings3DS.SecondScreenContent == CONTENT_INFO) {
            float alpha = (float)(settings3DS.SecondScreenOpacity) / OPACITY_STEPS;
            menu3dsSetFpsInfo(0xffffff, alpha, frameCountBuffer);
        }
        
        frameCount60 = 60;
        framesSkippedCount = 0;
        secondsCount++;


#if !defined(RELEASE) && !defined(DEBUG_CPU) && !defined(DEBUG_APU)
        printf ("\n\n");
        for (int i=0; i<100; i++)
        {
            t3dsShowTotalTiming(i);
        }
        t3dsResetTimings();
#endif
        frameCountTick = newTick;
    }

    frameCount60--;

    // start counter & wait 2 seconds until hiding secondScreenDialog 
    // (there is probably a better way to do this)

    if (secondScreenDialog.State == VISIBLE) {
        secondsCount = 0;
        secondScreenDialog.State = WAIT;
    }

    if (secondScreenDialog.State == WAIT && secondsCount >= 2) {
        secondScreenDialog.State = HIDDEN;
        setSecondScreenContent(false);
    }
}





//----------------------------------------------------------
// This is the main emulation loop. It calls the 
//    impl3dsRunOneFrame
//   (which must be implemented for any new core)
// for the execution of the frame.
//----------------------------------------------------------
void emulatorLoop()
{
	// Main loop
    //GPU3DS.enableDebug = true;

    int snesFramesSkipped = 0;
    long snesFrameTotalActualTicks = 0;
    long snesFrameTotalAccurateTicks = 0;

    bool firstFrame = true;
    appSuspended = 0;

    snd3DS.generateSilence = false;

    gpu3dsResetState();

    frameCount60 = 60;
    frameCountTick = 0;
    framesSkippedCount = 0;

    long startFrameTick = svcGetSystemTick();

    bool skipDrawingFrame = false;
    gfxSetDoubleBuffering(screenSettings.SecondScreen, false);

    snd3dsStartPlaying();

	while (true)
	{
        t3dsStartTiming(1, "aptMainLoop");

        startFrameTick = svcGetSystemTick();
        aptMainLoop();

        if (appExiting || appSuspended)
            break;

        gpu3dsStartNewFrame();

        updateSecondScreenContent();

        if (GPU3DS.emulatorState != EMUSTATE_EMULATE)
            break;

    	input3dsScanInputForEmulation();
        impl3dsRunOneFrame(firstFrame, skipDrawingFrame);

        firstFrame = false; 

        // This either waits for the next frame, or decides to skip
        // the rendering for the next frame if we are too slow.
        //
#ifndef RELEASE
        if (GPU3DS.isReal3DS)
#endif
        {

            long currentTick = svcGetSystemTick();
            long actualTicksThisFrame = currentTick - startFrameTick;

            snesFrameTotalActualTicks += actualTicksThisFrame;  // actual time spent rendering past x frames.
            snesFrameTotalAccurateTicks += settings3DS.TicksPerFrame;  // time supposed to be spent rendering past x frames.

            int isSlow = 0;


            long skew = snesFrameTotalAccurateTicks - snesFrameTotalActualTicks;

            if (skew < 0)
            {
                // We've skewed out of the actual frame rate.
                // Once we skew beyond 0.1 (10%) frames slower, skip the frame.
                //
                if (skew < -settings3DS.TicksPerFrame/10 && snesFramesSkipped < settings3DS.MaxFrameSkips)
                {
                    skipDrawingFrame = true;
                    snesFramesSkipped++;

                    framesSkippedCount++;   // this is used for the stats display every 60 frames.
                }
                else
                {
                    skipDrawingFrame = false;

                    if (snesFramesSkipped >= settings3DS.MaxFrameSkips)
                    {
                        snesFramesSkipped = 0;
                        snesFrameTotalActualTicks = actualTicksThisFrame;
                        snesFrameTotalAccurateTicks = settings3DS.TicksPerFrame;
                    }
                }
            }
            else
            {

                float timeDiffInMilliseconds = (float)skew * 1000000 / TICKS_PER_SEC;

                // Reset the counters.
                //
                snesFrameTotalActualTicks = 0;
                snesFrameTotalAccurateTicks = 0;
                snesFramesSkipped = 0;

                if (
                    (!settings3DS.UseGlobalEmuControlKeys && settings3DS.ButtonHotkeys[HOTKEY_DISABLE_FRAMELIMIT].IsHeld(input3dsGetCurrentKeysHeld())) ||
                    (settings3DS.UseGlobalEmuControlKeys && settings3DS.GlobalButtonHotkeys[HOTKEY_DISABLE_FRAMELIMIT].IsHeld(input3dsGetCurrentKeysHeld())) 
                    ) 
                {
                    skipDrawingFrame = (frameCount60 % 2) == 0;
                }
                else
                {
                    if (settings3DS.ForceFrameRate == EmulatedFramerate::Match3DS) {
                        gspWaitForVBlank();
                    } else {
                        svcSleepThread ((long)(timeDiffInMilliseconds * 1000));
                    }
                    skipDrawingFrame = false;
                }
            }

        }

	}

    snd3dsStopPlaying();
}


//---------------------------------------------------------
// Main entrypoint.
//---------------------------------------------------------
int main()
{
    emulatorInitialize();
    const char* startScreenImage = settings3DS.RomFsLoaded ? "romfs:/start-screen.png" : "sdmc:/snes9x_3ds_data/start-screen.png";
    ui3dsRenderScreenImage(screenSettings.GameScreen, startScreenImage, true);
    gfxSetDoubleBuffering(screenSettings.GameScreen, false); // prevents start screen image flickering
    menuSelectFile();
    while (true)
    {
        if (appExiting)
            goto quit;

        switch (GPU3DS.emulatorState)
        {
            case EMUSTATE_PAUSEMENU:
                menuPause();
                break;

            case EMUSTATE_EMULATE:
                emulatorLoop();
                break;

            case EMUSTATE_END:
                goto quit;

        }

    }

quit:
    clearScreen(screenSettings.SecondScreen);
    gfxSetScreenFormat(screenSettings.SecondScreen, GSP_RGB565_OES);
    gfxSwapBuffersGpu();
    menu3dsDrawBlackScreen();

    if (GPU3DS.emulatorState > 0 && settings3DS.AutoSavestate)
        impl3dsSaveStateAuto();

    //printf("emulatorFinalize:\n");
    emulatorFinalize();
    //printf ("Exiting...\n");
	exit(0);
}
