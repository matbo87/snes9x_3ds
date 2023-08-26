#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <sys/stat.h>

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

char romFileName[_MAX_PATH];
char romFileNameLastSelected[_MAX_PATH];
bool slotLoaded = false;

char* hotkeysData[HOTKEYS_COUNT][3];
std::vector<DirectoryEntry> romFileNames; // needs to stay in scope, is there a better way?

// TODO: move thumbnail caching logic to a more appropriate place
Thread thumbnailCachingThread;
volatile bool thumbnailCachingThreadRunning = false;
volatile bool thumbnailCachingInProgress = false;

size_t cacheThumbnails(std::vector<DirectoryEntry>& romFileNames, unsigned short totalCount, const char *currentDir) {
    size_t currentCount = 0;
    int lastRomItemIndex = menu3dsGetLastRomItemIndex();

    // we want to load `offset` thumbnails before `lastRomItemIndex`
    // so roms listed before lastRomItemIndex should also get their related thumbnail sooner than without providing `offset`
    int offset = 10; 
    int cachingStartIndex = lastRomItemIndex - offset;

    if (cachingStartIndex < 0)
        cachingStartIndex = 0;
    
    for (int i = 0; i < 2; i++) {
        int start, end;

        if (i == 0) {
            start = cachingStartIndex;
            end = romFileNames.size();
        } else {
            if (cachingStartIndex == 0)
                break;
            else {
                start = 0;
                end = cachingStartIndex;
            }
        }

        for (int j = start; j < end; j++) {
            
            if (romFileNames[j].Type == FileEntryType::File) {
                std::string thumbnailFilename = file3dsGetAssociatedFilename(romFileNames[j].Filename.c_str(), ".png", "thumbnails", true);
                file3dsAddFileBufferToMemory(romFileNames[j].Filename, thumbnailFilename);
                menu3dsSetCurrentPercent(++currentCount, totalCount);
            }

            // stop current caching on exit or if current dir have been changed
            if (!thumbnailCachingThreadRunning || strncmp(currentDir, file3dsGetCurrentDir(), _MAX_PATH - 1) != 0)
                break;
        }
    }

    return currentCount;
}

void threadThumbnailCaching(void *arg) {
    bool isFirstRun = true;
    u32 msDefault = (u32)arg;
    u32 ms = msDefault;
    char currentDir[_MAX_PATH];
    std::vector<std::string> checkedDirectories;

    thumbnailCachingThreadRunning = true;
	
    while (thumbnailCachingThreadRunning)
	{
        if (isFirstRun) {
            isFirstRun = false;
        } else {
            svcSleepThread(1000000ULL * ms);
        }

        if (GPU3DS.emulatorState == EMUSTATE_EMULATE) {
            ms = 2000;
            continue;
        } else {
            ms = msDefault;
        }

        // thumbnail caching done for current dir
        if (menu3dsGetCurrentPercent() == 100) {
           ms = 1000;
           continue;
        }

        // no thumbnail caching required when no roms are in current directory 
        // or directory  has already been added to checked directories
        unsigned short totalCount = file3dsGetCurrentDirRomCount();
        snprintf(currentDir, _MAX_PATH - 1, "%s", file3dsGetCurrentDir());   
        auto it = std::find(checkedDirectories.begin(), checkedDirectories.end(), std::string(currentDir));

        if (totalCount == 0 || it != checkedDirectories.end()) {
            menu3dsSetCurrentPercent(0, 0);
            continue;
        }

        thumbnailCachingInProgress = true;

        size_t currentCount = cacheThumbnails(romFileNames, totalCount, currentDir);
        if (currentCount == totalCount) {
            checkedDirectories.emplace_back(std::string(currentDir));
        }

        thumbnailCachingInProgress = false;
    }
}

void exitThumbnailThread() {
	thumbnailCachingThreadRunning = false;

    // ensure thumbnail caching is no longer in progress
    while (thumbnailCachingInProgress) {
        svcSleepThread(1000000ULL * 100);
    }

    file3dsFinalize();

	threadJoin(thumbnailCachingThread, U64_MAX);
	threadFree(thumbnailCachingThread);
}

void initThumbnailThread() {
    if (thumbnailCachingThreadRunning) {
        exitThumbnailThread();
    }

    if (settings3DS.GameThumbnailType == 0) {
        return;
    }

    const char *type;

    switch (settings3DS.GameThumbnailType)
    {
    case 1:
        type = "boxart";
        break;
    case 2:
        type = "title";
        break;
    default:
        type = "gameplay";
        break;
    }
    
    if (!file3dsthumbnailsAvailable(type) || !file3dsSetThumbnailSubDirectories(type)) {
        settings3DS.GameThumbnailType = 0;

        return;
    }
    
    // reset caching indicator
    menu3dsSetCurrentPercent(0, -1); 

    static char file[_MAX_PATH];
    snprintf(file, _MAX_PATH - 1, "%s/%s", settings3DS.RootDir, "assets/mappings.txt");
    file3dsSetRomNameMappings(file);
    
    // cache thumbnail of last selected rom instantly
    if (romFileNameLastSelected) {
        std::string thumbnailFilename = file3dsGetAssociatedFilename(romFileNameLastSelected, ".png", "thumbnails", true);
        StoredFile file = file3dsAddFileBufferToMemory(romFileNameLastSelected, thumbnailFilename);
    }

    // values have been taken from thread-basic example of 3ds-examples
    // don't know, if adjustments in prio, stacksize, etc. would improve any kind of performance noticeably
    // anyway, system seems to run stable with the given values so far
    int i = 0;
	s32 prio = 0;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	thumbnailCachingThread = threadCreate(threadThumbnailCaching, (void*)(500), STACKSIZE, prio-1, -2, false);
}


