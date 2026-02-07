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

S9xSettings3DS settings3DS;
ScreenSettings screenSettings;

#define TICKS_PER_SEC (268123480)
#define TICKS_PER_FRAME_NTSC (4468724)
#define TICKS_PER_FRAME_PAL (5362469)

#define STACKSIZE (4 * 1024)

int frameCount = 0;
int frameCount60 = 60;
u64 frameCountTick = 0;
int framesSkippedCount = 0;

// wait maxFramesForDialog before hiding dialog message
// (60 frames = 1 second)
int maxFramesForDialog = 60; 

char romFileName[NAME_MAX + 1];
bool slotLoaded = false;
bool cfgFileAvailable[2]; // global config, game config

char* hotkeysData[HOTKEYS_COUNT][3];

static bool gameMenuInitialized = false;
static std::vector<SMenuTab> menuTab;
static std::vector<DirectoryEntry> romFileNames;

extern SCheatData Cheat;

const std::vector<std::string>& getUiAssetOptions() {
    static std::vector<std::string> options;

    if (options.empty()) {
        options.reserve(ASSET_COUNT);
        const char* label;
            
        for (int i = 0; i < ASSET_COUNT; i++) {
            switch (static_cast<AssetDisplayMode>(i)) {
                case ASSET_DEFAULT:     label = "Default";     break;
                case ASSET_ADAPTIVE:    label = "Adaptive";    break;
                case ASSET_CUSTOM_ONLY: label = "Custom Only"; break;
                default:                label = "None";        break;
            }
            options.push_back(label);
        }
    }

    return options;
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
// Update settings.
//----------------------------------------------------------------------

bool settingsUpdateAllSettings(bool includeGameSettings)
{
    // set defaults (pixel perfect)
    settings3DS.StretchWidth = 256;
    settings3DS.StretchHeight = -1;
    settings3DS.CropPixels = 0;

    switch (settings3DS.ScreenStretch)
    {
        case SCALE_4_3_ASPECT:
            settings3DS.StretchWidth = 298;
            break;

        case SCALE_CRT_ASPECT:
            settings3DS.StretchWidth = 292;
            break;

        case SCALE_4_3_FIT_CROPPED:
            settings3DS.CropPixels = 8;
        case SCALE_4_3_FIT:
            settings3DS.StretchWidth = 320;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;

        case SCALE_FULL_CROPPED:
            settings3DS.CropPixels = 8;
        case SCALE_FULL:
            settings3DS.StretchWidth = screenSettings.GameScreenWidth;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;

        case SCALE_8_7_FIT:
            settings3DS.StretchWidth = 274;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;
    }

    if (includeGameSettings)
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
        }

        if (settings3DS.UseGlobalButtonMappings) {
            for (int i = 0; i < 10; i++)
                for (int j = 0; j < 4; j++)
                    settings3DS.ButtonMapping[i][j] = settings3DS.GlobalButtonMapping[i][j];
            
            settings3DS.BindCirclePad = settings3DS.GlobalBindCirclePad;
        }

        if (settings3DS.UseGlobalTurbo) {
            for (int i = 0; i < 8; i++) 
                settings3DS.Turbo[i] = settings3DS.GlobalTurbo[i];
        }

        if (settings3DS.UseGlobalEmuControlKeys) {
             for (int i = 0; i < HOTKEYS_COUNT; ++i) 
                settings3DS.ButtonHotkeys[i] = settings3DS.GlobalButtonHotkeys[i];
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

    return true;
}

void settingsResetGameDefaults() {
    settings3DS.PaletteFix = 3;
    settings3DS.SRAMSaveInterval = 4;
    settings3DS.ForceSRAMWriteOnPause = 0;
    settings3DS.AutoSavestate = 0;
    settings3DS.MaxFrameSkips = 1;
    settings3DS.Volume = 4;
    settings3DS.ForceFrameRate = EmulatedFramerate::UseRomRegion;

    // reset controls to global defaults (settings.cfg)
    //
    settings3DS.BindCirclePad = settings3DS.GlobalBindCirclePad;

    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 4; j++) {
            settings3DS.ButtonMapping[i][j] = settings3DS.GlobalButtonMapping[i][j];
        }
    }
    
    for (int i = 0; i < 8; i++) {
        settings3DS.Turbo[i] = settings3DS.GlobalTurbo[i];
    }

    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        settings3DS.ButtonHotkeys[i] = settings3DS.GlobalButtonHotkeys[i];
    }
}

void settingsResetGlobalDefaults() {
    settings3DS.Theme = 0;
    u32 defaultButtonMapping[] = 
    { SNES_A_MASK, SNES_B_MASK, SNES_X_MASK, SNES_Y_MASK, SNES_TL_MASK, SNES_TR_MASK, 0, 0, SNES_SELECT_MASK, SNES_START_MASK };

    for (int i = 0; i < 10; i++)
    {
        settings3DS.GlobalButtonMapping[i][0] = defaultButtonMapping[i];
    }

    for (int i = 0; i < HOTKEYS_COUNT; ++i)
        settings3DS.ButtonHotkeys[i].SetSingleMapping(0);

    // clear all turbo buttons.
    for (int i = 0; i < 8; i++)
        settings3DS.Turbo[i] = 0;
}

//----------------------------------------------------------------------
// Menu options
//----------------------------------------------------------------------

