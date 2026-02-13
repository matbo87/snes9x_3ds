#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>
#include <sys/stat.h>

#include <unistd.h>
#include <dirent.h>

#include <3ds.h>

#include "snes9x.h"
#include "cheats.h"
#include "memmap.h"

#include "3dssettings.h"
#include "3dslog.h"
#include "3dstimer.h"
#include "3dsexit.h"
#include "3dsconfig.h"
#include "3dsfiles.h"
#include "3dsinput.h"
#include "3dssound.h"
#include "3dsgpu.h"
#include "3dsimpl.h"
#include "3dsui.h"
#include "3dsui_img.h"
#include "3dsmenu.h"

inline std::string operator "" s(const char* s, size_t length) {
    return std::string(s, length);
}




int frameCount = 0;
int frameCount60 = 60;
u64 frameCountTick = 0;
int framesSkippedCount = 0;

// wait maxFramesForDialog before hiding dialog message
// (60 frames = 1 second)
int maxFramesForDialog = 60; 

char romFileName[NAME_MAX + 1];
bool slotLoaded = false;

char* hotkeysData[HOTKEYS_COUNT][3];

static bool cfgFileAvailable[2]; // global config, game config
static u32 lastLoadedRomCRC = 0;
static const DirectoryEntry* selectedDirectoryEntry = nullptr;

// static globals to prevent heap fragmentation and speed up menu access.
// note: not thread-safe, but safe here due to sequential menu/emulator execution
static std::vector<SMenuTab> menuTab;
static std::vector<DirectoryEntry> romFileNames;

extern SCheatData Cheat;

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
    bool CheckAndUpdate( T& oldValue, const T& newValue ) {
        if (oldValue != newValue) {
            settings3DS.isDirty = true;
            oldValue = newValue;
            return true;
        }
        return false;
    }

    bool CheckAndUpdateToggle( SettingToggle& oldValue, const int& newValue ) {
        return CheckAndUpdate(oldValue, (SettingToggle)newValue);
    }

    void AddMenuDialogOption(std::vector<SMenuItem>& items, int value, const std::string& text, const std::string& description = ""s) {
        items.emplace_back(nullptr, MenuItemType::Action, text, description, value);
    }

    void AddMenuDisabledOption(std::vector<SMenuItem>& items, const std::string& text, int value = -1) {
        items.emplace_back(nullptr, MenuItemType::Disabled, text, ""s, value);
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

    void AddMenuPicker(std::vector<SMenuItem>& items, const std::string& text, const std::string& description, const std::vector<SMenuItem>& options, int value, int dialogType, bool showSelectedOptionInMenu, std::function<void(int)> callback, int id = -1) {
        items.emplace_back(callback, MenuItemType::Picker, text, ""s, value, showSelectedOptionInMenu ? 1 : 0, id, description, options, dialogType);
    }
}

void exitEmulatorOptionSelected( int val ) {
    if ( val == 1 ) {
        GPU3DS.emulatorState = EMUSTATE_END;
    }
}

std::vector<SMenuItem> makePickerOptions(const std::vector<std::string>& options) {
    std::vector<SMenuItem> items;
    items.reserve(options.size());

    for (int i = 0; i < options.size(); i++) {
        AddMenuDialogOption(items, i, options[i], ""s);
    }

    return items;
}

std::vector<SMenuItem> makeOptionsForResetConfig() {
    std::vector<SMenuItem> items;
    items.reserve(4);

    AddMenuDialogOption(items, 0, "None"s, ""s);

    if (cfgFileAvailable[0]) {
        AddMenuDialogOption(items, 1, "Global"s, "settings.cfg"s);
    }
     
    if (cfgFileAvailable[1]) {
        char gameConfigFilename[128];
        char basename[NAME_MAX + 1];
        file3dsGetBasename(Memory.ROMFilename, basename, sizeof(basename), false);

        if (strlen(basename) > 44) {
            snprintf(gameConfigFilename, sizeof(gameConfigFilename), "%.44s...%s", basename, ".cfg");
        } else {
            snprintf(gameConfigFilename, sizeof(gameConfigFilename), "%s%s", basename, ".cfg");
        }

        AddMenuDialogOption(items, 2, "Game"s, gameConfigFilename);
    }

    if (cfgFileAvailable[0] && cfgFileAvailable[1]) {
        AddMenuDialogOption(items, 3, "Both"s, ""s);
    }
    
    return items;
}

const std::vector<SMenuItem>& makeOptionsForOk() {
    static std::vector<SMenuItem> items;

    if (items.empty()) {
        items.reserve(1);
        const char* options[] = { "OK" };
        AddMenuDialogOption(items, 0, options[0], ""s);
    }

    return items;
}

const std::vector<SMenuItem>& makeOptionsForGameThumbnail() {
    static std::vector<SMenuItem> items;

    if (items.empty()) {
        const char* options[] = { "None", "Boxart", "Title", "Gameplay" };
        int count = MAX_THUMB_TYPES + 1;
        items.reserve(count);
        
        AddMenuDialogOption(items, 0, options[0], "");
        
        for (int i = 1; i < count; i++) {
            char typeName[32];
            snprintf(typeName, sizeof(typeName), "%s", options[i]);
            typeName[0] = (char)(tolower(typeName[0]));

            if (file3dsThumbnailsAvailableByType(typeName)) {
                AddMenuDialogOption(items, i, options[i], "");
            } else {
                AddMenuDisabledOption(items, options[i]);
            }
        }
    }

    return items;
}

std::vector<SMenuItem> makeOptionsForFileMenu(const std::vector<std::string>& options, bool hasDeleteGameOption) {
    std::vector<SMenuItem> items;
    items.reserve(options.size());

    for (int i = 0; i < options.size(); i++) {
        if (i == 0) {
            // option "set default directory"
            if (strcmp(settings3DS.defaultDir, file3dsGetCurrentDir()) != 0) {
                AddMenuDialogOption(items, i, options[i], ""s);
            }
        }
        else if (i == 1) {
            // option "reset default directory"
            if (settings3DS.defaultDir[0]) {
                std::string defaulDirLabel =  std::string(settings3DS.defaultDir);
                size_t maxChars = 28;

                if (defaulDirLabel.length() > maxChars) {
                    defaulDirLabel = "..." + defaulDirLabel.substr(defaulDirLabel.length() - maxChars, maxChars);
                }

                AddMenuDialogOption(items, i, options[i], defaulDirLabel);
            }
        } 
        else if (i == 2) {
            // option "select random game"
            if (file3dsGetCurrentDirRomCount() > 1) {
                AddMenuDialogOption(items, i, options[i], ""s);
            }
        } else if (i == 3) {
            // option "delete game"
            if (hasDeleteGameOption) {
                AddMenuDialogOption(items, i, options[i], ""s);
            }
        }
    }

    return items;
}

bool confirmDialog(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab, std::vector<SMenuTab>& menuTab, const std::string& title, const std::string& message, bool hideDialog = true) {
    int result = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, title, message, Themes[settings3DS.Theme].dialogColorWarn, makePickerOptions({ "No", "Yes" }));

    if (hideDialog) {
        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
    }

    return result == 1;
}