//----------------------------------------------------------------------
// Set start screen
//----------------------------------------------------------------------
void drawStartScreen() {
    static char backgroundImage[_MAX_PATH];
    static char foregroundImage[_MAX_PATH];

    if (settings3DS.RomFsLoaded) {
        snprintf(backgroundImage, _MAX_PATH - 1, "%s/%s", "romfs:", "start-background.png");
        snprintf(foregroundImage, _MAX_PATH - 1, "%s/%s", "romfs:", "start-foreground.png");
    } else {
        snprintf(backgroundImage, _MAX_PATH - 1, "%s/%s", settings3DS.RootDir, "assets/start-background.png");
        snprintf(foregroundImage, _MAX_PATH - 1, "%s/%s", settings3DS.RootDir, "assets/start-foreground.png");
    }
        
    gfxSetScreenFormat(screenSettings.GameScreen, GSP_RGBA8_OES);
    gfxSetDoubleBuffering(screenSettings.GameScreen, false);
    clearScreen(screenSettings.GameScreen);
    gfxScreenSwapBuffers(screenSettings.GameScreen, false);
    gspWaitForVBlank();

    StoredFile startScreenBackground = file3dsAddFileBufferToMemory("startScreenBackground", std::string(backgroundImage));
    StoredFile startScreenForeground = file3dsAddFileBufferToMemory("startScreenForeground", std::string(foregroundImage));

	if (!startScreenBackground.Buffer.empty()) {
        ui3dsRenderImage(screenSettings.GameScreen, startScreenBackground.Filename.c_str(), startScreenBackground.Buffer.data(), startScreenBackground.Buffer.size(), IMAGE_TYPE::START_SCREEN);          
	}

	if (!startScreenForeground.Buffer.empty()) {
        ui3dsRenderImage(screenSettings.GameScreen, startScreenForeground.Filename.c_str(), startScreenForeground.Buffer.data(), startScreenForeground.Buffer.size(), IMAGE_TYPE::START_SCREEN, false);          
	}
}

//----------------------------------------------------------------------
// Set default buttons mapping
//----------------------------------------------------------------------
void settingsDefaultButtonMapping(std::array<std::array<int, 4>, 10>& buttonMapping)
{
    uint32 defaultButtons[] = 
    { SNES_A_MASK, SNES_B_MASK, SNES_X_MASK, SNES_Y_MASK, SNES_TL_MASK, SNES_TR_MASK, 0, 0, SNES_SELECT_MASK, SNES_START_MASK };

    for (int i = 0; i < 10; i++)
    {
        buttonMapping[i][0] = defaultButtons[i];
    }

}

void LoadDefaultSettings() {
    settings3DS.PaletteFix = 1;
    settings3DS.SRAMSaveInterval = 2;
    settings3DS.MaxFrameSkips = 1;
    settings3DS.Volume = 4;
    settings3DS.ForceFrameRate = EmulatedFramerate::UseRomRegion;

    // Reset to default button configuration first
    // to make sure a game without saved settings doesn't automatically keep
    // any button mapping changes made from the previous game
    settingsDefaultButtonMapping(settings3DS.ButtonMapping);
    settingsDefaultButtonMapping(settings3DS.GlobalButtonMapping);

    // other default settings already set in 3dssettings.h
    settings3DS.ForceSRAMWriteOnPause = 0;
    for (int i = 0; i < HOTKEYS_COUNT; ++i)
        settings3DS.ButtonHotkeys[i].SetSingleMapping(0);

    // clear all turbo buttons.
    for (int i = 0; i < 8; i++)
        settings3DS.Turbo[i] = 0;
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
    bool CheckAndUpdate( T& oldValue, const T& newValue ) {
        if ( oldValue != newValue ) {
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

    void AddMenuPicker(std::vector<SMenuItem>& items, const std::string& text, const std::string& description, const std::vector<SMenuItem>& options, int value, int backgroundColor, bool showSelectedOptionInMenu, std::function<void(int)> callback, int id = -1) {
        items.emplace_back(callback, MenuItemType::Picker, text, ""s, value, showSelectedOptionInMenu ? 1 : 0, id, description, options, backgroundColor);
    }
}

void exitEmulatorOptionSelected( int val ) {
    if ( val == 1 ) {
        GPU3DS.emulatorState = EMUSTATE_END;
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


std::vector<SMenuItem> makeOptionsForGameThumbnail() {
    std::vector<SMenuItem> items;
    int i = 0;

    AddMenuDialogOption(items, i, "None"s,                ""s);

    for (const std::string& entry : { "Boxart", "Title", "Gameplay"}) {
        i++;
        std::string type = entry;
        type[0] = std::tolower(type[0]);

        if (file3dsthumbnailsAvailable(type.c_str())) {
            AddMenuDialogOption(items, i, entry, ""s);
        } else {
            AddMenuDisabledOption(items, entry);
        }
    }

    return items;
};

std::vector<SMenuItem> makeOptionsForFont() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0, "Tempesta"s, ""s);
    AddMenuDialogOption(items, 1, "Ronda"s,    ""s);
    AddMenuDialogOption(items, 2, "Arial"s,    ""s);
    return items;
}

std::vector<SMenuItem> makeEmulatorMenu(std::vector<SMenuTab>& menuTab, int& currentMenuTab, bool& closeMenu, bool romLoaded = false) {
    std::vector<SMenuItem> items;

    if (romLoaded) {
        AddMenuHeader1(items, "CURRENT GAME"s);
        items.emplace_back([&closeMenu](int val) {
            closeMenu = true;
        }, MenuItemType::Action, "  Resume"s, ""s);


        items.emplace_back([&menuTab, &currentMenuTab, &closeMenu](int val) {
            SMenuTab dialogTab;
            bool isDialog = false;
            int result = menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Reset Console", "This will restart the game. Are you sure?", DIALOGCOLOR_RED, makeOptionsForNoYes());
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);

            if (result == 1) {
                impl3dsResetConsole();
                closeMenu = true;
            }
        }, MenuItemType::Action, "  Reset"s, ""s);


        items.emplace_back([&menuTab, &currentMenuTab](int val) {
            SMenuTab dialogTab;
            bool isDialog = false;
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Screenshot", "Now taking a screenshot...\nThis may take a while.", DIALOGCOLOR_CYAN, std::vector<SMenuItem>());

            const char *path;
            bool success = impl3dsTakeScreenshot(path, true);
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
        }, MenuItemType::Action, "  Take Screenshot"s, ""s);

        AddMenuHeader2(items, ""s);

        int groupId = 500; // necessary for radio group

        AddMenuHeader2(items, "Save and Load"s);

        AddMenuCheckbox(items, "  Automatically save state on exit and load state on start"s, settings3DS.AutoSavestate,
            []( int val ) { CheckAndUpdate( settings3DS.AutoSavestate, val ); });

        AddMenuHeader2(items, ""s);

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
                        if (CheckAndUpdate( settings3DS.CurrentSaveSlot, slot )) {
                            for (int i = 0; i < currentTab->MenuItems.size(); i++)
                            {
                                // workaround: use GaugeMaxValue for element id to update state
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
                    CheckAndUpdate( settings3DS.CurrentSaveSlot, slot );
                    slotLoaded = true;
                    closeMenu = true;
                }
            }, (state == RADIO_INACTIVE || state == RADIO_INACTIVE_CHECKED) ? MenuItemType::Disabled : MenuItemType::Action, optionText.str(), ""s, -1, groupId, groupId + slot);
        }
        AddMenuHeader2(items, ""s);
    }

    AddMenuHeader1(items, "APPEARANCE"s);

    std::string gameThumbnailMessage = "Type of thumbnails to display in \"Select ROM\" tab.";
    bool thumbnailsAvailable = false;

    for (const std::string& type : { "boxart", "title", "gameplay"}) {
        if (file3dsthumbnailsAvailable(type.c_str())) {
            thumbnailsAvailable = true;
            break;
        }
    }

    // display info message when user doesn't have provided any game thumbnails yet
    if (!thumbnailsAvailable) {
        gameThumbnailMessage += "\nNo thumbnails found. You can download assets here:\ngithub.com/matbo87/snes9x_3ds/releases";
    }

    AddMenuPicker(items, "  Game Thumbnail"s, gameThumbnailMessage, makeOptionsForGameThumbnail(), settings3DS.GameThumbnailType, DIALOGCOLOR_CYAN, true, []( int val ) {   
        bool updated = CheckAndUpdate(settings3DS.GameThumbnailType, val);
        file3dsSetThumbnailsUpdated(updated);
    });

    AddMenuPicker(items, "  Font"s, "The font used for the user interface."s, makeOptionsForFont(), settings3DS.Font, DIALOGCOLOR_CYAN, true,
                  []( int val ) { if ( CheckAndUpdate( settings3DS.Font, val ) ) { ui3dsSetFont(val); } });


    AddMenuCheckbox(items, "  Disable 3D Slider"s, settings3DS.Disable3DSlider,
        []( int val ) { CheckAndUpdate( settings3DS.Disable3DSlider, val ); });

    AddMenuPicker(items, "  Swap Screens"s, ""s, makeOptionsForNoYes(), 0, DIALOGCOLOR_CYAN, false, []( int val ) {
        ui3dsSetScreenSwapped(val == 1);
    });

    int emptyLines = romLoaded ? 1 : 5;

    for (int i = 0; i < emptyLines; i++) {
        AddMenuDisabledOption(items, ""s);
    }

    AddMenuHeader1(items, "OTHERS"s);
    AddMenuPicker(items, "  Quit Emulator"s, "Are you sure you want to quit?", makeOptionsForNoYes(), 0, DIALOGCOLOR_RED, false, exitEmulatorOptionSelected);

    return items;
}

