#ifndef _3DSSETTINGS_H_
#define _3DSSETTINGS_H_

#include <stdio.h>
#include <array>
#include <limits.h>
#include <3ds.h>

#ifndef VERSION_MAJOR
#define VERSION_MAJOR 0
#endif

#ifndef VERSION_MINOR
#define VERSION_MINOR 0
#endif

#ifndef VERSION_MICRO
#define VERSION_MICRO 0
#endif

#define TICKS_PER_SEC (268123480)
#define TICKS_PER_FRAME_NTSC (4468724)
#define TICKS_PER_FRAME_PAL (5362469)

#define SCREEN_TOP_WIDTH        400
#define SCREEN_BOTTOM_WIDTH     320
#define SCREEN_HEIGHT           240

#define SAVESLOTS_MAX   5

#define HOTKEY_OPEN_MENU            0
#define HOTKEY_DISABLE_FRAMELIMIT   1
#define HOTKEY_SWAP_CONTROLLERS     2
#define HOTKEY_SCREENSHOT           3
#define HOTKEY_QUICK_SAVE           4
#define HOTKEY_QUICK_LOAD           5
#define HOTKEY_SAVE_SLOT_NEXT       6
#define HOTKEY_SAVE_SLOT_PREV       7
#define HOTKEYS_COUNT   8

#define OPACITY_STEPS               20
#define GAUGE_DISABLED_VALUE        -1

#define MENU_ENTRY_CONTEXT_MENU     -2
#define MENU_CONTINUE_GAME          -3

typedef enum {
    SettingToggle_Disabled = 0,
    SettingToggle_Enabled  = 1,
} SettingToggle;

typedef enum { 
    SettingScreenStretch_None,                  // 1:1 Native (256x224, 256x240)
    SettingScreenStretch_4_3_Aspect,            // Stretch width only to 298
    SettingScreenStretch_CrtAspect,             // Stretch width only to 292 (8:7 PAR)
    SettingScreenStretch_4_3_Fit,               // 4:3 Fit: Stretch to 320 x 240
    SettingScreenStretch_8_7_Fit,               // 8:7 Fit: Stretched when 224 lines, No Stretch when 240 lines (e.g. Super Mario Kart PAL)
    SettingScreenStretch_4_3_Fit_Cropped,       // Cropped 4:3 Fit: Crop & Stretch to 320 x 240
    SettingScreenStretch_Full,                  // Fullscreen: Stretch to GameScreenWidth x 240
    SettingScreenStretch_Full_Cropped,          // Cropped Fullscreen: Crop & Stretch to GameScreenWidth x 240 
} SettingScreenStretch;

typedef enum {
    SettingThumbnailMode_None,
    SettingThumbnailMode_Boxart,
    SettingThumbnailMode_Title,
    SettingThumbnailMode_Gameplay
} SettingThumbnailMode;

typedef enum {
    SettingAssetMode_None,
    SettingAssetMode_Default,       // Built-in
    SettingAssetMode_Adaptive,      // Custom, else Default
    SettingAssetMode_CustomOnly     // Custom or nothing
} SettingAssetMode;

typedef enum  {
    SettingFramerate_VSyncCpu,  // VBlank wait in paceFrame (~59.8Hz, smooth but slightly slower than original SNES speed)
    SettingFramerate_Accurate,  // sleep-based original SNES speed (NTSC ~60.1Hz / PAL 50Hz)
    SettingFramerate_VSyncGpu,  // C3D_FRAME_SYNCDRAW (~59.8Hz, results vary per game)
} SettingFramerate;

typedef enum
{
    SettingTheme_DarkMode,
    SettingTheme_RetroArch,
    SettingTheme_Original
} SettingTheme;

typedef enum
{
    SettingFont_Tempesta,
    SettingFont_Ronda,
    SettingFont_Arial
} SettingFont;

template <int Count>
struct ButtonMapping {
    std::array<u32, Count> MappingBitmasks;

    bool operator==(const ButtonMapping& other) const {
        return this->MappingBitmasks == other.MappingBitmasks;
    }

    bool IsHeld(u32 held3dsButtons) const {
        for (u32 mapping : MappingBitmasks) {
            if (mapping != 0 && (mapping & held3dsButtons) == mapping) {
                return true;
            }
        }

        return false;
    }

    void SetSingleMapping(u32 mapping) {
        SetDoubleMapping(mapping, 0);
    }

    void SetDoubleMapping(u32 mapping0, u32 mapping1) {
        if (Count > 0) {
            MappingBitmasks[0] = mapping0;
        }
        if (Count > 1) {
            MappingBitmasks[1] = mapping1;
        }
        if (Count > 2) {
            for (size_t i = 2; i < MappingBitmasks.size(); ++i) {
                MappingBitmasks[i] = 0;
            }
        }
    }
};