void makeEmulatorMenu(std::vector<SMenuItem>& items, std::vector<SMenuTab>& menuTab, int& currentMenuTab, bool isGameLoaded) {
    items.clear();

    if (isGameLoaded) {
        AddMenuHeader1(items, "CURRENT GAME"s);
        items.emplace_back([](int val) {        
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }, MenuItemType::Action, "  Resume"s, ""s);


        items.emplace_back([&menuTab, &currentMenuTab](int val) {
            SMenuTab dialogTab;
            bool isDialog = false;
            bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Reset Console", "This will restart the game. Are you sure?");

            if (confirmed) {
                impl3dsResetConsole();
                GPU3DS.emulatorState = EMUSTATE_EMULATE;
            }
        }, MenuItemType::Action, "  Reset"s, ""s);

        items.emplace_back([&menuTab, &currentMenuTab](int val) {
            SMenuTab dialogTab;
            bool isDialog = false;
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Screenshot", "Saving screenshot...", Themes[settings3DS.Theme].dialogColorInfo, std::vector<SMenuItem>());

            char path[PATH_MAX];

            if (impl3dsTakeScreenshot(path, sizeof(path), true))
            {
                char message[PATH_MAX];
                snprintf(message, sizeof(message), "Screenshot saved to %s", path);
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Screenshot", message, Themes[settings3DS.Theme].dialogColorSuccess, makeOptionsForOk(), -1, false);
            }
            else
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Screenshot", "Failed to save screenshot!", Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk(), -1, false);
                        
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

        }, MenuItemType::Action, "  Take Screenshot"s, ""s);

        AddMenuHeader2(items, ""s);

        int groupId = 500; // necessary for radio group

        AddMenuHeader2(items, "Save and Load"s);
        AddMenuHeader2(items, ""s);

        char slotInfo[32];

        for (int slot = 1; slot <= SAVESLOTS_MAX; ++slot) {
            int state = impl3dsGetSlotState(slot);
            snprintf(slotInfo, sizeof(slotInfo), "  Save Slot #%d", slot);

            AddMenuRadio(items, slotInfo, state, groupId, groupId + slot,
                [slot, state, groupId, &menuTab, &currentMenuTab](int val) {
                    SMenuTab dialogTab;
                    SMenuTab *currentTab = &menuTab[currentMenuTab];
                    bool isDialog = false;
                    bool result;

                    if (val != RADIO_ACTIVE_CHECKED)
                        return;

                    bool stateUsed = state == RADIO_ACTIVE || state == RADIO_ACTIVE_CHECKED;
                    if (stateUsed) {
                        char confirmMessage[64];
                        snprintf(confirmMessage, sizeof(confirmMessage), "Are you sure to overwrite save slot #%d?", slot);
                        bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Savestates", confirmMessage, false);

                        if (!confirmed) {
                            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

                            return;
                        }
                    }
                    
                    char statusMessage[64];
                    snprintf(statusMessage, sizeof(statusMessage), "Saving into slot #%d", slot);

                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Savestates", statusMessage, Themes[settings3DS.Theme].dialogColorInfo, std::vector<SMenuItem>(), -1, !stateUsed);
                    result = impl3dsSaveStateSlot(slot);

                    if (!result) {
                        snprintf(statusMessage, sizeof(statusMessage), "Saving into slot #%d failed.", slot);
                        menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Savestates", statusMessage, Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk(), -1, false);
                        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
                    }
                    else
                    {
                        snprintf(statusMessage, sizeof(statusMessage), "Saving into slot #%d completed.", slot);
                        menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Savestates", statusMessage, Themes[settings3DS.Theme].dialogColorSuccess, makeOptionsForOk(), -1, false);
                        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
                        if (CheckAndUpdate( settings3DS.CurrentSaveSlot, slot )) {
                            for (int i = 0; i < currentTab->MenuItems.size(); i++)
                            {
                                // workaround: use GaugeMaxValue for element id to update state
                                // load slot: change MenuItemType::Disabled to Action
                                // TODO: find a better approach to update state
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
            int state = impl3dsGetSlotState(slot);
            snprintf(slotInfo, sizeof(slotInfo), "  Load Slot #%d", slot);

            items.emplace_back([slot, &menuTab, &currentMenuTab](int val) {
                bool result = impl3dsLoadStateSlot(slot);
                if (!result) {
                    SMenuTab dialogTab;
                    bool isDialog = false;
                    
                    char errorMessage[64];
                    snprintf(errorMessage, sizeof(errorMessage), "Unable to load slot #%d!", slot);

                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Savestate failure", errorMessage, Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk());
                    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
                } else {
                    CheckAndUpdate( settings3DS.CurrentSaveSlot, slot );
                    slotLoaded = true;
                    GPU3DS.emulatorState = EMUSTATE_EMULATE;
                }
            }, (state == RADIO_INACTIVE || state == RADIO_INACTIVE_CHECKED) ? MenuItemType::Disabled : MenuItemType::Action, slotInfo, ""s, -1, groupId, groupId + slot);
        }
        AddMenuHeader2(items, ""s);
    }

    AddMenuHeader1(items, "APPEARANCE"s);

    char gameThumbnailMessage[512] = "Type of thumbnails to display in \"Load Game\" tab.";

    if (file3dsThumbnailsAvailable()) {
    } else {
        size_t len = strlen(gameThumbnailMessage);
        snprintf(gameThumbnailMessage + len, sizeof(gameThumbnailMessage) - len, 
            "\nNo thumbnails found. You can download them on \ngithub.com/matbo87/snes9x_3ds-assets");
    }

    AddMenuPicker(items, "  Game Thumbnail"s, "Type of thumbnails to display in \"Load Game\" tab."s, makeOptionsForGameThumbnail(), settings3DS.GameThumbnailType, DIALOG_TYPE_INFO, true,
        []( int val ) { 
            if (!CheckAndUpdate(settings3DS.GameThumbnailType, (SettingThumbnailMode)val)) {
                return;
            }
            
            img3dsSetThumbMode();
        });

    std::vector<std::string>themeNames;

    for (int i = 0; i < TOTALTHEMECOUNT; i++) {
        themeNames.emplace_back(std::string(Themes[i].Name));
    }

    AddMenuPicker(items, "  Theme"s, "The theme used for the user interface."s, makePickerOptions(themeNames), settings3DS.Theme, DIALOG_TYPE_INFO, true,
        []( int val ) { CheckAndUpdate(settings3DS.Theme, (SettingTheme)val); });


    AddMenuPicker(items, "  Font"s, "The font used for the user interface."s, makePickerOptions({"Tempesta", "Ronda", "Arial"}), settings3DS.Font, DIALOG_TYPE_INFO, true,
        []( int val ) { if ( CheckAndUpdate( settings3DS.Font, (SettingFont)val ) ) { ui3dsSetFont(); } });

    AddMenuPicker(items, "  Game Screen"s, "Play your games on top or bottom screen"s, makePickerOptions({"Top", "Bottom"}), settings3DS.GameScreen, DIALOG_TYPE_INFO, true,
        [&menuTab, &currentMenuTab]( int val ) { 
            if (!CheckAndUpdate(settings3DS.GameScreen, (gfxScreen_t)val)) {
                return;
            }

            SMenuTab dialogTab;
            bool isDialog = false;
            
            // set primaryScreenDirty because changed settings3DS.GameScreen requires a framebuffer format update
            menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTab);
            ui3dsSetScreenLayout();
            log3dsWrite("screen swapped");
        });

    AddMenuCheckbox(items, "  Disable 3D Slider"s, settings3DS.Disable3DSlider,
        []( int val ) { CheckAndUpdateToggle( settings3DS.Disable3DSlider, val ); });

    AddMenuDisabledOption(items, ""s);

    AddMenuHeader1(items, "OTHERS"s);

    AddMenuCheckbox(items, "  Enable Logging (use when issues occur)"s, settings3DS.LogFileEnabled,
        []( int val ) { CheckAndUpdateToggle( settings3DS.LogFileEnabled, val ); });
    std::string logfileInfo = "  Creates a session log in \"3ds/snes9x_3ds\". Restart required";
    AddMenuDisabledOption(items, logfileInfo);
    AddMenuDisabledOption(items, ""s);

    if (cfgFileAvailable[0] || cfgFileAvailable[1]) {
        items.emplace_back([&menuTab, &currentMenuTab](int val) {
            char resetConfigDescription[NAME_MAX + 1];
            snprintf(
                resetConfigDescription, sizeof(resetConfigDescription), 
                "Restore default settings%s.", 
                (cfgFileAvailable[1] ? " and/or remove current game config" : "")
            );
            
            SMenuTab dialogTab;
            bool isDialog = false;
            int option = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Reset config"s, resetConfigDescription, Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForResetConfig());
            
            // "None" selected or B pressed
            if (option <= 0) {
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

                return;
            }

            // 1=Global, 2=Game, 3=Both
            bool resetGlobal = (option == 1 || option == 3);
            bool resetGame   = (option == 2 || option == 3);

            if (resetGlobal) {
                settings3dsResetGlobalDefaults();
                cfgFileAvailable[0] = false;

                // set primaryScreenDirty flag 
                // because settings like settings3DS.GameScreen might have changed 
                // which requires a framebuffer format update (BGR8 -> RGB565)
                menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTab); 
                ui3dsSetFont(); 
                ui3dsSetScreenLayout();
            }

            if (resetGame) {
                settings3dsResetGameDefaults();
                cfgFileAvailable[1] = false;
            }

            settings3dsUpdate(resetGame);
            settings3DS.isDirty = true;
            settings3DS.uiNeedsRebuild = true;
        }, MenuItemType::Action, "  Reset Config"s, ""s);
    }

    AddMenuPicker(items, "  Quit Emulator"s, "Are you sure you want to quit?", makePickerOptions({ "No", "Yes" }), 0, DIALOG_TYPE_WARN, false, exitEmulatorOptionSelected);

    AddMenuHeader2(items, ""s);
    std::string info = std::string(settings3dsGetAppVersion("  Snes9x for 3DS v")) + " \x0b7 github.com/matbo87/snes9x_3ds";
    AddMenuDisabledOption(items, info);
}

const std::vector<SMenuItem>& makeOptionsForOnScreenDisplay() {
    static std::vector<SMenuItem> items;

    if (items.empty()) {
        items.reserve(4);
        AddMenuDialogOption(items, SettingAssetMode_None, "None"s,              ""s);
        AddMenuDialogOption(items, SettingAssetMode_Default, "Standard"s,              "Uses _default.png, fallback to internal image"s);
        AddMenuDialogOption(items, SettingAssetMode_Adaptive, "Adaptive"s,              "Uses <game>.png first, fallback to Standard"s);
        AddMenuDialogOption(items, SettingAssetMode_CustomOnly, "Custom Only"s,              "Uses <game>.png only, no fallback"s);
    }
    
    return items;
}

std::vector<SMenuItem> makeOptionsForStretch() {
    std::vector<SMenuItem> items;
    items.reserve(8);

    AddMenuDialogOption(items, SettingScreenStretch_None, "No Stretch"s,              "Pixel Perfect (256x224)"s);
    AddMenuDialogOption(items, SettingScreenStretch_4_3_Aspect, "4:3 Aspect"s,              "Stretch width only to 298"s);
    AddMenuDialogOption(items, SettingScreenStretch_CrtAspect, "CRT Aspect"s,              "Stretch width only to 292 (8:7 PAR)"s);
    AddMenuDialogOption(items, SettingScreenStretch_4_3_Fit, "4:3 Fit"s,                 "Stretch to 320x240"s);
    AddMenuDialogOption(items, SettingScreenStretch_8_7_Fit, "8:7 Fit"s,                 "Stretch to 274x240"s);
    AddMenuDialogOption(items, SettingScreenStretch_4_3_Fit_Cropped, "Cropped 4:3 Fit"s,         "Crop & Stretch to 320x240"s);

    if (settings3DS.GameScreen == GFX_TOP) {
        AddMenuDialogOption(items, SettingScreenStretch_Full, "Fullscreen"s,              "Stretch to 400x240");
        AddMenuDialogOption(items, SettingScreenStretch_Full_Cropped, "Cropped Fullscreen"s,      "Crop & Stretch to 400x240");
    }
    
    return items;
}


const std::vector<SMenuItem>& makeOptionsForButtonMapping() {
    static std::vector<SMenuItem> items;

    if (items.empty()) {
        items.reserve(9);
        
        AddMenuDialogOption(items, 0,                      "-"s);
        AddMenuDialogOption(items, SNES_A_MASK,            "SNES A Button"s);
        AddMenuDialogOption(items, SNES_B_MASK,            "SNES B Button"s);
        AddMenuDialogOption(items, SNES_X_MASK,            "SNES X Button"s);
        AddMenuDialogOption(items, SNES_Y_MASK,            "SNES Y Button"s);
        AddMenuDialogOption(items, SNES_TL_MASK,           "SNES L Button"s);
        AddMenuDialogOption(items, SNES_TR_MASK,           "SNES R Button"s);
        AddMenuDialogOption(items, SNES_SELECT_MASK,       "SNES SELECT Button"s);
        AddMenuDialogOption(items, SNES_START_MASK,        "SNES START Button"s);
    }
    
    return items;
}

std::vector<SMenuItem> makeOptionsFor3DSButtonMapping() {
    std::vector<SMenuItem> items;
    items.reserve(17);

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

const std::vector<SMenuItem>& makeOptionsForFrameRate() {
    static std::vector<SMenuItem> items;
    if (items.empty()) {
        items.reserve(4);
        AddMenuDialogOption(items, static_cast<int>(SettingFramerate_UseRomRegion), "Default based on Game region"s, ""s);
        AddMenuDialogOption(items, static_cast<int>(SettingFramerate_ForceFps50), "50 FPS"s, ""s);
        AddMenuDialogOption(items, static_cast<int>(SettingFramerate_ForceFps60), "60 FPS"s, ""s);
        AddMenuDialogOption(items, static_cast<int>(SettingFramerate_Match3DS), "Match 3DS refresh rate"s, ""s);
    }
    return items;
}

const std::vector<SMenuItem>& makeOptionsForAutoSaveSRAMDelay() {
    static std::vector<SMenuItem> items;
    if (items.empty()) {
        items.reserve(4);
        AddMenuDialogOption(items, 1, "1 second"s,   "May result in sound- and frameskips"s);
        AddMenuDialogOption(items, 2, "10 seconds"s, ""s);
        AddMenuDialogOption(items, 3, "60 seconds"s, ""s);
        AddMenuDialogOption(items, 4, "Disabled"s,   ""s);
    }
    return items;
}

const std::vector<SMenuItem>& makeOptionsForInFramePaletteChanges() {
    static std::vector<SMenuItem> items;
    if (items.empty()) {
        items.reserve(3);
        AddMenuDialogOption(items, 1, "Enabled"s,          "Best (not 100% accurate); slower"s);
        AddMenuDialogOption(items, 2, "Disabled Style 1"s, "Faster than \"Enabled\""s);
        AddMenuDialogOption(items, 3, "Disabled Style 2"s, "Faster than \"Enabled\""s);
    }
    return items;
}

void makeOptionMenu(std::vector<SMenuItem>& items, std::vector<SMenuTab>& menuTab, int& currentMenuTab) {
    items.clear();

    AddMenuHeader1(items, "GENERAL SETTINGS"s);
    AddMenuHeader2(items, "Video"s);
    AddMenuPicker(items, "  Scaling"s, "Change video scaling settings"s, makeOptionsForStretch(), settings3DS.ScreenStretch, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.ScreenStretch, (SettingScreenStretch)val ); });

    AddMenuDisabledOption(items, ""s);
    AddMenuHeader2(items, "On-Screen Display"s);

    AddMenuPicker(items, "  Bezel"s, "Shown in front of game screen. Usage for custom image:\nmax 506x256px, path = \"/3ds/snes9x3ds/bezels/\",\nfilename = trimmed ROM (e.g. NBA Jam.png) or _default.png"s, 
        makeOptionsForOnScreenDisplay(), settings3DS.GameBezel, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.GameBezel, (SettingAssetMode)val ); });

    AddMenuCheckbox(items, "  Auto-Fit Bezel (based on \"Video Scaling\")", settings3DS.GameBezelAutoFit,
        []( int val ) { CheckAndUpdateToggle( settings3DS.GameBezelAutoFit, val ); });



    int gameBorderPickerId = 1500;
    AddMenuPicker(items, "  Border"s, "Shown behind game screen. Usage for custom image:\nmax 400x240px, path = \"/3ds/snes9x3ds/borders/\",\nfilename = trimmed ROM (e.g. NBA Jam.png) or _default.png"s, 
        makeOptionsForOnScreenDisplay(), settings3DS.GameBorder, DIALOG_TYPE_INFO, true,
                    [gameBorderPickerId, &menuTab, &currentMenuTab]( int val ) { 
                        if (CheckAndUpdate(settings3DS.GameBorder, (SettingAssetMode)val)) {
                            SMenuTab *currentTab = &menuTab[currentMenuTab]; 
                            menu3dsUpdateGaugeVisibility(currentTab, gameBorderPickerId, val > 0 ? OPACITY_STEPS : GAUGE_DISABLED_VALUE);
                        }
                    }, gameBorderPickerId
                );

    AddMenuGauge(items, "  Border Opacity"s, 1, settings3DS.GameBorder > 0 ? OPACITY_STEPS : GAUGE_DISABLED_VALUE, settings3DS.GameBorderOpacity,
                    []( int val ) { CheckAndUpdate( settings3DS.GameBorderOpacity, val ); });
                        
    int secondScreenPickerId = 1000;
    AddMenuPicker(items, "  Cover"s, "Shown on second screen. Usage for custom image:\nmax 400x240px, path = \"/3ds/snes9x3ds/covers/\"\nfilename = trimmed ROM (e.g. NBA Jam.png) or _default.png"s, 
        makeOptionsForOnScreenDisplay(), settings3DS.SecondScreenContent, DIALOG_TYPE_INFO, true,
                    [secondScreenPickerId, &menuTab, &currentMenuTab]( int val ) { 
                        if (CheckAndUpdate(settings3DS.SecondScreenContent, (SettingAssetMode)val)) {
                            SMenuTab *currentTab = &menuTab[currentMenuTab]; 
                            menu3dsUpdateGaugeVisibility(currentTab, secondScreenPickerId, val != SettingAssetMode_None ? OPACITY_STEPS : GAUGE_DISABLED_VALUE);
                        }
                    }, secondScreenPickerId
                );

    AddMenuGauge(items, "  Cover Opacity"s, 1, settings3DS.SecondScreenContent !=  SettingAssetMode_None ? OPACITY_STEPS :GAUGE_DISABLED_VALUE, settings3DS.SecondScreenOpacity,
                    []( int val ) { CheckAndUpdate( settings3DS.SecondScreenOpacity, val ); });
        
    AddMenuDisabledOption(items, ""s);

    AddMenuHeader1(items, "GAME-SPECIFIC SETTINGS"s);
    AddMenuHeader2(items, "Video"s);
    AddMenuPicker(items, "  Frameskip"s, "Try changing this if the game runs slow. Skipping frames helps it run faster, but less smooth."s, 
        makePickerOptions({"Disabled", "Enabled (max 1 frame)", "Enabled (max 2 frames)", "Enabled (max 3 frames)", "Enabled (max 4 frames)"}), settings3DS.MaxFrameSkips, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.MaxFrameSkips, val ); });
    AddMenuPicker(items, "  Framerate"s, "Some games run at 50 or 60 FPS by default. Override if required."s, makeOptionsForFrameRate(), static_cast<int>(settings3DS.ForceFrameRate), DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.ForceFrameRate, (SettingFramerate)val ); });
    AddMenuPicker(items, "  In-Frame Palette Changes"s, "Try changing this if some colors in the game look off."s, makeOptionsForInFramePaletteChanges(), settings3DS.PaletteFix, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.PaletteFix, val ); });
    
    AddMenuDisabledOption(items, ""s);

    AddMenuHeader2(items, "Audio"s);
    AddMenuGauge(items, "  Volume Amplification"s, 0, 8, 
                settings3DS.UseGlobalVolume ? settings3DS.GlobalVolume : settings3DS.Volume,
                []( int val ) { 
                    if (settings3DS.UseGlobalVolume)
                        CheckAndUpdate( settings3DS.GlobalVolume, val ); 
                    else
                        CheckAndUpdate( settings3DS.Volume, val ); 
                });
    AddMenuCheckbox(items, "  Apply volume to all games"s, settings3DS.UseGlobalVolume,
                []( int val ) 
                { 
                    CheckAndUpdateToggle( settings3DS.UseGlobalVolume, val ); 
                    if (settings3DS.UseGlobalVolume)
                        settings3DS.GlobalVolume = settings3DS.Volume; 
                    else
                        settings3DS.Volume = settings3DS.GlobalVolume; 
                });
    
    AddMenuDisabledOption(items, ""s);

    AddMenuHeader2(items, "Save Data"s);

    AddMenuCheckbox(items, "  Automatically save state on exit and load state on start"s, settings3DS.AutoSavestate,
        []( int val ) { CheckAndUpdateToggle( settings3DS.AutoSavestate, val ); });
    items.emplace_back(nullptr, MenuItemType::Textarea, "  (creates an *.auto.frz file inside \"savestates\" directory)"s, ""s);

    AddMenuPicker(items, "  SRAM Auto-Save Delay"s, "Try 60 seconds or Disabled if the game saves SRAM to SD card too frequently."s, makeOptionsForAutoSaveSRAMDelay(), settings3DS.SRAMSaveInterval, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.SRAMSaveInterval, val ); });
    AddMenuCheckbox(items, "  Force SRAM Write on Pause"s, settings3DS.ForceSRAMWriteOnPause,
                    []( int val ) { CheckAndUpdateToggle( settings3DS.ForceSRAMWriteOnPause, val ); });

    items.emplace_back(nullptr, MenuItemType::Textarea, "  (some games like Yoshi's Island require this)"s, ""s);
};