std::vector<SMenuItem> makeOptionsForStretch() {
    std::vector<SMenuItem> items;

    AddMenuDialogOption(items, 0, "No Stretch"s,              "Pixel Perfect (256x224)"s);
    AddMenuDialogOption(items, 1, "TV-style"s,                "Stretch width only to 292px"s);

    if (screenSettings.GameScreen == GFX_TOP) {
        AddMenuDialogOption(items, 2, "4:3 Fit"s,                 "Stretch to 320x240"s);
        AddMenuDialogOption(items, 3, "Cropped 4:3 Fit"s,         "Crop & Stretch to 320x240"s);
        AddMenuDialogOption(items, 4, "Fullscreen"s,              "Stretch to 400x240");
        AddMenuDialogOption(items, 5, "Cropped Fullscreen"s,      "Crop & Stretch to 400x240");
    }

    else {
        AddMenuDialogOption(items, (settings3DS.ScreenStretch == 2) ? 2 : 4, "Fullscreen"s,                 "Stretch to 320x240"s);
        AddMenuDialogOption(items, (settings3DS.ScreenStretch == 3) ? 3 : 5, "Cropped Fullscreen"s,         "Crop & Stretch to 320x240"s);
    }
    
    return items;
}

std::vector<SMenuItem> makeOptionsforSecondScreen() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0, "None"s,    ""s);
    AddMenuDialogOption(items, 1, "Game Cover"s, ""s);
    AddMenuDialogOption(items, 2, "ROM Information"s,    ""s);
    return items;
}