typedef struct {

    // --- GENERAL ---
    SettingTheme Theme;
    SettingFont Font;
    SettingThumbnailMode GameThumbnailType;
    gfxScreen_t GameScreen;
    SettingToggle Disable3DSlider;
    SettingToggle LogFileEnabled;       // Write logs to sdmc:/3ds/snes9x_3ds/debug_<APP_VERSION>_session.log
    int CurrentSaveSlot;                // remember last used save slot (1 - 5)
  
    // --- FILE MENU ---
    char defaultDir[PATH_MAX];
    char lastSelectedDir[PATH_MAX];
    char lastSelectedFilename[NAME_MAX + 1];

    // --- OSD & VIDEO ---
    SettingAssetMode     GameBezel;
    SettingToggle        GameBezelAutoFit;
    SettingAssetMode     GameBorder;                                                                           
    int                  GameBorderOpacity;     // 20 - Maxium opacity
    SettingAssetMode     SecondScreenContent;
    int                  SecondScreenOpacity;   // Default opacity

    SettingToggle        ShowFPS;

    SettingScreenStretch ScreenStretch;

    // --- GAME-SPECIFIC ---
    int                  MaxFrameSkips;         // 0 - disable,
                                                // 1 - enable (max 1 consecutive skipped frame)
                                                // 2 - enable (max 2 consecutive skipped frames)
                                                // 3 - enable (max 3 consecutive skipped frames)
                                                // 4 - enable (max 4 consecutive skipped frames)    

    SettingFramerate     ForceFrameRate;
    int                  PaletteFix;            // Palette In-Frame Changes
                                                //   1 - Enabled - Default.
                                                //   2 - Disabled - Style 1.
                                                //   3 - Disabled - Style 2.

    int                  Volume;                // 0: 100% Default volume,
                                                // 1: 125%, 2: 150%, 3: 175%, 4: 200%
                                                // 5: 225%, 6: 250%, 7: 275%, 8: 300%
    int                  GlobalVolume;
    
    SettingToggle        AutoSavestate;         // Automatically save the the current state when the emulator is closed
                                                // or the game is changed, and load it again when the game is loaded.

    int                  SRAMSaveInterval;      // SRAM Save Interval
                                                //   1 - 1 second.
                                                //   2 - 10 seconds
                                                //   3 - 60 seconds
                                                //   4 - Never

    SettingToggle        ForceSRAMWriteOnPause; // If the SRAM should be written to SD even when no change was detected.
                                                // Some games (eg. Yoshi's Island) don't detect SRAM writes correctly.

    // --- CONTROLS ---
    std::array<::ButtonMapping<1>, HOTKEYS_COUNT> ButtonHotkeys;
    std::array<::ButtonMapping<1>, HOTKEYS_COUNT> GlobalButtonHotkeys;

    SettingToggle        BindCirclePad;        // Use Circle Pad as D-Pad for gaming      
    SettingToggle        GlobalBindCirclePad;

    std::array<std::array<int, 4>, 10> ButtonMapping;
    std::array<std::array<int, 4>, 10> GlobalButtonMapping;

    std::array<int, 8>   Turbo;                // Turbo buttons: 0 - No turbo, 1 - Release/Press every alt frame.
                                               // Indexes: 0 - A, 1 - B, 2 - X, 3 - Y, 4 - L, 5 - R
    std::array<int, 8>   GlobalTurbo;
    
    SettingToggle        UseGlobalEmuControlKeys;  // Use global emulator control keys for all games
    SettingToggle        UseGlobalBindCirclePad;   // Use Circle Pad as D-Pad
    SettingToggle        UseGlobalButtonMappings;  // Use global button mappings for all games
    SettingToggle        UseGlobalTurbo;
    SettingToggle        UseGlobalVolume;

    // --- RUNTIME / CALCULATED ---
    // Not saved to config
    const char           *RootDir;

    gfxScreen_t         SecondScreen;
    int                 GameScreenWidth;
    int                 SecondScreenWidth;

    int                  StretchWidth;
    int                  StretchHeight;
    GPU_TEXTURE_FILTER_PARAM ScreenFilter;         // GPU_NEAREST for ScreenStretch = SettingScreenStretch_None or taking screenshot
                                                   // otherwise GPU_LINEAR
    int                  CropPixels;
    long                 TicksPerFrame;

    bool                 TurboMode;                 // Toggled via hotkey
    
    bool                 isNew3DS;
    bool                 isRomFsLoaded;
    bool                 isRomLoaded;
    bool                 isDirty;                   // needs saving to disk
    bool                 cheatsDirty;               // 
    bool                 uiNeedsRebuild;            // e.g. when reset to default config
} S9xSettings3DS;

extern S9xSettings3DS settings3DS;

void settings3dsResetGlobalDefaults();
void settings3dsResetGameDefaults();
void settings3dsUpdate(bool includeGameSettings);
void settings3dsApplyScreenLayout();
void settings3dsApplyScreenStretch();

const char *settings3dsGetAppVersion(const char *prefix);

#endif