void makeControlsMenu(std::vector<SMenuItem>& items, std::vector<SMenuTab>& menuTab, int& currentMenuTab) {
    items.clear();

    const char *t3dsButtonNames[10];
    t3dsButtonNames[BTN3DS_A] = "3DS A Button";
    t3dsButtonNames[BTN3DS_B] = "3DS B Button";
    t3dsButtonNames[BTN3DS_X] = "3DS X Button";
    t3dsButtonNames[BTN3DS_Y] = "3DS Y Button";
    t3dsButtonNames[BTN3DS_L] = "3DS L Button";
    t3dsButtonNames[BTN3DS_R] = "3DS R Button";
    t3dsButtonNames[BTN3DS_ZL] = "3DS ZL Button";
    t3dsButtonNames[BTN3DS_ZR] = "3DS ZR Button";
    t3dsButtonNames[BTN3DS_SELECT] = "3DS SELECT Button";
    t3dsButtonNames[BTN3DS_START] = "3DS START Button";

    AddMenuHeader1(items, "EMULATOR INGAME FUNCTIONS"s);


    AddMenuCheckbox(items, "  Apply hotkey mappings to all games"s, settings3DS.UseGlobalEmuControlKeys,
                []( int val ) 
                { 
                    CheckAndUpdateToggle( settings3DS.UseGlobalEmuControlKeys, val ); 
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
            settings3DS.UseGlobalEmuControlKeys ? settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0] : settings3DS.ButtonHotkeys[i].MappingBitmasks[0], DIALOG_TYPE_INFO, true, 
            [i]( int val ) {
                if (settings3DS.UseGlobalEmuControlKeys)
                    CheckAndUpdate( settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0], (u32)val );
                else
                    CheckAndUpdate( settings3DS.ButtonHotkeys[i].MappingBitmasks[0], (u32)val );
            }, hotkeyPickerGroupId
        );
    }

    AddMenuDisabledOption(items, ""s);

    AddMenuHeader1(items, "BUTTON CONFIGURATION"s);
    AddMenuCheckbox(items, "  Apply button mappings to all games"s, settings3DS.UseGlobalButtonMappings,
                []( int val ) 
                { 
                    CheckAndUpdateToggle( settings3DS.UseGlobalButtonMappings, val ); 
                    
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
    AddMenuCheckbox(items, "  Apply rapid fire settings to all games"s, settings3DS.UseGlobalTurbo,
                []( int val ) 
                { 
                    CheckAndUpdateToggle( settings3DS.UseGlobalTurbo, val ); 
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
                makePickerOptions({"Disabled", "Enabled"}), settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalBindCirclePad : settings3DS.BindCirclePad, DIALOG_TYPE_INFO, true,
                  [hotkeyPickerGroupId, &menuTab, &currentMenuTab]( int val ) { 
                    if (CheckAndUpdateToggle(settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalBindCirclePad : settings3DS.BindCirclePad, val)) {
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
        // skip option for ZL and ZR button when device is O3DS/O2DS
        if ((i == BTN3DS_ZL || i == BTN3DS_ZR) && !GPU3DS.isNew3DS) {
            continue;
        }

        std::string optionButtonName = std::string(t3dsButtonNames[i]);
        AddMenuHeader2(items, "");
        AddMenuHeader2(items, optionButtonName);

        for (size_t j = 0; j < 3; ++j) {
            AddMenuPicker( items, "  Maps to"s, ""s, makeOptionsForButtonMapping(), 
                settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalButtonMapping[i][j] : settings3DS.ButtonMapping[i][j], 
                DIALOG_TYPE_INFO, true,
                [i, j]( int val ) {
                    if (settings3DS.UseGlobalButtonMappings)
                        CheckAndUpdate( settings3DS.GlobalButtonMapping[i][j], val );
                    else
                        CheckAndUpdate( settings3DS.ButtonMapping[i][j], val );
                }
            );
        }

        if (i < 8)
            AddMenuGauge(items, "  Rapid-Fire Speed"s, 0, 10, 
                settings3DS.UseGlobalTurbo ? settings3DS.GlobalTurbo[i] : settings3DS.Turbo[i], 
                [i]( int val ) 
                { 
                    if (settings3DS.UseGlobalTurbo)
                        CheckAndUpdate( settings3DS.GlobalTurbo[i], val ); 
                    else
                        CheckAndUpdate( settings3DS.Turbo[i], val ); 
                });
        
    }
}

//-------------------------------------------------------
// Sets up all the cheats to be displayed in the menu.
//-------------------------------------------------------

void makeCheatMenu(std::vector<SMenuItem>& items)
{
    items.clear();

    if (Cheat.num_cheats > 0) {
        items.reserve(Cheat.num_cheats + 1); 
    } else {
        items.reserve(1);
    }

    if (Cheat.num_cheats > 0) {
        AddMenuHeader1(items, "");

        char buffer[128]; 

        for (int i = 0; i < MAX_CHEATS && i < Cheat.num_cheats; i++) {
            snprintf(buffer, sizeof(buffer), "  %s", Cheat.c[i].name);

            items.emplace_back(
                nullptr, 
                MenuItemType::Checkbox, 
                buffer, 
                Cheat.c[i].cheat_code, 
                Cheat.c[i].enabled ? 1 : 0
            );
        }
    }
    else {
        char romName[NAME_MAX + 1];
        file3dsGetTrimmedBasename(Memory.ROMFilename, romName, sizeof(romName), false);

        static char message[PATH_MAX];
        snprintf(message, sizeof(message),
            "\nNo cheats found for this game. To enable cheats, copy\n"
            "\"%s.chx\" (or *.cht) into folder \"%s\" on your sd card.\n"
            "\n\nGame-Genie and Pro Action Replay Codes are supported.\n"
            "Format for *.chx is [Y/N],[CheatCode],[Name].\n"
            "See %s for more info\n"
            "\n\nCheat collection (roughly tested): %s",
            romName,
            "3ds/snes9x_3ds/cheats",
            "github.com/matbo87/snes9x_3ds-assets",
            "https://github.com/matbo87/snes9x_3ds-assets/releases/download/v0.1.0/cheats.zip");

        items.emplace_back(nullptr, MenuItemType::Textarea, message, "");
    }
}

//----------------------------------------------------------------------
// Read/write all possible game specific settings.
//----------------------------------------------------------------------
bool settingsReadWriteFullListByGame(bool writeMode)
{
    char path[PATH_MAX];
    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".cfg", "configs");
    
    if (path[0] == '\0') {
        return false;
    }
    
    BufferedFileWriter stream;

    if (writeMode) {
        if (!stream.open(path, "w"))
            return false;
    } else {
        if (!stream.open(path, "r"))
            return false;
    }

    char version[16];
    snprintf(version, sizeof(version), "%.1f", GAME_CONFIG_FILE_TARGET_VERSION);
    config3dsReadWriteString(stream, writeMode, "# v%s\n", "# v%15[^\n]\n", version);

    // if writing, we are definitely on the latest version
    // if reading, we parse what we just read into 'version'
    float detectedConfigVersion = writeMode 
        ? GAME_CONFIG_FILE_TARGET_VERSION 
        : config3dsGetVersionFromFile(true, version);
    config3dsReadWriteInt32(stream, writeMode, "# Do not modify this file or risk losing your settings.\n", NULL, 0, 0);
    config3dsReadWriteInt32(stream, writeMode, "Frameskips=%d\n", &settings3DS.MaxFrameSkips, 0, 4);
    config3dsReadWriteEnum(stream, writeMode, "Framerate=%d\n", &settings3DS.ForceFrameRate, 0, 3);
    config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.Volume, 0, 8);
    config3dsReadWriteInt32(stream, writeMode, "PalFix=%d\n", &settings3DS.PaletteFix, 0, 3);
    config3dsReadWriteEnum(stream, writeMode, "AutoSavestate=%d\n", &settings3DS.AutoSavestate, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "SRAMInterval=%d\n", &settings3DS.SRAMSaveInterval, 0, 4);
    config3dsReadWriteEnum(stream, writeMode, "ForceSRAMWrite=%d\n", &settings3DS.ForceSRAMWriteOnPause, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "BindCirclePad=%d\n", &settings3DS.BindCirclePad, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "LastSaveSlot=%d\n", &settings3DS.CurrentSaveSlot, 0, 5);

    static const char *buttonName[10] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "SELECT","START"};
    char keyBuf[64];

    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 3; ++j) {
            snprintf(keyBuf, sizeof(keyBuf), "ButtonMap%s_%d=%%d\n", buttonName[i], j);
            config3dsReadWriteInt32(stream, writeMode, keyBuf, &settings3DS.ButtonMapping[i][j]);
        }
    }

    static const char *turboButtonName[8] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    for (int i = 0; i < 8; ++i) {
        snprintf(keyBuf, sizeof(keyBuf), "Turbo%s=%%d\n", turboButtonName[i]);
        config3dsReadWriteInt32(stream, writeMode, keyBuf, &settings3DS.Turbo[i], 0, 10);
    }

    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        if (strlen(hotkeysData[i][0])) {
            snprintf(keyBuf, sizeof(keyBuf), "ButtonMapping%s_0=%%d\n", hotkeysData[i][0]);
            config3dsReadWriteBitmask(stream, writeMode, keyBuf, &settings3DS.ButtonHotkeys[i].MappingBitmasks[0]);
        }
    }

    return true;
}