std::vector<SMenuItem> makeOptionsforGameBorder() {
    std::vector<SMenuItem> items;
    AddMenuDialogOption(items, 0, "None"s,    ""s);
    AddMenuDialogOption(items, 1, "Default"s, ""s);
    AddMenuDialogOption(items, 2, "Game-Specific"s,    ""s);
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
    AddMenuDialogOption(items, 1, "1 second"s,    "May result in sound- and frameskips"s);
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

std::vector<SMenuItem> makeOptionMenu(std::vector<SMenuTab>& menuTab, int& currentMenuTab, bool& closeMenu) {
    std::vector<SMenuItem> items;

    AddMenuHeader1(items, "GENERAL SETTINGS"s);
    AddMenuHeader2(items, "Video"s);
    AddMenuPicker(items, "  Scaling"s, "Change video scaling settings"s, makeOptionsForStretch(), settings3DS.ScreenStretch, DIALOGCOLOR_CYAN, true,
                  []( int val ) { CheckAndUpdate( settings3DS.ScreenStretch, val ); });
    
    
    AddMenuDisabledOption(items, ""s);
    AddMenuHeader2(items, "On-Screen Display"s);
    int secondScreenPickerId = 1000;
    AddMenuPicker(items, "  Second Screen Content"s, "When selecting \"Game Cover\" make sure that image exists."s, makeOptionsforSecondScreen(), settings3DS.SecondScreenContent, DIALOGCOLOR_CYAN, true,
                    [secondScreenPickerId, &menuTab, &currentMenuTab]( int val ) { 
                        if (CheckAndUpdate(settings3DS.SecondScreenContent, val)) {
                            SMenuTab *currentTab = &menuTab[currentMenuTab]; 
                            menu3dsUpdateGaugeVisibility(currentTab, secondScreenPickerId, val != CONTENT_NONE ? OPACITY_STEPS : GAUGE_DISABLED_VALUE);
                        }
                    }, secondScreenPickerId
                );

    AddMenuGauge(items, "  Second Screen Opacity"s, 1, settings3DS.SecondScreenContent !=  CONTENT_NONE ? OPACITY_STEPS :GAUGE_DISABLED_VALUE, settings3DS.SecondScreenOpacity,
                    []( int val ) { CheckAndUpdate( settings3DS.SecondScreenOpacity, val ); });


    int gameBorderPickerId = 1500;
    AddMenuPicker(items, "  Game Border"s, "When selecting \"Game-specific\" make sure that image exists."s, makeOptionsforGameBorder(), settings3DS.GameBorder, DIALOGCOLOR_CYAN, true,
                    [gameBorderPickerId, &menuTab, &currentMenuTab]( int val ) { 
                        if (CheckAndUpdate(settings3DS.GameBorder, val)) {
                            SMenuTab *currentTab = &menuTab[currentMenuTab]; 
                            menu3dsUpdateGaugeVisibility(currentTab, gameBorderPickerId, val > 0 ? OPACITY_STEPS : GAUGE_DISABLED_VALUE);
                        }
                    }, gameBorderPickerId
                );

    AddMenuGauge(items, "  Game Border Opacity"s, 1, settings3DS.GameBorder > 0 ? OPACITY_STEPS : GAUGE_DISABLED_VALUE, settings3DS.GameBorderOpacity,
                    []( int val ) { CheckAndUpdate( settings3DS.GameBorderOpacity, val ); });
                    
    AddMenuDisabledOption(items, ""s);

    AddMenuHeader1(items, "GAME-SPECIFIC SETTINGS"s);
    AddMenuHeader2(items, "Video"s);
    AddMenuPicker(items, "  Frameskip"s, "Try changing this if the game runs slow. Skipping frames helps it run faster, but less smooth."s, makeOptionsForFrameskip(), settings3DS.MaxFrameSkips, DIALOGCOLOR_CYAN, true,
                  []( int val ) { CheckAndUpdate( settings3DS.MaxFrameSkips, val ); });
    AddMenuPicker(items, "  Framerate"s, "Some games run at 50 or 60 FPS by default. Override if required."s, makeOptionsForFrameRate(), static_cast<int>(settings3DS.ForceFrameRate), DIALOGCOLOR_CYAN, true,
                  []( int val ) { CheckAndUpdate( settings3DS.ForceFrameRate, static_cast<EmulatedFramerate>(val) ); });
    AddMenuPicker(items, "  In-Frame Palette Changes"s, "Try changing this if some colours in the game look off."s, makeOptionsForInFramePaletteChanges(), settings3DS.PaletteFix, DIALOGCOLOR_CYAN, true,
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

    AddMenuHeader2(items, "Save Data (SRAM)"s);
    AddMenuPicker(items, "  SRAM Auto-Save Delay"s, "Try 60 seconds or Disabled this if the game saves SRAM to SD card too frequently."s, makeOptionsForAutoSaveSRAMDelay(), settings3DS.SRAMSaveInterval, DIALOGCOLOR_CYAN, true,
                  []( int val ) { CheckAndUpdate( settings3DS.SRAMSaveInterval, val ); });
    AddMenuCheckbox(items, "  Force SRAM Write on Pause"s, settings3DS.ForceSRAMWriteOnPause,
                    []( int val ) { CheckAndUpdate( settings3DS.ForceSRAMWriteOnPause, val ); });
    AddMenuDisabledOption(items, "  (some games like Yoshi's Island require this)"s);

    AddMenuDisabledOption(items, ""s);

    return items;
};

std::vector<SMenuItem> makeControlsMenu(std::vector<SMenuTab>& menuTab, int& currentMenuTab, bool& closeMenu) {
    std::vector<SMenuItem> items;
    const char *t3dsButtonNames[10];
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
            settings3DS.UseGlobalEmuControlKeys ? settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0] : settings3DS.ButtonHotkeys[i].MappingBitmasks[0], DIALOGCOLOR_CYAN, true, 
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
    AddMenuCheckbox(items, "Apply button mappings to all games"s, settings3DS.UseGlobalButtonMappings,
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
    AddMenuCheckbox(items, "Apply rapid fire settings to all games"s, settings3DS.UseGlobalTurbo,
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
                makeOptionsForCirclePad(), settings3DS.UseGlobalButtonMappings ? settings3DS.GlobalBindCirclePad : settings3DS.BindCirclePad, DIALOGCOLOR_CYAN, true,
                  [hotkeyPickerGroupId, &closeMenu, &menuTab, &currentMenuTab]( int val ) { 
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
    return items;
}

void menuSetupCheats(std::vector<SMenuItem>& cheatMenu);

std::vector<SMenuItem> makeCheatMenu() {
    std::vector<SMenuItem> items;
    menuSetupCheats(items);
    return items;
};


//----------------------------------------------------------------------
// Update settings.
//----------------------------------------------------------------------

bool settingsUpdateAllSettings(bool updateGameSettings = true)
{
    bool settingsChanged = false;
    
    if (settings3DS.ScreenStretch == 1) // TV Style
    {
        settings3DS.StretchWidth = 292;       
        settings3DS.StretchHeight = -1;
        settings3DS.CropPixels = 0;
    }
    else if (settings3DS.ScreenStretch == 2) // 4:3 Fit
    {
        settings3DS.StretchWidth = 320;
        settings3DS.StretchHeight = SCREEN_HEIGHT;
        settings3DS.CropPixels = 0;
    }
    else if (settings3DS.ScreenStretch == 3) // Cropped 4:3 Fit
    {
        settings3DS.StretchWidth = 320;
        settings3DS.StretchHeight = SCREEN_HEIGHT;
        settings3DS.CropPixels = 8;
    }
    else if (settings3DS.ScreenStretch == 4) // Fullscreen
    {
        settings3DS.StretchWidth = screenSettings.GameScreenWidth;
        settings3DS.StretchHeight = SCREEN_HEIGHT;
        settings3DS.CropPixels = 0;
    }
    else if (settings3DS.ScreenStretch == 5) // Cropeed Fullscreen
    {
        settings3DS.StretchWidth = screenSettings.GameScreenWidth;
        settings3DS.StretchHeight = SCREEN_HEIGHT;
        settings3DS.CropPixels = 8;
    } else {
         // No Stretch / Pixel Perfect
        settings3DS.StretchWidth = 256;
        settings3DS.StretchHeight = -1;    
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

//----------------------------------------------------------------------
// Read/write all possible game specific settings.
//----------------------------------------------------------------------
bool settingsReadWriteFullListByGame(bool writeMode)
{
    if (!writeMode) {
        // set default values first.
        LoadDefaultSettings();
    }

    BufferedFileWriter stream;
    std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ".cfg", "configs");

    if (writeMode) {
        if (!stream.open(path.c_str(), "w"))
            return false;
    } else {
        if (!stream.open(path.c_str(), "r"))
            return false;
    }

    config3dsReadWriteInt32(stream, writeMode, "#v1\n", NULL, 0, 0);
    config3dsReadWriteInt32(stream, writeMode, "# Do not modify this file or risk losing your settings.\n", NULL, 0, 0);
    config3dsReadWriteInt32(stream, writeMode, "Frameskips=%d\n", &settings3DS.MaxFrameSkips, 0, 4);

    int tmp = static_cast<int>(settings3DS.ForceFrameRate);
    config3dsReadWriteInt32(stream, writeMode, "Framerate=%d\n", &tmp, 0, static_cast<int>(EmulatedFramerate::Count) - 1);
    settings3DS.ForceFrameRate = static_cast<EmulatedFramerate>(tmp);
    
    config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.Volume, 0, 8);
    config3dsReadWriteInt32(stream, writeMode, "PalFix=%d\n", &settings3DS.PaletteFix, 0, 3);
    config3dsReadWriteInt32(stream, writeMode, "SRAMInterval=%d\n", &settings3DS.SRAMSaveInterval, 0, 4);
    config3dsReadWriteInt32(stream, writeMode, "ForceSRAMWrite=%d\n", &settings3DS.ForceSRAMWriteOnPause, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "BindCirclePad=%d\n", &settings3DS.BindCirclePad, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "LastSaveSlot=%d\n", &settings3DS.CurrentSaveSlot, 0, 5);

    static const char *buttonName[10] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "SELECT","START"};
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::ostringstream oss;
            oss << "ButtonMap" << buttonName[i] << "_" << j << "=%d\n";
            config3dsReadWriteInt32(stream, writeMode, oss.str().c_str(), &settings3DS.ButtonMapping[i][j]);
        }
    }

    static const char *turboButtonName[8] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    for (int i = 0; i < 8; ++i) {
        std::ostringstream oss;
        oss << "Turbo" << turboButtonName[i] << "=%d\n";
        config3dsReadWriteInt32(stream, writeMode, oss.str().c_str(), &settings3DS.Turbo[i], 0, 10);
    }

    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        if (strlen(hotkeysData[i][0])) {
            std::ostringstream oss;
            oss << "ButtonMapping" << hotkeysData[i][0] << "_0" << "=%d\n";
            config3dsReadWriteBitmask(stream, writeMode, oss.str().c_str(), &settings3DS.ButtonHotkeys[i].MappingBitmasks[0]);
        }
    }

    stream.close();
    return true;
}