namespace {
    template <typename T>
    bool CheckAndUpdate( T& oldValue, const T& newValue ) {
        if (oldValue != newValue) {
            settings3DS.dirty = true;
            oldValue = newValue;
            return true;
        }
        return false;
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

int resetConfigOptionSelected(int val) {
    int cfgRemovalfailed = 0;

    if (val == 1 || val == 3) {
        char globalConfigFile[_MAX_PATH];
        snprintf(globalConfigFile, sizeof(globalConfigFile), "%s/%s", settings3DS.RootDir, "settings.cfg");
        if (std::remove(globalConfigFile) == 0) {
            settingsResetGlobalDefaults();
            cfgFileAvailable[0] = false;
            settingsUpdateAllSettings(false);
        } else {
            cfgRemovalfailed += 1;
        }
    }
    
    if (val > 1) {
        std::string gameConfigFile = file3dsGetAssociatedFilename(Memory.ROMFilename, ".cfg", "configs");
        if (!gameConfigFile.empty() && std::remove(gameConfigFile.c_str()) == 0) {
            settingsResetGameDefaults();
            cfgFileAvailable[1] = false;
            settingsUpdateAllSettings(true);
        } else {
            cfgRemovalfailed += 2;
        }
    }

    return  cfgRemovalfailed;
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
        std::string gameConfigFilename =  file3dsGetFileBasename(Memory.ROMFilename, false);

        if (gameConfigFilename.length() > 44) {
            gameConfigFilename = gameConfigFilename.substr(0, 44) + "...";
        }

        gameConfigFilename += ".cfg";
        AddMenuDialogOption(items, 2, "Game"s, gameConfigFilename);
    }

    if (cfgFileAvailable[0] && cfgFileAvailable[1]) {
        AddMenuDialogOption(items, 3, "Both"s, ""s);
    }
    
    return items;
}

std::vector<SMenuItem> makeOptionsForOk() {
    return makePickerOptions({"OK"});
}

std::vector<SMenuItem> makeOptionsForGameThumbnail(const std::vector<std::string>& options) {
    std::vector<SMenuItem> items;
    items.reserve(options.size());

    for (int i = 0; i < options.size(); i++) {
        if (i == 0)
            AddMenuDialogOption(items, i, options[i],                ""s);
        else {
            std::string type = options[i];
            type[0] = std::tolower(type[0]);

            if (file3dsthumbnailsAvailable(type.c_str())) {
                AddMenuDialogOption(items, i, options[i], ""s);
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
            if (strcmp(settings3DS.defaultDir, "/") != 0) {
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

void makeEmulatorMenu(std::vector<SMenuItem>& items, std::vector<SMenuTab>& menuTab, int& currentMenuTab, bool currentGamePaused) {
    items.clear();

    if (currentGamePaused) {
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

            char path[_MAX_PATH];
            bool success = impl3dsTakeScreenshot(path, true);

            if (success)
            {
                char text[_MAX_PATH];
                snprintf(text, sizeof(text), "Screenshot saved to %s", path);
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Screenshot", text, Themes[settings3DS.Theme].dialogColorSuccess, makeOptionsForOk(), -1, false);
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

    std::vector<std::string>thumbnailOptions = {"None", "Boxart", "Title", "Gameplay"};
    std::string gameThumbnailMessage = "Type of thumbnails to display in \"Load Game\" tab.";
    bool thumbnailsAvailable = false;

    for (const std::string& option : thumbnailOptions) {
        std::string type = option;
        type[0] = std::tolower(type[0]);
        if (file3dsthumbnailsAvailable(type.c_str())) {
            thumbnailsAvailable = true;
            break;
        }
    }

    // display info message when user doesn't have provided any game thumbnails yet
    if (!thumbnailsAvailable) {
        gameThumbnailMessage += "\nNo thumbnails found. You can download them on \ngithub.com/matbo87/snes9x_3ds-assets";
    }

    AddMenuPicker(items, "  Game Thumbnail"s, "Type of thumbnails to display in \"Load Game\" tab."s, makeOptionsForGameThumbnail(thumbnailOptions), settings3DS.GameThumbnailType, DIALOG_TYPE_INFO, true,
        [&menuTab, &currentMenuTab]( int val ) { 
            if (!CheckAndUpdate(settings3DS.GameThumbnailType, val)) {
                return;
            }
            
            img3dsSetThumbMode();
        });

    std::vector<std::string>themeNames;

    for (int i = 0; i < TOTALTHEMECOUNT; i++) {
        themeNames.emplace_back(std::string(Themes[i].Name));
    }

    AddMenuPicker(items, "  Theme"s, "The theme used for the user interface."s, makePickerOptions(themeNames), settings3DS.Theme, DIALOG_TYPE_INFO, true,
        []( int val ) { CheckAndUpdate(settings3DS.Theme, val); });


    AddMenuPicker(items, "  Font"s, "The font used for the user interface."s, makePickerOptions({"Tempesta", "Ronda", "Arial"}), settings3DS.Font, DIALOG_TYPE_INFO, true,
        []( int val ) { if ( CheckAndUpdate( settings3DS.Font, val ) ) { ui3dsSetFont(val); } });

    AddMenuPicker(items, "  Game Screen"s, "Play your games on top or bottom screen"s, makePickerOptions({"Top", "Bottom"}), settings3DS.GameScreen, DIALOG_TYPE_INFO, true,
        [&menuTab, &currentMenuTab]( int val ) { 
            gfxScreen_t gameScreen = (val == 0) ? GFX_TOP : GFX_BOTTOM;
        
            if (!CheckAndUpdate(settings3DS.GameScreen, gameScreen)) {
                return;
            }

            ui3dsUpdateScreenSettings(settings3DS.GameScreen);

            SMenuTab dialogTab;
            bool isDialog = false;
            menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTab);
        });

    AddMenuCheckbox(items, "  Disable 3D Slider"s, settings3DS.Disable3DSlider,
        []( int val ) { CheckAndUpdate( settings3DS.Disable3DSlider, val ); });

    AddMenuDisabledOption(items, ""s);

    AddMenuHeader1(items, "OTHERS"s);

    AddMenuCheckbox(items, "  Enable Logging (use when issues occur)"s, settings3DS.LogFileEnabled,
        []( int val ) { CheckAndUpdate( settings3DS.LogFileEnabled, val ); });
    std::string logfileInfo = "  Creates a session log in \"3ds/snes9x_3ds\". Restart required";
    AddMenuDisabledOption(items, logfileInfo);
    AddMenuDisabledOption(items, ""s);

    if (cfgFileAvailable[0]) {
        items.emplace_back([&menuTab, &currentMenuTab](int val) {
            char resetConfigDescription[256];
            snprintf(
                resetConfigDescription, sizeof(resetConfigDescription), 
                "Restore default settings%s. Emulator will quit afterwards so that changes take effect on restart.", 
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

            int result = resetConfigOptionSelected(option);
            
            switch (result) {
                case 1:
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Error", "Couldn't remove global config. If the error persists, try to delete the file manually from your sd card.", Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk(), -1, false);
                    break;
                case 2:
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Error", "Couldn't remove game config. If the error persists, try to delete the file manually from your sd card.", Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk(), -1, false);
                    break;
                case 3:
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Error", "Couldn't remove global config and game config. If the error persists, try to delete the files manually from your sd card.", Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk(), -1, false);
                    break;
                default:
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Success",  "Config removed.", Themes[settings3DS.Theme].dialogColorSuccess, makeOptionsForOk(), -1, false);
                    break;
            }

            // disable option when no config is available 
            if (!cfgFileAvailable[0] && !cfgFileAvailable[1]) {
                SMenuTab *currentTab = &menuTab[currentMenuTab]; 
                currentTab->MenuItems[currentTab->SelectedItemIndex].Type = MenuItemType::Disabled;                
            }

            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
        }, MenuItemType::Action, "  Reset Config"s, ""s);
    }

    AddMenuPicker(items, "  Quit Emulator"s, "Are you sure you want to quit?", makePickerOptions({ "No", "Yes" }), 0, DIALOG_TYPE_WARN, false, exitEmulatorOptionSelected);

    AddMenuHeader2(items, ""s);
    std::string info = std::string(getAppVersion("  Snes9x for 3DS v")) + " \x0b7 github.com/matbo87/snes9x_3ds";
    AddMenuDisabledOption(items, info);
}

const std::vector<SMenuItem>& makeOptionsForOnScreenDisplay() {
    static std::vector<SMenuItem> items;

    if (items.empty()) {
        items.reserve(4);
        AddMenuDialogOption(items, ASSET_NONE, "None"s,              ""s);
        AddMenuDialogOption(items, ASSET_DEFAULT, "Standard"s,              "Uses _default.png, fallback to internal image"s);
        AddMenuDialogOption(items, ASSET_ADAPTIVE, "Adaptive"s,              "Uses <game>.png first, fallback to Standard"s);
        AddMenuDialogOption(items, ASSET_CUSTOM_ONLY, "Custom Only"s,              "Uses <game>.png only, no fallback"s);
    }
    
    return items;
}

std::vector<SMenuItem> makeOptionsForStretch() {
    std::vector<SMenuItem> items;
    items.reserve(8);

    AddMenuDialogOption(items, SCALE_NO_STRETCH, "No Stretch"s,              "Pixel Perfect (256x224)"s);
    AddMenuDialogOption(items, SCALE_4_3_ASPECT, "4:3 Aspect"s,              "Stretch width only to 298"s);
    AddMenuDialogOption(items, SCALE_CRT_ASPECT, "CRT Aspect"s,              "Stretch width only to 292 (8:7 PAR)"s);
    AddMenuDialogOption(items, SCALE_4_3_FIT, "4:3 Fit"s,                 "Stretch to 320x240"s);
    AddMenuDialogOption(items, SCALE_8_7_FIT, "8:7 Fit"s,                 "Stretch to 274x240"s);
    AddMenuDialogOption(items, SCALE_4_3_FIT_CROPPED, "Cropped 4:3 Fit"s,         "Crop & Stretch to 320x240"s);

    if (screenSettings.GameScreen == GFX_TOP) {
        AddMenuDialogOption(items, SCALE_FULL, "Fullscreen"s,              "Stretch to 400x240");
        AddMenuDialogOption(items, SCALE_FULL_CROPPED, "Cropped Fullscreen"s,      "Crop & Stretch to 400x240");
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
        AddMenuDialogOption(items, static_cast<int>(EmulatedFramerate::UseRomRegion), "Default based on Game region"s, ""s);
        AddMenuDialogOption(items, static_cast<int>(EmulatedFramerate::ForceFps50), "50 FPS"s, ""s);
        AddMenuDialogOption(items, static_cast<int>(EmulatedFramerate::ForceFps60), "60 FPS"s, ""s);
        AddMenuDialogOption(items, static_cast<int>(EmulatedFramerate::Match3DS), "Match 3DS refresh rate"s, ""s);
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
                  []( int val ) { CheckAndUpdate( settings3DS.ScreenStretch, val ); });
    AddMenuCheckbox(items, "  Linear Filtering"s, settings3DS.ScreenFilter,
        []( int val ) { CheckAndUpdate( settings3DS.ScreenFilter, val ); });
    items.emplace_back(nullptr, MenuItemType::Textarea, "  (adds a slight blur, no effect when Scaling = \"No Stretch\")"s, ""s);

    AddMenuDisabledOption(items, ""s);
    AddMenuHeader2(items, "On-Screen Display"s);

    AddMenuPicker(items, "  Bezel"s, "Shown in front of game screen. Usage for custom image:\nmax 506x256px, path = \"/3ds/snes9x3ds/bezels/\",\nfilename = trimmed ROM (e.g. NBA Jam.png) or _default.png"s, 
        makeOptionsForOnScreenDisplay(), settings3DS.GameBezel, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.GameBezel, val ); });

    AddMenuCheckbox(items, "  Auto-Fit Bezel (based on \"Video Scaling\")", settings3DS.GameBezelAutoFit,
        []( int val ) { CheckAndUpdate( settings3DS.GameBezelAutoFit, val ); });



    int gameBorderPickerId = 1500;
    AddMenuPicker(items, "  Border"s, "Shown behind game screen. Usage for custom image:\nmax 400x240px, path = \"/3ds/snes9x3ds/borders/\",\nfilename = trimmed ROM (e.g. NBA Jam.png) or _default.png"s, 
        makeOptionsForOnScreenDisplay(), settings3DS.GameBorder, DIALOG_TYPE_INFO, true,
                    [gameBorderPickerId, &menuTab, &currentMenuTab]( int val ) { 
                        if (CheckAndUpdate(settings3DS.GameBorder, val)) {
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
                        if (CheckAndUpdate(settings3DS.SecondScreenContent, val)) {
                            SMenuTab *currentTab = &menuTab[currentMenuTab]; 
                            menu3dsUpdateGaugeVisibility(currentTab, secondScreenPickerId, val != ASSET_NONE ? OPACITY_STEPS : GAUGE_DISABLED_VALUE);
                        }
                    }, secondScreenPickerId
                );

    AddMenuGauge(items, "  Cover Opacity"s, 1, settings3DS.SecondScreenContent !=  ASSET_NONE ? OPACITY_STEPS :GAUGE_DISABLED_VALUE, settings3DS.SecondScreenOpacity,
                    []( int val ) { CheckAndUpdate( settings3DS.SecondScreenOpacity, val ); });
        
    AddMenuDisabledOption(items, ""s);

    AddMenuHeader1(items, "GAME-SPECIFIC SETTINGS"s);
    AddMenuHeader2(items, "Video"s);
    AddMenuPicker(items, "  Frameskip"s, "Try changing this if the game runs slow. Skipping frames helps it run faster, but less smooth."s, 
        makePickerOptions({"Disabled", "Enabled (max 1 frame)", "Enabled (max 2 frames)", "Enabled (max 3 frames)", "Enabled (max 4 frames)"}), settings3DS.MaxFrameSkips, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.MaxFrameSkips, val ); });
    AddMenuPicker(items, "  Framerate"s, "Some games run at 50 or 60 FPS by default. Override if required."s, makeOptionsForFrameRate(), static_cast<int>(settings3DS.ForceFrameRate), DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.ForceFrameRate, static_cast<EmulatedFramerate>(val) ); });
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
                    CheckAndUpdate( settings3DS.UseGlobalVolume, val ); 
                    if (settings3DS.UseGlobalVolume)
                        settings3DS.GlobalVolume = settings3DS.Volume; 
                    else
                        settings3DS.Volume = settings3DS.GlobalVolume; 
                });
    
    AddMenuDisabledOption(items, ""s);

    AddMenuHeader2(items, "Save Data"s);

    AddMenuCheckbox(items, "  Automatically save state on exit and load state on start"s, settings3DS.AutoSavestate,
        []( int val ) { CheckAndUpdate( settings3DS.AutoSavestate, val ); });
    items.emplace_back(nullptr, MenuItemType::Textarea, "  (creates an *.auto.frz file inside \"savestates\" directory)"s, ""s);

    AddMenuPicker(items, "  SRAM Auto-Save Delay"s, "Try 60 seconds or Disabled if the game saves SRAM to SD card too frequently."s, makeOptionsForAutoSaveSRAMDelay(), settings3DS.SRAMSaveInterval, DIALOG_TYPE_INFO, true,
                  []( int val ) { CheckAndUpdate( settings3DS.SRAMSaveInterval, val ); });
    AddMenuCheckbox(items, "  Force SRAM Write on Pause"s, settings3DS.ForceSRAMWriteOnPause,
                    []( int val ) { CheckAndUpdate( settings3DS.ForceSRAMWriteOnPause, val ); });

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
                    CheckAndUpdate( settings3DS.UseGlobalEmuControlKeys, val ); 
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
                uint32 v = static_cast<uint32>(val);
                if (settings3DS.UseGlobalEmuControlKeys)
                    CheckAndUpdate( settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0], v );
                else
                    CheckAndUpdate( settings3DS.ButtonHotkeys[i].MappingBitmasks[0], v );
            }, hotkeyPickerGroupId
        );
    }

    AddMenuDisabledOption(items, ""s);

    AddMenuHeader1(items, "BUTTON CONFIGURATION"s);
    AddMenuCheckbox(items, "  Apply button mappings to all games"s, settings3DS.UseGlobalButtonMappings,
                []( int val ) 
                { 
                    CheckAndUpdate( settings3DS.UseGlobalButtonMappings, val ); 
                    
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
                    CheckAndUpdate( settings3DS.UseGlobalTurbo, val ); 
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
                    if (CheckAndUpdate(settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalBindCirclePad : settings3DS.BindCirclePad, val)) {
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
        AddMenuHeader1(items, ""s);

        for (uint32 i = 0; i < MAX_CHEATS && i < Cheat.num_cheats; i++) {
            items.emplace_back(nullptr, MenuItemType::Checkbox, "  " + std::string(Cheat.c[i].name), std::string(Cheat.c[i].cheat_code), Cheat.c[i].enabled ? 1 : 0);
        }
    }
    else {
        static char message[_MAX_PATH];
        snprintf(message, sizeof(message),
            "\nNo cheats found for this game. To enable cheats, copy\n"
            "\"%s.chx\" (or *.cht) into folder \"%s\" on your sd card.\n"
            "\n\nGame-Genie and Pro Action Replay Codes are supported.\n"
            "Format for *.chx is [Y/N],[CheatCode],[Name].\n"
            "See %s for more info\n"
            "\n\nCheat collection (roughly tested): %s",
            file3dsGetTrimmedFileBasename(Memory.ROMFilename, false).c_str(),
            "3ds/snes9x_3ds/cheats",
            "github.com/matbo87/snes9x_3ds-assets",
            "https://github.com/matbo87/snes9x_3ds-assets/releases/download/v0.1.0/cheats.zip");

        items.emplace_back(nullptr, MenuItemType::Textarea, message, ""s);
    }
}

//----------------------------------------------------------------------
// Read/write all possible game specific settings.
//----------------------------------------------------------------------
bool settingsReadWriteFullListByGame(bool writeMode)
{
    std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ".cfg", "configs");
    
    BufferedFileWriter stream;
    
    if (path.empty()) {
        return false;
    }

    if (writeMode) {
        if (!stream.open(path.c_str(), "w"))
            return false;
    } else {
        if (!stream.open(path.c_str(), "r"))
            return false;
    }

    char version[16];
    snprintf(version, sizeof(version), "%.1f", GAME_CONFIG_FILE_TARGET_VERSION);
    config3dsReadWriteString(stream, writeMode, "#v%s\n", "#v%15[^\n]\n", version);

    // if writing, we are definitely on the latest version
    // if reading, we parse what we just read into 'version'
    float detectedConfigVersion = writeMode 
        ? GAME_CONFIG_FILE_TARGET_VERSION 
        : config3dsGetVersionFromFile(true, version);
    config3dsReadWriteInt32(stream, writeMode, "# Do not modify this file or risk losing your settings.\n", NULL, 0, 0, detectedConfigVersion);
    config3dsReadWriteInt32(stream, writeMode, "Frameskips=%d\n", &settings3DS.MaxFrameSkips, 0, 4);

    int tmp = static_cast<int>(settings3DS.ForceFrameRate);
    config3dsReadWriteInt32(stream, writeMode, "Framerate=%d\n", &tmp, 0, static_cast<int>(EmulatedFramerate::Count) - 1);
    settings3DS.ForceFrameRate = static_cast<EmulatedFramerate>(tmp);
    
    config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.Volume, 0, 8);
    config3dsReadWriteInt32(stream, writeMode, "PalFix=%d\n", &settings3DS.PaletteFix, 0, 3);
    config3dsReadWriteInt32(stream, writeMode, "AutoSavestate=%d\n", &settings3DS.AutoSavestate, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "SRAMInterval=%d\n", &settings3DS.SRAMSaveInterval, 0, 4);
    config3dsReadWriteInt32(stream, writeMode, "ForceSRAMWrite=%d\n", &settings3DS.ForceSRAMWriteOnPause, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "BindCirclePad=%d\n", &settings3DS.BindCirclePad, 0, 1);
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
    char globalConfig[_MAX_PATH];
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
    config3dsReadWriteString(stream, writeMode, "#v%s\n", "#v%15[^\n]\n", version);

    // if writing, we are definitely on the latest version
    // if reading, we parse what we just read into 'version'
    float detectedConfigVersion = writeMode 
        ? GLOBAL_CONFIG_FILE_TARGET_VERSION 
        : config3dsGetVersionFromFile(true, version);

    config3dsReadWriteInt32(stream, writeMode, "# Do not modify this file or risk losing your settings.\n", NULL, 0, 0);
    int screen = (int)settings3DS.GameScreen;
    config3dsReadWriteInt32(stream, writeMode, "GameScreen=%d\n", &screen, 0, 1);

    if (!writeMode) {
        settings3DS.GameScreen = (gfxScreen_t)screen;
    }
    
    config3dsReadWriteInt32(stream, writeMode, "Theme=%d\n", &settings3DS.Theme, 0, TOTALTHEMECOUNT - 1);
    config3dsReadWriteInt32(stream, writeMode, "GameThumbnailType=%d\n", &settings3DS.GameThumbnailType, 0, 3);
    config3dsReadWriteInt32(stream, writeMode, "ScreenStretch=%d\n", &settings3DS.ScreenStretch, 0, 7);
    config3dsReadWriteInt32(stream, writeMode, "ScreenFilter=%d\n", &settings3DS.ScreenFilter, 0, 1, detectedConfigVersion);
    config3dsReadWriteInt32(stream, writeMode, "GameBezel=%d\n", &settings3DS.GameBezel, ASSET_NONE, ASSET_COUNT - 1, detectedConfigVersion);
    config3dsReadWriteInt32(stream, writeMode, "GameBezelAutoFit=%d\n", &settings3DS.GameBezelAutoFit, 0, 1, detectedConfigVersion);
    config3dsReadWriteInt32(stream, writeMode, "SecondScreenContent=%d\n", &settings3DS.SecondScreenContent, ASSET_NONE, ASSET_COUNT - 1);
    config3dsReadWriteInt32(stream, writeMode, "SecondScreenOpacity=%d\n", &settings3DS.SecondScreenOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteInt32(stream, writeMode, "GameBorder=%d\n", &settings3DS.GameBorder, ASSET_NONE, ASSET_COUNT - 1);
    config3dsReadWriteInt32(stream, writeMode, "GameBorderOpacity=%d\n", &settings3DS.GameBorderOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteInt32(stream, writeMode, "Disable3DSlider=%d\n", &settings3DS.Disable3DSlider, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "Font=%d\n", &settings3DS.Font, 0, 2);
    config3dsReadWriteInt32(stream, writeMode, "LogFileEnabled=%d\n", &settings3DS.LogFileEnabled, 0, 1, detectedConfigVersion);

    char formatBuf[64];
    snprintf(formatBuf, sizeof(formatBuf), "DefaultDir=%%%zu[^\n]\n", sizeof(settings3DS.defaultDir) - 1);
    config3dsReadWriteString(stream, writeMode, "DefaultDir=%s\n", formatBuf, settings3DS.defaultDir);
    snprintf(formatBuf, sizeof(formatBuf), "LastSelectedDir=%%%zu[^\n]\n", sizeof(settings3DS.lastSelectedDir) - 1);
    config3dsReadWriteString(stream, writeMode, "LastSelectedDir=%s\n", formatBuf, settings3DS.lastSelectedDir);
    snprintf(formatBuf, sizeof(formatBuf), "LastSelectedFilename=%%%zu[^\n]\n", sizeof(settings3DS.lastSelectedFilename) - 1);
    config3dsReadWriteString(stream, writeMode, "LastSelectedFilename=%s\n", formatBuf, settings3DS.lastSelectedFilename);

    config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.GlobalVolume, 0, 8);
    config3dsReadWriteInt32(stream, writeMode, "GlobalBindCirclePad=%d\n", &settings3DS.GlobalBindCirclePad, 0, 1);

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

    config3dsReadWriteInt32(stream, writeMode, "UseGlobalButtonMappings=%d\n", &settings3DS.UseGlobalButtonMappings, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "UseGlobalTurbo=%d\n", &settings3DS.UseGlobalTurbo, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "UseGlobalVolume=%d\n", &settings3DS.UseGlobalVolume, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "UseGlobalEmuControlKeys=%d\n", &settings3DS.UseGlobalEmuControlKeys, 0, 1);

    return true;
}