//----------------------------------------------------------------------
// Read/write all possible global settings.
//----------------------------------------------------------------------
bool settingsReadWriteFullListGlobal(bool writeMode)
{
    char globalConfig[PATH_MAX];
    snprintf(globalConfig, sizeof(globalConfig), "%s/%s", settings3DS.RootDir, "settings.cfg");
    
    BufferedFileWriter stream;

    if (writeMode) {
        if (!stream.open(globalConfig, "w"))
            return false;
    } else {
        if (!stream.open(globalConfig, "r"))
            return false;
    }


    char version[16];
    snprintf(version, sizeof(version), "%.1f", GLOBAL_CONFIG_FILE_TARGET_VERSION);
    config3dsReadWriteString(stream, writeMode, "# v%s\n", "# v%15[^\n]\n", version);

    // if writing, we are definitely on the latest version
    // if reading, we parse what we just read into 'version'
    float detectedConfigVersion = writeMode 
        ? GLOBAL_CONFIG_FILE_TARGET_VERSION 
        : config3dsGetVersionFromFile(false, version);

    config3dsReadWriteInt32(stream, writeMode, "# Do not modify this file or risk losing your settings.\n", NULL, 0, 0);
    config3dsReadWriteEnum(stream, writeMode, "GameScreen=%d\n", &settings3DS.GameScreen, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "Theme=%d\n", &settings3DS.Theme, 0, TOTALTHEMECOUNT - 1);
    config3dsReadWriteEnum(stream, writeMode, "GameThumbnailType=%d\n", &settings3DS.GameThumbnailType, 0, 3);
    config3dsReadWriteEnum(stream, writeMode, "ScreenStretch=%d\n", &settings3DS.ScreenStretch, 0, 7);

    // removed since v1.3 -> skip
    if (!writeMode)
        config3dsReadWriteInt32(stream, writeMode, "ScreenFilter=%d\n", NULL, 0, 0);

    config3dsReadWriteEnum(stream, writeMode, "GameBezel=%d\n", &settings3DS.GameBezel, 0, 3);
    config3dsReadWriteEnum(stream, writeMode, "GameBezelAutoFit=%d\n", &settings3DS.GameBezelAutoFit, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "SecondScreenContent=%d\n", &settings3DS.SecondScreenContent, 0, 3);
    config3dsReadWriteInt32(stream, writeMode, "SecondScreenOpacity=%d\n", &settings3DS.SecondScreenOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteEnum(stream, writeMode, "GameBorder=%d\n", &settings3DS.GameBorder, 0, 3);
    config3dsReadWriteInt32(stream, writeMode, "GameBorderOpacity=%d\n", &settings3DS.GameBorderOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteEnum(stream, writeMode, "Disable3DSlider=%d\n", &settings3DS.Disable3DSlider, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "Font=%d\n", &settings3DS.Font, 0, 2);
    config3dsReadWriteEnum(stream, writeMode, "LogFileEnabled=%d\n", &settings3DS.LogFileEnabled, 0, 1);

    char formatBuf[64];
    snprintf(formatBuf, sizeof(formatBuf), "DefaultDir=%%%zu[^\n]\n", sizeof(settings3DS.defaultDir) - 1);
    config3dsReadWriteString(stream, writeMode, "DefaultDir=%s\n", formatBuf, settings3DS.defaultDir);
    snprintf(formatBuf, sizeof(formatBuf), "LastSelectedDir=%%%zu[^\n]\n", sizeof(settings3DS.lastSelectedDir) - 1);
    config3dsReadWriteString(stream, writeMode, "LastSelectedDir=%s\n", formatBuf, settings3DS.lastSelectedDir);
    snprintf(formatBuf, sizeof(formatBuf), "LastSelectedFilename=%%%zu[^\n]\n", sizeof(settings3DS.lastSelectedFilename) - 1);
    config3dsReadWriteString(stream, writeMode, "LastSelectedFilename=%s\n", formatBuf, settings3DS.lastSelectedFilename);

    config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.GlobalVolume, 0, 8);
    config3dsReadWriteEnum(stream, writeMode, "BindCirclePad=%d\n", &settings3DS.GlobalBindCirclePad, 0, 1);

    static const char *buttonName[10] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "SELECT","START"};
    char keyBuf[64];

    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 3; ++j) {
            snprintf(keyBuf, sizeof(keyBuf), "ButtonMap%s_%d=%%d\n", buttonName[i], j);
            config3dsReadWriteInt32(stream, writeMode, keyBuf, &settings3DS.GlobalButtonMapping[i][j]);
        }
    }
    
    static const char *turboButtonName[8] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    for (int i = 0; i < 8; ++i) {
        snprintf(keyBuf, sizeof(keyBuf), "Turbo%s=%%d\n", turboButtonName[i]);
        config3dsReadWriteInt32(stream, writeMode, keyBuf, &settings3DS.GlobalTurbo[i], 0, 10);
    }

    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        if (strlen(hotkeysData[i][0])) {
            snprintf(keyBuf, sizeof(keyBuf), "ButtonMapping%s_0=%%d\n", hotkeysData[i][0]);
            config3dsReadWriteBitmask(stream, writeMode, keyBuf, &settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0]);
        }
    }

    config3dsReadWriteEnum(stream, writeMode, "UseGlobalButtonMappings=%d\n", &settings3DS.UseGlobalButtonMappings, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "UseGlobalTurbo=%d\n", &settings3DS.UseGlobalTurbo, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "UseGlobalVolume=%d\n", &settings3DS.UseGlobalVolume, 0, 1);
    config3dsReadWriteEnum(stream, writeMode, "UseGlobalEmuControlKeys=%d\n", &settings3DS.UseGlobalEmuControlKeys, 0, 1);

    return true;
}