//----------------------------------------------------------------------
// Read/write all possible global settings.
//----------------------------------------------------------------------
bool settingsReadWriteFullListGlobal(bool writeMode)
{
    char emulatorConfig[_MAX_PATH];
    snprintf(emulatorConfig, _MAX_PATH - 1, "%s/%s", settings3DS.RootDir, "settings.cfg");
    
    BufferedFileWriter stream;

    if (writeMode) {
        if (!stream.open(emulatorConfig, "w"))
            return false;
    } else {
        if (!stream.open(emulatorConfig, "r"))
            return false;
    }

    config3dsReadWriteInt32(stream, writeMode, "#v1\n", NULL, 0, 0);
    config3dsReadWriteInt32(stream, writeMode, "# Do not modify this file or risk losing your settings.\n", NULL, 0, 0);
    int screen = static_cast<int>(settings3DS.GameScreen);
    config3dsReadWriteInt32(stream, writeMode, "GameScreen=%d\n", &screen, 0, 1);
    screenSettings.GameScreen = static_cast<gfxScreen_t>(screen);
    settings3DS.GameScreen = screenSettings.GameScreen;
    config3dsReadWriteInt32(stream, writeMode, "GameThumbnailType=%d\n", &settings3DS.GameThumbnailType, 0, 3);
    config3dsReadWriteInt32(stream, writeMode, "ScreenStretch=%d\n", &settings3DS.ScreenStretch, 0, 7);
    config3dsReadWriteInt32(stream, writeMode, "SecondScreenContent=%d\n", &settings3DS.SecondScreenContent, 0, 2);
    config3dsReadWriteInt32(stream, writeMode, "SecondScreenOpacity=%d\n", &settings3DS.SecondScreenOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteInt32(stream, writeMode, "GameBorder=%d\n", &settings3DS.GameBorder, 0, 2);
    config3dsReadWriteInt32(stream, writeMode, "GameBorderOpacity=%d\n", &settings3DS.GameBorderOpacity, 1, OPACITY_STEPS);
    config3dsReadWriteInt32(stream, writeMode, "Disable3DSlider=%d\n", &settings3DS.Disable3DSlider, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "Font=%d\n", &settings3DS.Font, 0, 2);

    // Fixes the bug where we have spaces in the directory name
    config3dsReadWriteString(stream, writeMode, "Dir=%s\n", "Dir=%1000[^\n]\n", file3dsGetCurrentDir());
    config3dsReadWriteString(stream, writeMode, "ROM=%s\n", "ROM=%1000[^\n]\n", romFileNameLastSelected);

    config3dsReadWriteInt32(stream, writeMode, "AutoSavestate=%d\n", &settings3DS.AutoSavestate, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "Vol=%d\n", &settings3DS.GlobalVolume, 0, 8);
    config3dsReadWriteInt32(stream, writeMode, "GlobalBindCirclePad=%d\n", &settings3DS.GlobalBindCirclePad, 0, 1);

    static const char *buttonName[10] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR", "SELECT","START"};
    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::ostringstream oss;
            oss << "ButtonMap" << buttonName[i] << "_" << j << "=%d\n";
            config3dsReadWriteInt32(stream, writeMode, oss.str().c_str(), &settings3DS.GlobalButtonMapping[i][j]);
        }
    }
    
    static const char *turboButtonName[8] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR"};
    for (int i = 0; i < 8; ++i) {
        std::ostringstream oss;
        oss << "Turbo" << turboButtonName[i] << "=%d\n";
        config3dsReadWriteInt32(stream, writeMode, oss.str().c_str(), &settings3DS.GlobalTurbo[i], 0, 10);
    }

    for (int i = 0; i < HOTKEYS_COUNT; ++i) {
        if (strlen(hotkeysData[i][0])) {
            std::ostringstream oss;
            oss << "ButtonMapping" << hotkeysData[i][0] << "_0" << "=%d\n";
            config3dsReadWriteBitmask(stream, writeMode, oss.str().c_str(), &settings3DS.GlobalButtonHotkeys[i].MappingBitmasks[0]);
        }
    }

    config3dsReadWriteInt32(stream, writeMode, "UseGlobalButtonMappings=%d\n", &settings3DS.UseGlobalButtonMappings, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "UseGlobalTurbo=%d\n", &settings3DS.UseGlobalTurbo, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "UseGlobalVolume=%d\n", &settings3DS.UseGlobalVolume, 0, 1);
    config3dsReadWriteInt32(stream, writeMode, "UseGlobalEmuControlKeys=%d\n", &settings3DS.UseGlobalEmuControlKeys, 0, 1);

    stream.close();
    return true;
}