//----------------------------------------------------------------------
// Save settings by game.
//----------------------------------------------------------------------
bool settingsSave(bool includeGameSettings)
{
    if (!settings3DS.dirty) return true;

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
    
    settings3DS.dirty = false;

    return true;
}

//-------------------------------------------------------
// Load the ROM and reset the CPU.
//-------------------------------------------------------
bool emulatorLoadRom(bool currentGamePaused)
{
    // save current game state first
    if (currentGamePaused) {
        settingsSave(true);

        if (settings3DS.AutoSavestate) {
            impl3dsSaveStateAuto();
        }
    }

    char romFileNameFullPath[_MAX_PATH];
    snprintf(romFileNameFullPath, sizeof(romFileNameFullPath), "%s%s", file3dsGetCurrentDir(), romFileName);

    // when impl3dsLoadROM fails our previous game (if any) is also unusable
    // therefore we always set ROMCRC32 to 0
    Memory.ROMCRC32 = 0; 
    bool loaded = impl3dsLoadROM(romFileNameFullPath);

    if (!loaded || !Memory.ROMCRC32) {
        return false;
    }

    // update global config
    snprintf(settings3DS.lastSelectedDir, sizeof(settings3DS.lastSelectedDir), "%s", file3dsGetCurrentDir());
    snprintf(settings3DS.lastSelectedFilename, sizeof(settings3DS.lastSelectedFilename), "%s", romFileName);
    
    settings3DS.dirty = true;
    settingsResetGameDefaults();

    // if file exists, overwrite the defaults
    // if not, stay on defaults
    cfgFileAvailable[1] = settingsReadWriteFullListByGame(false);

    settingsUpdateAllSettings(true);
    
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


void fillFileMenuFromFileNames(std::vector<SMenuItem>& fileMenu, const std::vector<DirectoryEntry>& romFileNames, const DirectoryEntry*& selectedEntry) {
    fileMenu.clear();
    fileMenu.reserve(romFileNames.size());

    for (size_t i = 0; i < romFileNames.size(); ++i) {
        const DirectoryEntry& entry = romFileNames[i];
        std::string prefix;

        switch (entry.Type) {
            case FileEntryType::ChildDirectory:
                prefix = "  \x01 ";
                break;
            case FileEntryType::ParentDirectory:
                prefix = "";
                break;
            default:
                prefix = "  ";
                break;
        }

        fileMenu.emplace_back( [&entry, &selectedEntry]( int val ) {
            selectedEntry = &entry;
        }, MenuItemType::Action, prefix + std::string(entry.Filename), ""s, 99999);
    }
}

void setupMenu(std::vector<SMenuTab>& menuTab, std::vector<DirectoryEntry>& romFileNames, const DirectoryEntry*& selectedDirectoryEntry, int& currentMenuTab, bool currentGamePaused) {
    int requiredTabs = currentGamePaused ? 5 : 2;
    int fileMenuTabIndex = currentGamePaused ? 4 : 1;
    
    // only reallocate if the size grows, otherwise reuse the buffer
    if (menuTab.size() != requiredTabs) {
        menuTab.resize(requiredTabs);
    }

    menuTab[0].SetTitle("Emulator");
    menuTab[0].SubTitle.clear();

    makeEmulatorMenu(menuTab[0].MenuItems, menuTab, currentMenuTab, currentGamePaused);

    if (!currentGamePaused) {
        bool success = file3dsGetFiles(romFileNames);
        int selectedItemIndex = findLastSelected(romFileNames, settings3DS.lastSelectedFilename);
        
        menu3dsSetLastSelectedIndexByTab("Load Game", selectedItemIndex);
    } else {        
        auto tab = [&](int index, const char* title) -> std::vector<SMenuItem>& {
            menuTab[index].FirstItemIndex = 0;
            menuTab[index].SelectedItemIndex = 0;
            menuTab[index].SetTitle(title);
            menuTab[index].SubTitle.clear();
            return menuTab[index].MenuItems;
        };

        makeOptionMenu(tab(1, "Settings"), menuTab, currentMenuTab);
        makeControlsMenu(tab(2, "Controls"), menuTab, currentMenuTab);
        makeCheatMenu(tab(3, "Cheats"));
    }

    SMenuTab& fileMenu = menuTab[fileMenuTabIndex];
    
    fileMenu.SetTitle("Load Game");
    fileMenu.SubTitle.assign(file3dsGetCurrentDir());
    fillFileMenuFromFileNames(fileMenu.MenuItems, romFileNames, selectedDirectoryEntry);

    for (int i = 0; i < menuTab.size(); i++) {
        int lastSelectedItemIndex = menu3dsGetLastSelectedIndexByTab(menuTab[i].Title);
        menu3dsSetSelectedItemByIndex(menuTab[i], lastSelectedItemIndex);
    }
}

void updateFileMenuTab(std::vector<SMenuTab>& menuTab, std::vector<DirectoryEntry>& romFileNames, const DirectoryEntry*& selectedDirectoryEntry, const std::string& lastSubDirectory) {
    SMenuTab& fileMenuTab = menuTab.back();
    
    file3dsGetFiles(romFileNames);
    fillFileMenuFromFileNames(fileMenuTab.MenuItems, romFileNames, selectedDirectoryEntry);
    fileMenuTab.SubTitle.assign(file3dsGetCurrentDir());
    
    if (!lastSubDirectory.empty()) {
        int selectedItemIndex = findLastSelected(romFileNames, lastSubDirectory.c_str());
        menu3dsSetSelectedItemByIndex(fileMenuTab, selectedItemIndex);
    } else {
        menu3dsSetSelectedItemByIndex(fileMenuTab, 0);
    }
}

int showFileMenuOptions(SMenuTab& dialogTab, bool& isDialog, int& currentMenuTab) {
    SMenuTab *currentTab = &menuTab[currentMenuTab];
    std::string selectedFileName;

    if (romFileNames[currentTab->SelectedItemIndex].Type == FileEntryType::File) {
        selectedFileName = romFileNames[currentTab->SelectedItemIndex].Filename;
    }

    bool hasDeleteGameOption = !selectedFileName.empty() && !(strcmp(selectedFileName.c_str(), settings3DS.lastSelectedFilename) == 0);
    
    int option = menu3dsShowDialog(
        dialogTab, isDialog, currentMenuTab, menuTab, 
        "File Menu Options", 
        "If no default directory is set, the file menu will show the directory of the last selected game."s, 
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
        std::string message = "Do you really want to remove \"" + selectedFileName +  "\" from your SD card?";
        bool confirmed = confirmDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Delete Game", message, false);

        if (confirmed) {
            std::string path = std::string(file3dsGetCurrentDir()) + selectedFileName;

            if (std::remove(path.c_str()) == 0) {
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Success", selectedFileName + " removed from SD card.", Themes[settings3DS.Theme].dialogColorSuccess, makeOptionsForOk(), -1, false);
                currentTab->MenuItems.erase(currentTab->MenuItems.begin() + currentTab->SelectedItemIndex);
                romFileNames.erase(romFileNames.begin() + currentTab->SelectedItemIndex);
            } else {
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Error", "Couldn't remove " + selectedFileName, Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk(), -1, false);
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
    bool currentGamePaused,
    const DirectoryEntry*& entry
) {
    if (entry->Type == FileEntryType::File) 
    {
        // Same game -> skip
        if (strncmp(romFileName, entry->Filename, sizeof(romFileName) - 1) == 0) 
            return;
        
        snprintf(romFileName, sizeof(romFileName), "%s", entry->Filename);

        menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Loading Game:", file3dsGetFileBasename(romFileName, false).c_str(), Themes[settings3DS.Theme].dialogColorInfo, std::vector<SMenuItem>());
        
        runNextGame = emulatorLoadRom(currentGamePaused);
        
        if (!runNextGame) {
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Loading Game:", "Oops. Unable to load Game", Themes[settings3DS.Theme].dialogColorWarn, makeOptionsForOk(), -1, false);
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
        } else {
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }
    } 
    else if (entry->Type == FileEntryType::ParentDirectory || entry->Type == FileEntryType::ChildDirectory) 
    {
        std::string lastSubDirectory = entry->Type == FileEntryType::ParentDirectory ? file3dsGetCurrentDirName() : "";
        file3dsGoUpOrDownDirectory(*entry);
        updateFileMenuTab(menuTab, romFileNames, entry, lastSubDirectory);
    }
}

void showMenu(bool currentGamePaused) {
    const DirectoryEntry* selectedDirectoryEntry = nullptr;
    static std::vector<SMenuItem> emptyCheats;

    int currentMenuTab = currentGamePaused ? menu3dsGetLastSelectedTabIndex() : 1;

    if (!gameMenuInitialized)
    {
        setupMenu(menuTab, romFileNames, selectedDirectoryEntry, currentMenuTab, currentGamePaused);
        gameMenuInitialized = currentGamePaused;
    }

    std::vector<SMenuItem>& cheatMenu = currentGamePaused ? menuTab[3].MenuItems : emptyCheats;

    if (!cheatMenu.empty()) {
        menuCopyCheats(cheatMenu, false);
        menu3dsSetCheatsIndicator(cheatMenu);
    }

    bool isDialog = false;
    bool runNextGame = false;
    SMenuTab dialogTab;

    while (GPU3DS.emulatorState == EMUSTATE_PAUSEMENU) {
        int result = menu3dsMenuSelectItem(dialogTab, isDialog, currentMenuTab, menuTab, currentGamePaused);

        // user pressed X button in file menu
        if (result == FILE_MENU_SHOW_OPTIONS) 
        {
            showFileMenuOptions(dialogTab, isDialog, currentMenuTab);
        }
        else if (result == FILE_MENU_CONTINUE_GAME) 
        {
            // user pressed START button in pause menu -> continue game
            GPU3DS.emulatorState = EMUSTATE_EMULATE;
        }
        else if (selectedDirectoryEntry) 
        {
            onDirectoryEntrySelected(dialogTab, isDialog, currentMenuTab, runNextGame, currentGamePaused, selectedDirectoryEntry);
            selectedDirectoryEntry = nullptr;
        }
    }

    // only save cheat state after resuming current game 
    if (currentGamePaused && !runNextGame) {
        bool cheatsUpdated = !cheatMenu.empty() && menuCopyCheats(cheatMenu, true);

        if (cheatsUpdated) {
            std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ".chx", "cheats", true);
            
            if (!S9xSaveCheatTextFile(path.c_str())) {
                path = file3dsGetAssociatedFilename(Memory.ROMFilename, ".cht", "cheats", true);
                S9xSaveCheatFile (path.c_str());
            }
        }

        settingsUpdateAllSettings(true);
    }

    if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
        impl3dsUpdateUiAssets();

        if (slotLoaded) {
            static char message[_MAX_PATH];
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
    log3dsWrite("==== START Logging (%s, %s) ====", getAppVersion("v"), log3dsGetCurrentDate());

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

    ui3dsInitialize();

    u16 x0 = 0;
    u16 y0 = (SCREEN_HEIGHT - FONT_HEIGHT) / 2;
    u16 x1 = screenSettings.SecondScreenWidth;
    u16 y1 = y0 + FONT_HEIGHT;

    for (int i = 0; i < 2; i ++) {
        aptMainLoop();
        ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, x0, y0, x1, y1, 0xEEEEEE, HALIGN_CENTER, "Initializing...");
        gfxScreenSwapBuffers(screenSettings.SecondScreen, false);
        gspWaitForVBlank();
    }

	log3dsWrite("romfsInit");
	rc = romfsInit();

    if (R_FAILED(rc)) {
        settings3DS.RomFsLoaded = false;
    } else {
        settings3DS.RomFsLoaded = true;
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

    if (settings3DS.RomFsLoaded) {
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

    log3dsWrite("==== END Logging (%s, %s) ====", getAppVersion("v"), log3dsGetCurrentDate());
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
    appSuspended = 0;

    snd3DS.generateSilence = false;

    frameCount60 = 60;
    frameCountTick = 0;
    framesSkippedCount = 0;

    bool skipDrawingFrame = false;

    gpu3dsResetState();
    
    bool profilingEnabled = GPU3DS.profilingMode != PROFILING_NONE; // for debugging

    if (profilingEnabled) {
        // important: consoleInit(...) sets double buffering to false
        // make sure to enable double buffering again
        consoleInit(screenSettings.SecondScreen, NULL);
    }

    // menu is currently rendered via software and may have configured the screen for 
    // a lower color depth than our other screen content rendered via GPU.
    // therefore we check the screen format first to ensure pixel data is interpreted correctly
    GSPGPU_FramebufferFormat gpuBufFmt = (GSPGPU_FramebufferFormat)DISPLAY_TRANSFER_FMT;

    if (gfxGetScreenFormat(screenSettings.SecondScreen) != gpuBufFmt) {
        gfxSetScreenFormat(screenSettings.SecondScreen, gpuBufFmt);
    }

    int totalFrames = 0;

    snd3dsStartPlaying();

	while (aptMainLoop() && GPU3DS.emulatorState == EMUSTATE_EMULATE && !appSuspended)
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
                    if (settings3DS.ForceFrameRate == EmulatedFramerate::Match3DS) {
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
                    consoleInit(screenSettings.SecondScreen, NULL);
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

    gfxSetDoubleBuffering(screenSettings.SecondScreen, true);

    snd3dsStopPlaying();
}

//---------------------------------------------------------
// Main entrypoint.
//---------------------------------------------------------
int main()
{
    menu3dsSetHotkeysData(hotkeysData);
    settingsResetGlobalDefaults();
    settingsResetGameDefaults();
    
    // load global config, overwrites defaults if file exists
    cfgFileAvailable[0] = settingsReadWriteFullListGlobal(false);
    settingsUpdateAllSettings(false);

    ui3dsUpdateScreenSettings(settings3DS.GameScreen);

    if (!emulatorInitialize()) {
        emulatorFinalize();

        return 0;
    }
    
    img3dsSetThumbMode();

    GPU3DS.emulatorState = EMUSTATE_PAUSEMENU;
    
    while (GPU3DS.emulatorState != EMUSTATE_END) {
        switch (GPU3DS.emulatorState) {
            case EMUSTATE_PAUSEMENU:
                showMenu(Memory.ROMCRC32);
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
    Bounds b = ui3dsGetBounds(screenSettings.SecondScreenWidth, screenSettings.SecondScreenWidth, FONT_HEIGHT, Position::MC, 0, 0);
    ui3dsDrawStringWithNoWrapping(screenSettings.SecondScreen, b.left, b.top, b.right, b.bottom,0xEEEEEE, HALIGN_CENTER, "clean up...");
    gfxScreenSwapBuffers(screenSettings.SecondScreen, false);
    gspWaitForVBlank();

    settingsSave(Memory.ROMCRC32);

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