//----------------------------------------------------------------------
// Save settings by game.
//----------------------------------------------------------------------
bool settingsSave(bool includeGameSettings)
{
    if (!settings3DS.isDirty) return true;

    TickCounter timer;

    osTickCounterStart(&timer);

    cfgFileAvailable[0] = settingsReadWriteFullListGlobal(true);
    if (cfgFileAvailable[0]) {
        osTickCounterUpdate(&timer);
        log3dsWrite("Global settings saved in %.3fms", osTickCounterRead(&timer));
    }

    if (includeGameSettings) {
        osTickCounterStart(&timer);

        cfgFileAvailable[1] = settingsReadWriteFullListByGame(true);
        if (cfgFileAvailable[1]) {
            osTickCounterUpdate(&timer);
            log3dsWrite("Game settings saved in %.3fms", osTickCounterRead(&timer));
        }
    }
    
    settings3DS.isDirty = false;

    return true;
}

//-------------------------------------------------------
// Load the ROM and reset the CPU.
//-------------------------------------------------------
bool emulatorLoadRom(bool isGameLoaded)
{
    // save current game state first
    if (isGameLoaded) {
        settingsSave(true);

        if (settings3DS.AutoSavestate) {
            impl3dsSaveStateAuto();
        }
    }

    char romFileNameFullPath[PATH_MAX];
    snprintf(romFileNameFullPath, sizeof(romFileNameFullPath), "%s%s", file3dsGetCurrentDir(), romFileName);

    // when impl3dsLoadROM fails, our previous game (if any) is also unusable
    // therefore we always set ROMCRC32 to 0
    Memory.ROMCRC32 = 0; 
    bool loaded = impl3dsLoadROM(romFileNameFullPath);

    if (!loaded || !Memory.ROMCRC32) {
        return false;
    }

    // update global config
    snprintf(settings3DS.lastSelectedDir, sizeof(settings3DS.lastSelectedDir), "%s", file3dsGetCurrentDir());
    snprintf(settings3DS.lastSelectedFilename, sizeof(settings3DS.lastSelectedFilename), "%s", romFileName);
    
    settings3DS.isDirty = true;
    settings3dsResetGameDefaults();

    // if file exists, overwrite the defaults
    // if not, stay on defaults
    cfgFileAvailable[1] = settingsReadWriteFullListByGame(false);

    settings3dsUpdate(true);
    
    // check for valid hotkeys if circle pad binding is enabled
    if ((!settings3DS.UseGlobalButtonMappings && settings3DS.BindCirclePad) || 
        (settings3DS.UseGlobalButtonMappings && settings3DS.GlobalBindCirclePad))
        for (int i = 0; i < HOTKEYS_COUNT; ++i)
            ResetHotkeyIfNecessary(i, true);
    
    // set proper state (radio_state) for every save slot of loaded game
    for (int slot = 1; slot <= SAVESLOTS_MAX; ++slot)
        impl3dsUpdateSlotState(slot, true);

    if (settings3DS.AutoSavestate)
        impl3dsLoadStateAuto();
        
    return true;   
}