//----------------------------------------------------------------------
// Save settings by game.
//----------------------------------------------------------------------
bool settingsSave(bool includeGameSettings = true)
{
    
    if (includeGameSettings)
        settingsReadWriteFullListByGame(true);

    settingsReadWriteFullListGlobal(true);
    return true;
}

//----------------------------------------------------------------------
// Load settings by game.
//----------------------------------------------------------------------
bool settingsLoad(bool includeGameSettings = true)
{
    // load and update global settings first
    //
    bool success = settingsReadWriteFullListGlobal(false);

    if (!success)
        return false;

    settingsUpdateAllSettings(false);

    if (!includeGameSettings)
        return true;


    // load and update game settings if already saved before
    //
    success = settingsReadWriteFullListByGame(false);
    
    if (success) {
        if (settingsUpdateAllSettings())
            settingsSave();
        
        return true;
    }

    if (SNESGameFixes.PaletteCommitLine == -2)
        settings3DS.PaletteFix = 1;
    else if (SNESGameFixes.PaletteCommitLine == 1)
        settings3DS.PaletteFix = 2;
    else if (SNESGameFixes.PaletteCommitLine == -1)
        settings3DS.PaletteFix = 3;

    if (Settings.AutoSaveDelay == 600)
        settings3DS.SRAMSaveInterval = 2;
    else if (Settings.AutoSaveDelay == 3600)
        settings3DS.SRAMSaveInterval = 3;

    settingsUpdateAllSettings();

    return settingsSave();
}




//-------------------------------------------------------
// Load the ROM and reset the CPU.
//-------------------------------------------------------

extern SCheatData Cheat;

bool emulatorLoadRom()
{
    char romFileNameFullPath[_MAX_PATH];
    snprintf(romFileNameFullPath, _MAX_PATH, "%s%s", file3dsGetCurrentDir(), romFileName);
        
    int currentMenuTab;
    int lastItemIndex;

    menu3dsGetCurrentTabPosition(currentMenuTab, lastItemIndex);
    // "Select ROM" tab switches from index 1 to index 4 after rom has been loaded
    menu3dsSetCurrentTabPosition(currentMenuTab == 1 ? 4 : currentMenuTab, lastItemIndex);
    
    bool loaded=impl3dsLoadROM(romFileNameFullPath);

    if (!Memory.ROMCRC32) 
        return false;
    
    if(loaded)
    {
        snd3DS.generateSilence = true;
        settingsSave(false);

        GPU3DS.emulatorState = EMUSTATE_EMULATE;
        settingsLoad();
    
        // check for valid hotkeys if circle pad binding is enabled
        if ((!settings3DS.UseGlobalButtonMappings && settings3DS.BindCirclePad) || 
            (settings3DS.UseGlobalButtonMappings && settings3DS.GlobalBindCirclePad))
            for (int i = 0; i < HOTKEYS_COUNT; ++i)
                ResetHotkeyIfNecessary(i, true);
        
        // set proper state (radio_state) for every save slot of loaded game
        for (int slot = 1; slot <= SAVESLOTS_MAX; ++slot)
            impl3dsUpdateSlotState(slot, true);
        
        menu3dsSetSecondScreenContent(NULL);
        impl3dsSetBorderImage();

        if (settings3DS.AutoSavestate)
            impl3dsLoadStateAuto();

        snd3DS.generateSilence = false;

        return true;
    }

    return false;
    
}


//----------------------------------------------------------------------
// Load all ROM file names
//----------------------------------------------------------------------
void fileGetAllFiles(std::vector<DirectoryEntry>& romFileNames)
{
    file3dsGetFiles(romFileNames, {".smc", ".sfc", ".fig"});
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
        
        cheatMenu[i+1].Text = Cheat.c[i].name;
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

    int currentMenuTab;
    bool closeMenu;

    {
        menu3dsAddTab(menuTab, "Emulator", makeEmulatorMenu(menuTab, currentMenuTab, closeMenu));
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

void menuSelectFile(void)
{
    S9xSettings3DS prevSettings3DS = settings3DS;
    std::vector<SMenuTab> menuTab;
    const DirectoryEntry* selectedDirectoryEntry = nullptr;
    setupBootupMenu(menuTab, romFileNames, selectedDirectoryEntry, true);

    int currentMenuTab;
    int lastItemIndex;
    menu3dsGetCurrentTabPosition(currentMenuTab, lastItemIndex);
    bool isDialog = false;
    SMenuTab dialogTab;
    
    gfxSetDoubleBuffering(screenSettings.SecondScreen, true);
    menu3dsSetTransferGameScreen(false);

    while (aptMainLoop() && GPU3DS.emulatorState != EMUSTATE_END) {
        menu3dsShowMenu(dialogTab, isDialog, currentMenuTab, menuTab);

        if (ui3dsGetScreenSwapped()) {
            ui3dsSetScreenSwapped(false);
            menu3dsDrawBlackScreen();
            settings3DS.GameScreen = screenSettings.GameScreen == GFX_TOP ? GFX_BOTTOM : GFX_TOP;
            ui3dsUpdateScreenSettings(settings3DS.GameScreen);
            gfxSetScreenFormat(screenSettings.SecondScreen, GSP_RGB565_OES);
            gfxSetDoubleBuffering(screenSettings.SecondScreen, true);        
            drawStartScreen();
        }

        if (file3dsGetThumbnailsUpdated()) {
            file3dsSetThumbnailsUpdated(false);

            if (thumbnailCachingThreadRunning) {
	            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Game Thumbnail", "Apply Changes...", DIALOGCOLOR_CYAN, std::vector<SMenuItem>());  
                initThumbnailThread();  
                menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
            } else {
                initThumbnailThread();
            }
        }

        if (selectedDirectoryEntry) {
            if (selectedDirectoryEntry->Type == FileEntryType::File) {
                strncpy(romFileName, selectedDirectoryEntry->Filename.c_str(), _MAX_PATH);
                strncpy(romFileNameLastSelected, romFileName, _MAX_PATH);
                menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Loading Game:", file3dsGetFileBasename(romFileName, false).c_str(), DIALOGCOLOR_CYAN, std::vector<SMenuItem>());
                
                if (!emulatorLoadRom()) {
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Select ROM", "Oops. Unable to load Game", DIALOGCOLOR_RED, makeOptionsForOk());
                    menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
                } else {
                    menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTab);
                    return;
                }

            } else if (selectedDirectoryEntry->Type == FileEntryType::ParentDirectory || selectedDirectoryEntry->Type == FileEntryType::ChildDirectory) {
                file3dsGoUpOrDownDirectory(*selectedDirectoryEntry);
                setupBootupMenu(menuTab, romFileNames, selectedDirectoryEntry, false);
            }
            selectedDirectoryEntry = nullptr;
        }
    }

    menu3dsHideMenu(dialogTab, isDialog, currentMenuTab, menuTab);

    bool settingsUpdated = false;
    if (settings3DS != prevSettings3DS) {
        settingsUpdated = settingsSave();
    }
}


//----------------------------------------------------------------------
// Menu when the emulator is paused in-game.
//----------------------------------------------------------------------
void setupPauseMenu(std::vector<SMenuTab>& menuTab, std::vector<DirectoryEntry>& romFileNames, const DirectoryEntry*& selectedDirectoryEntry, bool selectPreviousFile, int& currentMenuTab, bool& closeMenu, bool refreshFileList) {
    menuTab.clear();
    menuTab.reserve(4);

    {
        menu3dsAddTab(menuTab, "Emulator", makeEmulatorMenu(menuTab, currentMenuTab, closeMenu, true));
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
    S9xSettings3DS prevSettings3DS = settings3DS;

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
    menu3dsSetCheatsIndicator(cheatMenu);

    while (aptMainLoop() && !closeMenu && GPU3DS.emulatorState != EMUSTATE_END) {
        if (menu3dsShowMenu(dialogTab, isDialog, currentMenuTab, menuTab) == -1) {
            // user pressed B, close menu
            closeMenu = true;
        }
        
        if (ui3dsGetScreenSwapped()) {
            ui3dsSetScreenSwapped(false);
            menu3dsDrawBlackScreen();
            settings3DS.GameScreen = screenSettings.GameScreen == GFX_TOP ? GFX_BOTTOM : GFX_TOP;
            ui3dsUpdateScreenSettings(settings3DS.GameScreen);
            gfxSetScreenFormat(screenSettings.SecondScreen, GSP_RGB565_OES);
            gfxSetScreenFormat(screenSettings.GameScreen, GSP_RGBA8_OES);
            closeMenu = true;
        }

        if (file3dsGetThumbnailsUpdated()) {
            file3dsSetThumbnailsUpdated(false);
	        menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Game Thumbnail", "Apply Changes...", DIALOGCOLOR_CYAN, std::vector<SMenuItem>());        
            initThumbnailThread();
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
        }

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
                    menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Loading Game:", file3dsGetFileBasename(romFileName, false).c_str(), DIALOGCOLOR_CYAN, std::vector<SMenuItem>());
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

    bool settingsUpdated = false;
    if (settings3DS != prevSettings3DS) {
        settingsUpdated = settingsSave();
    }

    settingsUpdateAllSettings();

    if (menuCopyCheats(cheatMenu, true))
    {
        std::string path = file3dsGetAssociatedFilename(Memory.ROMFilename, ".chx", "cheats", true);
        
        if (!S9xSaveCheatTextFile(path.c_str())) {
            path = file3dsGetAssociatedFilename(Memory.ROMFilename, ".cht", "cheats", true);
            S9xSaveCheatFile (path.c_str());
        }
    }

    if (closeMenu && GPU3DS.emulatorState != EMUSTATE_END) {
        GPU3DS.emulatorState = EMUSTATE_EMULATE;

        static char message[_MAX_PATH] = "";

        if (slotLoaded) {
			snprintf(message, _MAX_PATH, "Slot #%d loaded", settings3DS.CurrentSaveSlot);
        } else if (settingsUpdated) {
			snprintf(message, _MAX_PATH, "Settings saved to %s", "SD Card");
        }
        
        menu3dsSetSecondScreenContent((settingsUpdated || slotLoaded) ? message : NULL);
        slotLoaded = false;

        impl3dsSetBorderImage();
    }

    // Loads the new ROM if a ROM was selected.
    //
    if (loadRomBeforeExit)
        if (!emulatorLoadRom()) {
            menu3dsShowDialog(dialogTab, isDialog, currentMenuTab, menuTab, "Select ROM", "Oops. Unable to load Game", DIALOGCOLOR_RED, makeOptionsForOk());
            menu3dsHideDialog(dialogTab, isDialog, currentMenuTab, menuTab);
            menuPause();
        }
}

//-------------------------------------------------------
// Sets up all the cheats to be displayed in the menu.
//-------------------------------------------------------

void menuSetupCheats(std::vector<SMenuItem>& cheatMenu)
{
    if (Cheat.num_cheats > 0) {
        AddMenuHeader1(cheatMenu, ""s);

        for (uint32 i = 0; i < MAX_CHEATS && i < Cheat.num_cheats; i++) {
            cheatMenu.emplace_back(nullptr, MenuItemType::Checkbox, std::string(Cheat.c[i].name), std::string(Cheat.c[i].cheat_code), Cheat.c[i].enabled ? 1 : 0);
        }
    }
    else {
        static char message[_MAX_PATH];
        snprintf(message, _MAX_PATH - 1,
            "\nNo cheats found for this game. To enable cheats, copy\n"
            "\"%s.chx\" (or *.cht) into folder \"%s\" on your sd card.\n"
            "\n\nGame-Genie and Pro Action Replay Codes are supported.\n"
            "Format for *.chx is [Y/N],[CheatCode],[Name].\n"
            "See %s for more info\n"
            "\n\nCheat pack (roughly tested): %s",
            file3dsGetTrimmedFileBasename(Memory.ROMFilename, false).c_str(),
            "3ds/snes9x_3ds/cheats",
            "github.com/matbo87/snes9x_3ds",
            "github.com/matbo87/snes9x_3ds/\nreleases/download/v1.50/cheats.zip");

        cheatMenu.emplace_back(nullptr, MenuItemType::Textarea, message, ""s);
    }
}

//--------------------------------------------------------
// Initialize the emulator engine and everything else.
// This calls the impl3dsInitializeCore, which executes
// initialization code specific to the emulation core.
//--------------------------------------------------------
void emulatorInitialize()
{
    file3dsInitialize();
    romFileNameLastSelected[0] = 0;
    menu3dsSetHotkeysData(hotkeysData);
    settingsLoad(false);
    ui3dsUpdateScreenSettings(screenSettings.GameScreen);

    if (!gpu3dsInitialize())
    {
        printf ("Unable to initialize GPU\n");
        exit(0);
    }

    osSetSpeedupEnable(true);

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

	Result rc = romfsInit();
    
	if (rc) {
        settings3DS.RomFsLoaded = false;
	} else {
        settings3DS.RomFsLoaded = true;
    }

    
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
    disableAptHooks();

    if (settings3DS.RomFsLoaded)
    {
        printf("romfsExit:\n");
        romfsExit();
    }

    osSetSpeedupEnable(false);

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

void updateSecondScreenContent()
{
    if (frameCountTick == 0)
        frameCountTick = svcGetSystemTick();

    if (frameCount60 == 0)
    {
        u64 newTick = svcGetSystemTick();

        if (settings3DS.SecondScreenContent == CONTENT_INFO) {
            float timeDelta = ((float)(newTick - frameCountTick))/TICKS_PER_SEC;
            int fpsmul10 = (int)((float)600 / timeDelta);

            if (framesSkippedCount)
                snprintf (frameCountBuffer, 69, "FPS: %2d.%1d (%d skipped)", fpsmul10 / 10, fpsmul10 % 10, framesSkippedCount);
            else
                snprintf (frameCountBuffer, 69, "FPS: %2d.%1d", fpsmul10 / 10, fpsmul10 % 10);

            if (ui3dsGetSecondScreenDialogState() == HIDDEN) {
                float alpha = (float)(settings3DS.SecondScreenOpacity) / OPACITY_STEPS;
                gfxSetDoubleBuffering(screenSettings.SecondScreen, false);
                menu3dsSetFpsInfo(framesSkippedCount ? DIALOGCOLOR_RED : 0xFFFFFF, alpha, frameCountBuffer);
            }
        }
        
        frameCount60 = 60;
        framesSkippedCount = 0;


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

    // start counter & wait  'maxFramesForDialog' until hiding secondScreenDialog 
    // TODO: use tick counter from libctru instead

    if (++frameCount == UINT16_MAX)
        frameCount = 0;

    if (ui3dsGetSecondScreenDialogState() == VISIBLE) {
        frameCount = 0;
        ui3dsSetSecondScreenDialogState(WAIT);
    }

    if (ui3dsGetSecondScreenDialogState() == WAIT && frameCount >= maxFramesForDialog) {
        menu3dsSetSecondScreenContent(NULL);
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
    gfxSetDoubleBuffering(screenSettings.GameScreen, true);
    gfxSetDoubleBuffering(screenSettings.SecondScreen, false);

    snd3dsStartPlaying();

	while (true)
	{
        t3dsStartTiming(1, "aptMainLoop");

        startFrameTick = svcGetSystemTick();
        aptMainLoop();

        if (GPU3DS.emulatorState == EMUSTATE_END || appSuspended)
            break;

        gpu3dsStartNewFrame();
        
        if(!settings3DS.Disable3DSlider)
        {
            gfxSet3D(true);
            gpu3dsCheckSlider();
        }
        else
            gfxSet3D(false);

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
    drawStartScreen();
    //consoleInit(GFX_TOP, NULL);
    
    gspWaitForVBlank();
    initThumbnailThread();

    menuSelectFile();
    while (aptMainLoop() && GPU3DS.emulatorState != EMUSTATE_END) {
        switch (GPU3DS.emulatorState) {
            case EMUSTATE_PAUSEMENU:
                menuPause();
                break;
            case EMUSTATE_EMULATE:
                emulatorLoop();
                break;
            default:
                GPU3DS.emulatorState = EMUSTATE_END;
        }
    }

    romFileNames.clear();

    if (thumbnailCachingThreadRunning) {
        exitThumbnailThread();
    }
    
    clearScreen(screenSettings.SecondScreen);
    gfxSetScreenFormat(screenSettings.SecondScreen, GSP_RGB565_OES);
    gpu3dsSwapScreenBuffers();
    menu3dsDrawBlackScreen();

    if (GPU3DS.emulatorState > 0 && settings3DS.AutoSavestate)
        impl3dsSaveStateAuto();

    emulatorFinalize();
    return 0;
}