//----------------------------------------------------------------------
// Find the ID of the last selected item in the file list.
//----------------------------------------------------------------------
int findLastSelected(std::vector<DirectoryEntry>& romFileNames, const char* name)
{
    if (name == NULL || name[0] == '\0') {
		return -1;
	}

    for (int i = 0; i < romFileNames.size(); i++)
    {
       if (strncmp(romFileNames[i].Filename, name, sizeof(romFileNames[i].Filename) - 1) == 0)
            return i;
    }

    return -1;
}

//----------------------------------------------------------------------
// Handle menu cheats.
//----------------------------------------------------------------------

bool isAllUppercase(const char* text) {
    bool allUppercase = true;
    
    for (int i = 0; text[i] != '\0'; i++) {
        if (std::isalpha(text[i]) && !std::isupper(text[i])) {
            allUppercase = false;
            break;
        }
    }
    
    return allUppercase;
}

bool menuCopyCheats(std::vector<SMenuItem>& cheatMenu, bool copyMenuToSettings)
{
    bool cheatsUpdated = false;
    for (uint i = 0; (i+1) < cheatMenu.size() && i < MAX_CHEATS && i < Cheat.num_cheats; i++) {
        
        // if cheat name is all uppercase, capitalize it
        if (isAllUppercase(Cheat.c[i].name)) {
            for (int j = 1; Cheat.c[i].name[j] != '\0'; j++) {
                if (std::isalpha(Cheat.c[i].name[j])) {
                    Cheat.c[i].name[j] = std::tolower(Cheat.c[i].name[j]);
                }
            }
        }
        
        cheatMenu[i+1].Text = "  " + std::string(Cheat.c[i].name);
        cheatMenu[i+1].Description = Cheat.c[i].cheat_code;
        cheatMenu[i+1].Type = MenuItemType::Checkbox;

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

// returns the index of the item matching 'selectedItemName', or 0 if not found/empty
int fillFileMenuFromFileNames(std::vector<SMenuItem>& fileMenu, const char *selectedItemName) {
    fileMenu.clear();
    fileMenu.reserve(romFileNames.size());

    int selectedItemIndex = 0;

    for (size_t i = 0; i < romFileNames.size(); ++i) {
        // get the permanent address of the item in the global vector
        const DirectoryEntry* entry = &romFileNames[i];

        const char* prefix = MENU_PREFIX_FILE; 

        if (entry->Type == FileEntryType::ChildDirectory)
            prefix = MENU_PREFIX_CHILD_DIRECTORY;
        else if (entry->Type == FileEntryType::ParentDirectory)
            prefix = MENU_PREFIX_PARENT_DIRECTORY;

        char label[NAME_MAX + 1];
        snprintf(label, sizeof(label), "%s%s", prefix, entry->Filename);

        if (selectedItemName && selectedItemName[0] != '\0') {
            if (strncmp(entry->Filename, selectedItemName, NAME_MAX) == 0) {
                selectedItemIndex = i;
            }
        }

        fileMenu.emplace_back(
            // Do NOT use [&entry] here (previous implementation, dangling reference)
            // capture the pointer by value [entry]
            [entry](int val) { 
                selectedDirectoryEntry = entry; 
            }, 
            MenuItemType::Action, 
            label, 
            "", 
            99999
        );
    }

    return selectedItemIndex;
}


void updateFileMenuTab(const char *selectedItemName) {
    SMenuTab& fileMenuTab = menuTab.back();

    fileMenuTab.SubTitle.assign(file3dsGetCurrentDir());

    file3dsGetFiles(romFileNames);
    fileMenuTab.SelectedItemIndex = fillFileMenuFromFileNames(fileMenuTab.MenuItems, selectedItemName);
    fileMenuTab.MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);
}

void setupMenu(int& currentMenuTab, bool isGameLoaded) {
    int requiredTabs = isGameLoaded ? 5 : 2;
    int fileMenuTabIndex = isGameLoaded ? 4 : 1;
    
    // only reallocate if the size grows, otherwise reuse the buffer
    if (menuTab.size() != requiredTabs) {
        menuTab.resize(requiredTabs);
    }

    const char* tabsStart[] = { "Emulator", "Load Game" };
    const char* tabsGame[] = { "Emulator", "Settings", "Controls", "Cheats", "Load Game" };
    const char** tabs = isGameLoaded ? tabsGame : tabsStart;

    for (int i = 0; i < requiredTabs; i++) {
        menuTab[i].SetTitle(tabs[i]);
        menuTab[i].SubTitle.clear();

        if (i != fileMenuTabIndex) {
            
            switch (i) {
                case 0:
                    makeEmulatorMenu(menuTab[i].MenuItems, menuTab, currentMenuTab, isGameLoaded);
                    break;
                case 1:
                    makeOptionMenu(menuTab[i].MenuItems, menuTab, currentMenuTab);
                    break;
                case 2:
                    makeControlsMenu(menuTab[i].MenuItems, menuTab, currentMenuTab);
                    break;
                case 3:
                    makeCheatMenu(menuTab[i].MenuItems);
                    break;
            }

            // 
            for (int j = 0; j < menuTab[i].MenuItems.size(); j++) {
                if (menuTab[i].MenuItems[j].IsHighlightable()) {
                    menuTab[i].SelectedItemIndex = j;
                    menuTab[i].MakeSureSelectionIsOnScreen(MENU_HEIGHT, 2);

                    break;
                }
            }
        } else {
            updateFileMenuTab(settings3DS.lastSelectedFilename);
        }
    }
}

int showFileMenuOptions(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab) {
    SMenuTab *currentTab = &menuTab[currentMenuTab];
    const char* selectedFileName = "";

    if (romFileNames[currentTab->SelectedItemIndex].Type == FileEntryType::File) {
        selectedFileName = romFileNames[currentTab->SelectedItemIndex].Filename;
    }
    
    bool hasDeleteGameOption = (selectedFileName[0] != '\0') && (strcmp(selectedFileName, settings3DS.lastSelectedFilename) != 0);
    
    int option = menu3dsShowDialog(
        dialogTab, isDialog, currentMenuTab, menuTab, 
        "File Menu Options", 
        "If no default directory is set, the file menu will show the directory of the last selected game.", 
        Themes[settings3DS.Theme].dialogColorInfo, 
        makeOptionsForFileMenu({"Set current directory as default", "Reset default directory", "Select random game in current directory", "Delete selected game" }, hasDeleteGameOption));
    
    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

    if (option == 0 || option == 1) {
        file3dsSetDefaultDir(option == 1);
    }
    else if (option == 2) {
        menu3dsSelectRandomGame(&menuTab[currentMenuTab]);
    }
    else if (option == 3) {
        char message[512];
        snprintf(message, sizeof(message), "Do you really want to remove \"%s\" from your SD card?", selectedFileName);
        bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Delete Game", message, false);

        if (confirmed) {
            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s%s", file3dsGetCurrentDir(), selectedFileName);

            if (remove(path) == 0) {
                file3dsDeleteDirCache(file3dsGetCurrentDir());
                updateFileMenuTab(NULL);
                snprintf(message, sizeof(message), "%s removed from SD card.", selectedFileName);
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Success", message, Themes[settings3DS.Theme].dialogColorSuccess, makeOptionsForOk(), -1, false);                
            } else {
                snprintf(message, sizeof(message), "Couldn't remove %s", selectedFileName);
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Error", message, Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk(), -1, false);
            }
        }

        menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
    }

    return option;
}

void onDirectoryEntrySelected(
    SMenuTab& dialogTab, 
    bool& isDialog, 
    int currentMenuTab, 
    bool& runNextGame, 
    bool isGameLoaded,
    const DirectoryEntry*& entry
) {
    if (entry->Type == FileEntryType::File) 
    {
        // Same game -> skip
        if (strncmp(romFileName, entry->Filename, sizeof(romFileName) - 1) == 0) 
            return;
        
        snprintf(romFileName, sizeof(romFileName), "%s", entry->Filename);

        char basename[NAME_MAX + 1];
        file3dsGetBasename(romFileName, basename, sizeof(basename), false);
        menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Loading Game:", basename, Themes[settings3DS.Theme].dialogColorInfo, std::vector<SMenuItem>());
        
        runNextGame = emulatorLoadRom(isGameLoaded);
        
        if (!runNextGame) {
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Loading Game:", "Oops. Unable to load Game", Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk(), -1, false);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
        } else {
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }
    } 
    else if (entry->Type == FileEntryType::ParentDirectory || entry->Type == FileEntryType::ChildDirectory) 
    {
        char lastDirectoryName[128] = ""; // e.g. for "sdmc:/roms/snes/EUR" it's "EUR"

        if (entry->Type == FileEntryType::ParentDirectory) {
            file3dsGetCurrentDirName(lastDirectoryName, sizeof(lastDirectoryName));
        }

        file3dsGoUpOrDownDirectory(*entry);
        updateFileMenuTab(lastDirectoryName);
    }
}

void showMenu() {
    static std::vector<SMenuItem> emptyCheats;
    int currentMenuTab = menu3dsGetLastSelectedTabIndex();
    bool isGameLoaded = Memory.ROMCRC32 != 0;

    // 1. first boot
    // 2. new game loaded
    if (menuTab.empty() || Memory.ROMCRC32 != lastLoadedRomCRC)
    {
        setupMenu(currentMenuTab, isGameLoaded);
        lastLoadedRomCRC = Memory.ROMCRC32;
    }

    std::vector<SMenuItem>& cheatMenu = isGameLoaded ? menuTab[3].MenuItems : emptyCheats;

    if (!cheatMenu.empty()) {
        menuCopyCheats(cheatMenu, false);
        menu3dsSetCheatsIndicator(cheatMenu);
    }

    bool isDialog = false;
    bool runNextGame = false;
    SMenuTab dialogTab;

    while (aptMainLoop() && GPU3DS.emulatorState == EMUSTATE_PAUSEMENU) {
        int result = menu3dsMenuSelectItem(dialogTab, isDialog, currentMenuTab, menuTab, isGameLoaded);

        if (settings3DS.uiNeedsRebuild)
        {
            setupMenu(currentMenuTab, isGameLoaded);
            settings3DS.uiNeedsRebuild = false;
        }

        // user pressed X button in file menu
        if (result == MENU_ENTRY_CONTEXT_MENU) 
        {
            showFileMenuOptions(dialogTab, isDialog, currentMenuTab);
        }
        // user pressed START button in pause menu -> continue game
        else if (result == MENU_CONTINUE_GAME) 
        {
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }
        else if (selectedDirectoryEntry) 
        {
            onDirectoryEntrySelected(dialogTab, isDialog, currentMenuTab, runNextGame, isGameLoaded, selectedDirectoryEntry);
            selectedDirectoryEntry = nullptr;
        }
    }

    // load/resume game
    if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
        if (!runNextGame) {
            // only save cheat state after resuming current game 
            bool cheatsUpdated = !cheatMenu.empty() && menuCopyCheats(cheatMenu, true);

            if (cheatsUpdated) {
                char path[PATH_MAX];
                
                file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".chx", "cheats", true);
            
                if (!S9xSaveCheatTextFile(path)) {
                    // if .chx fails, try .cht
                    file3dsGetRelatedPath(Memory.ROMFilename, path, sizeof(path), ".cht", "cheats", true);
                    S9xSaveCheatFile(path);
                }
            }

            settings3dsUpdate(true);
        }

        // point to first tab when running a new game
        menu3dsSetLastSelectedTabIndex(runNextGame ? 0 : currentMenuTab);

        impl3dsUpdateUiAssets();

        if (slotLoaded) {
            static char message[PATH_MAX];
			snprintf(message, sizeof(message), "Slot #%d loaded", settings3DS.CurrentSaveSlot);
            ui3dsTriggerNotification(message, NOTIFICATION_SUCCESS);
        
            slotLoaded = false;
        }

        if (isDialog) {
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
        }
    }

    menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTab);
}

//--------------------------------------------------------
// Initialize the emulator engine and everything else.
// This calls the impl3dsInitialize, which executes
// initialization code specific to the emulation core.
//--------------------------------------------------------
bool emulatorInitialize()
{
    log3dsInitialize();
    log3dsWrite("==== START Logging (%s, %s) ====", settings3dsGetAppVersion("v"), log3dsGetCurrentDate());

    Result rc = fsInit();
    if (R_FAILED(rc)) {
        log3dsWrite("Unable to initialize FS service: %08lx", rc);
        return false;
    }

	log3dsWrite("-- gpu3ds initialize --");
    if (!gpu3dsInitialize())
    {
	    log3dsWrite("Unable to initialize GPU");

        return false;
    }

    osSetSpeedupEnable(true);

    u16 x0 = 0;
    u16 y0 = (SCREEN_HEIGHT - FONT_HEIGHT) / 2;
    u16 x1 = settings3DS.SecondScreenWidth;
    u16 y1 = y0 + FONT_HEIGHT;

    for (int i = 0; i < 2; i ++) {
        aptMainLoop();
        ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, x0, y0, x1, y1, 0xEEEEEE, HALIGN_CENTER, "Initializing...");
        gfxScreenSwapBuffers(settings3DS.SecondScreen, false);
        gspWaitForVBlank();
    }

	log3dsWrite("romfsInit");
	rc = romfsInit();

    if (R_FAILED(rc)) {
        settings3DS.isRomFsLoaded = false;
    } else {
        settings3DS.isRomFsLoaded = true;
    }

	log3dsWrite("-- impl3dsInitialize --");

    if (!impl3dsInitialize())
    {
	    log3dsWrite("Unable to initialize emulator core");

        return false;
    }

	log3dsWrite("-- snd3dsInitialize --");

    if (!snd3dsInitialize())
    {
	    log3dsWrite("Unable to initialize CSND");
        
        return false;
    }

	log3dsWrite("-- file3ds initialize --");
    file3dsInitialize();

    if (settings3DS.isRomFsLoaded) {
	    log3dsWrite("file3dsSetRomNameMappings");

        file3dsSetRomNameMappings("romfs:/mappings.txt");
    }

    #ifndef PROFILING_DISABLED
        t3dsResetTimers();
    #endif

	log3dsWrite("enableAptHooks");
    enableAptHooks();

	log3dsWrite("srvInit");
    srvInit();

	log3dsWrite("#### memory in use ####");
    log3dsWrite("linear: %dkb / %dkb", (GPU3DS.linearMemTotal - linearSpaceFree()) / 1024, GPU3DS.linearMemTotal / 1024);
    log3dsWrite("vram: %dkb / %dkb", (GPU3DS.vramTotal - vramSpaceFree()) / 1024, GPU3DS.vramTotal / 1024);
	log3dsWrite("---- initialized ----");

    return true;
}

//--------------------------------------------------------
// Finalize the emulator.
//--------------------------------------------------------
void emulatorFinalize()
{
	log3dsWrite("---- emulatorFinalize ----");

    consoleClear();

    log3dsWrite("-- snd3dsFinalize --");
    snd3dsFinalize();
    
    log3dsWrite("-- impl3dsFinalize --");
    impl3dsFinalize();

    log3dsWrite("-- gpu3dsFinalize --");
    gpu3dsFinalize();

    log3dsWrite("romfsExit");
    romfsExit();

    osSetSpeedupEnable(false);

    log3dsWrite("ptmSysmExit");
    ptmSysmExit();

    log3dsWrite("disableAptHooks");
    disableAptHooks();

    log3dsWrite("hidExit");
    hidExit();
    
    log3dsWrite("aptExit");
    aptExit();
    
    log3dsWrite("srvExit");
    srvExit();

    log3dsWrite("fsExit");
	fsExit();

    log3dsWrite("==== END Logging (%s, %s) ====", settings3dsGetAppVersion("v"), log3dsGetCurrentDate());
    log3dsClose();
}


//---------------------------------------------------------
// Counts the number of frames per second, and prints
// it to the second screen every 60 frames.
//---------------------------------------------------------

char frameCountBuffer[70];

void updateSecondScreenContent(int totalFrames)
{
    #ifndef PROFILING_DISABLED
        if (GPU3DS.profilingMode) {
            //  show current timer values per frame
            if (GPU3DS.profilingMode == PROFILING_CUSTOM) {
                t3dsPrintAllTimers(totalFrames);
                t3dsResetTimers();
            }

            if (frameCount60 == 0)
            {
                //  show current fps every 60 frames
                if (GPU3DS.profilingMode == PROFILING_FPS) {
                    t3dsPrintTimer(TIMER_RUN_ONE_FRAME);
                    t3dsResetTimers(true);
                }

                frameCount60 = 60;
            }

            frameCount60--;

            return;
        }

    #endif

    if (frameCountTick == 0)
        frameCountTick = svcGetSystemTick();

    if (frameCount60 == 0)
    {
        u64 newTick = svcGetSystemTick();

        // TODO: draw fps info
        if (false) {
            float timeDelta = ((float)(newTick - frameCountTick))/TICKS_PER_SEC;
            int fpsmul10 = (int)((float)600 / timeDelta);

            if (framesSkippedCount)
                snprintf (frameCountBuffer, 69, "FPS: %2d.%1d (%d skipped)", fpsmul10 / 10, fpsmul10 % 10, framesSkippedCount);
            else
                snprintf (frameCountBuffer, 69, "FPS: %2d.%1d", fpsmul10 / 10, fpsmul10 % 10);

            float alpha = (float)(settings3DS.SecondScreenOpacity) / OPACITY_STEPS;
            // menu3dsSetFpsInfo(framesSkippedCount ? Themes[settings3DS.Theme].dialogColorWarn : 0xFFFFFF, alpha, frameCountBuffer);
        }

        frameCount60 = 60;
        framesSkippedCount = 0;
        frameCountTick = newTick;
    }

    frameCount60--;

    if (++frameCount == UINT16_MAX)
        frameCount = 0;
}

//----------------------------------------------------------
// This is the main emulation loop. It calls the 
//    impl3dsRunOneFrame
//   (which must be implemented for any new core)
// for the execution of the frame.
//----------------------------------------------------------
void emulatorLoop()
{
    int snesFramesSkipped = 0;
    long snesFrameTotalActualTicks = 0;
    long snesFrameTotalAccurateTicks = 0;

    bool firstFrame = true;

    snd3DS.generateSilence = false;

    frameCount60 = 60;
    frameCountTick = 0;
    framesSkippedCount = 0;

    bool skipDrawingFrame = false;

    gpu3dsResetState();
    
    bool profilingEnabled = GPU3DS.profilingMode != PROFILING_NONE; // for debugging

    if (profilingEnabled) {
        // important: consoleInit(...) sets double buffering to false
        // make sure to enable double buffering again when leaving emulatorLoop()
        consoleInit(settings3DS.SecondScreen, NULL);
    }

    // menu is currently rendered via software and may have configured the screen for 
    // a lower color depth than our other screen content rendered via GPU.
    // therefore we check the screen format first to ensure pixel data is interpreted correctly
    GSPGPU_FramebufferFormat gpuBufFmt = (GSPGPU_FramebufferFormat)DISPLAY_TRANSFER_FMT;

    if (gfxGetScreenFormat(settings3DS.SecondScreen) != gpuBufFmt) {
        gfxSetScreenFormat(settings3DS.SecondScreen, gpuBufFmt);
    }

    int totalFrames = 0;

    snd3dsStartPlaying();

	while (aptMainLoop() && GPU3DS.emulatorState == EMUSTATE_EMULATE)
	{
        u64 startFrameTick = svcGetSystemTick();

        input3dsScanInputForEmulation();
        t3dsStartTimer(TIMER_RUN_ONE_FRAME);
        impl3dsRunOneFrame(firstFrame, skipDrawingFrame);
        t3dsStopTimer(TIMER_RUN_ONE_FRAME);

        updateSecondScreenContent(++totalFrames);
        
        // This either waits for the next frame, or decides to skip
        // the rendering for the next frame if we are too slow.
        //
        if (GPU3DS.profilingMode == PROFILING_NONE)
        {
            if (profilingEnabled) {
                consoleClear();
                profilingEnabled = false;
            }
            
            long currentTick = svcGetSystemTick();
            long actualTicksThisFrame = currentTick - startFrameTick;

            snesFrameTotalActualTicks += actualTicksThisFrame;  // actual time spent rendering past x frames.
            snesFrameTotalAccurateTicks += settings3DS.TicksPerFrame;  // time supposed to be spent rendering past x frames.

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
                    if (settings3DS.ForceFrameRate == SettingFramerate_Match3DS) {
                        gspWaitForVBlank();
                    } else {
                        svcSleepThread ((long)(timeDiffInMilliseconds * 1000));
                    }
                    skipDrawingFrame = false;
                }
            }
        }
        #ifndef PROFILING_DISABLED
            else 
            {
                if (!profilingEnabled) {
                    consoleInit(settings3DS.SecondScreen, NULL);
                    profilingEnabled = true;
                }

                if (GPU3DS.profilingMode == PROFILING_CUSTOM) {
                    while (aptMainLoop())
                    {
                        hidScanInput();

                        bool fast = (hidKeysHeld() & KEY_RIGHT) || (hidKeysHeld() & KEY_R) || (hidKeysHeld() & KEY_ZR);

                        u32 kDown = (fast ? hidKeysHeld() : hidKeysDown());

                        if (hidKeysDown() & KEY_R) {
                            consoleClear();
                            break;
                        }

                        if (kDown) {
                            break;
                        }
                    }
                } else {
                    skipDrawingFrame = (frameCount60 % 2) == 0;
                }
            }
        #endif

        firstFrame = false; 
	}

    gfxSetDoubleBuffering(settings3DS.SecondScreen, true);

    snd3dsStopPlaying();
}

//---------------------------------------------------------
// Main entrypoint.
//---------------------------------------------------------
int main()
{
    menu3dsSetHotkeysData(hotkeysData);
    settings3dsResetGlobalDefaults();
    settings3dsResetGameDefaults();

    // load global config, overwrites defaults if file exists
    cfgFileAvailable[0] = settingsReadWriteFullListGlobal(false);

    // we now have our most recent config values
    ui3dsInitialize();
    settings3dsUpdate(false);

    if (!emulatorInitialize()) {
        emulatorFinalize();

        return 0;
    }
    
    img3dsSetThumbMode();

    GPU3DS.emulatorState = EMUSTATE_PAUSEMENU;
    
    while (aptMainLoop() && GPU3DS.emulatorState != EMUSTATE_END) {
        switch (GPU3DS.emulatorState) {
            case EMUSTATE_PAUSEMENU:
                showMenu();
                break;
            case EMUSTATE_EMULATE:
                emulatorLoop();
                break;
            default:
                GPU3DS.emulatorState = EMUSTATE_END;
        }
    }

    log3dsWrite("==== EXIT emulator ====");

    menu3dsDrawBlackScreen();
    Bounds b = ui3dsGetBounds(settings3DS.SecondScreenWidth, settings3DS.SecondScreenWidth, FONT_HEIGHT, Position::MC, 0, 0);
    ui3dsDrawStringWithNoWrapping(settings3DS.SecondScreen, b.left, b.top, b.right, b.bottom,0xEEEEEE, HALIGN_CENTER, "clean up...");
    gfxScreenSwapBuffers(settings3DS.SecondScreen, false);
    gspWaitForVBlank();

    settingsSave(!!Memory.ROMCRC32);

    // autosave rom on exit
    if (Memory.ROMCRC32 && settings3DS.AutoSavestate) {
        impl3dsSaveStateAuto();
    }

    file3dsFinalize();
    romFileNames.clear();    
    menuTab.clear();
    DUMP_VECTOR_INFO("romFileNames after cleanup", romFileNames);
    
    
    emulatorFinalize();

    return 0;
}