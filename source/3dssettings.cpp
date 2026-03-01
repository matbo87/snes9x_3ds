
#include <cstring>

#include "snes9x.h"
#include "3dssettings.h"
#include "3dsui.h"

S9xSettings3DS settings3DS;

void settings3dsResetGlobalDefaults() {
    settings3DS.RootDir = "sdmc:/3ds/snes9x_3ds";
    
    memset(settings3DS.defaultDir, 0, sizeof(settings3DS.defaultDir));
    memset(settings3DS.lastSelectedDir, 0, sizeof(settings3DS.lastSelectedDir));
    memset(settings3DS.lastSelectedFilename, 0, sizeof(settings3DS.lastSelectedFilename));
    
    settings3DS.Theme = SettingTheme_DarkMode;
    settings3DS.Font  = SettingFont_Tempesta;
    settings3DS.GameThumbnailType = SettingThumbnailMode_None;
    settings3DS.GameScreen = GFX_TOP;
    
    settings3DS.Disable3DSlider = SettingToggle_Disabled;
    settings3DS.LogFileEnabled = SettingToggle_Disabled;

    settings3DS.ScreenStretch = SettingScreenStretch_None;
    settings3dsApplyScreenStretch();
    
    settings3DS.TicksPerFrame = TICKS_PER_FRAME_NTSC;
    settings3DS.GlobalVolume = 4;

    settings3DS.GameBezel = SettingAssetMode_None;
    settings3DS.GameBezelAutoFit = SettingToggle_Disabled;
    settings3DS.GameBorder = SettingAssetMode_Default;
    settings3DS.GameBorderOpacity = OPACITY_STEPS / 2;
    settings3DS.SecondScreenContent = SettingAssetMode_Default;
    settings3DS.SecondScreenOpacity = OPACITY_STEPS / 2;

    settings3DS.ShowFPS = SettingToggle_Disabled;

    settings3DS.UseGlobalEmuControlKeys = SettingToggle_Enabled;
    settings3DS.UseGlobalBindCirclePad = SettingToggle_Enabled;
    settings3DS.UseGlobalButtonMappings = SettingToggle_Enabled;
    settings3DS.UseGlobalTurbo = SettingToggle_Disabled;
    settings3DS.UseGlobalVolume = SettingToggle_Disabled;

    u32 defaultButtonMapping[] = { 
      SNES_A_MASK, SNES_B_MASK, SNES_X_MASK, SNES_Y_MASK, SNES_TL_MASK, SNES_TR_MASK, 0, 0, SNES_SELECT_MASK, SNES_START_MASK 
    };

    for (int i = 0; i < 10; i++)
      settings3DS.GlobalButtonMapping[i][0] = defaultButtonMapping[i];

    settings3DS.GlobalBindCirclePad = SettingToggle_Enabled;

    for (int i = 0; i < HOTKEYS_COUNT; ++i)
      settings3DS.ButtonHotkeys[i].SetSingleMapping(0);

    for (int i = 0; i < 8; i++)
      settings3DS.GlobalTurbo[i] = 0;

    settings3DS.isDirty = true;
}

void settings3dsResetGameDefaults() {
    settings3DS.PaletteFix = 3;
    settings3DS.Volume = settings3DS.GlobalVolume;
    settings3DS.MaxFrameSkips = 1;
    settings3DS.ForceFrameRate = SettingFramerate_VSyncCpu;
    settings3DS.CurrentSaveSlot = 1;
    settings3DS.AutoSavestate = SettingToggle_Disabled;
    settings3DS.SRAMSaveInterval = 4;
    settings3DS.ForceSRAMWriteOnPause = SettingToggle_Disabled;

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

void settings3dsApplyScreenStretch() {
    settings3DS.StretchWidth = 256;
    settings3DS.StretchHeight = -1;
    settings3DS.CropPixels = 0;
    settings3DS.ScreenFilter = settings3DS.ScreenStretch == SettingScreenStretch_None ? GPU_NEAREST : GPU_LINEAR;

    switch (settings3DS.ScreenStretch)
    {
        case SettingScreenStretch_4_3_Aspect:
            settings3DS.StretchWidth = 298;
            break;

        case SettingScreenStretch_CrtAspect:
            settings3DS.StretchWidth = 292;
            break;

        case SettingScreenStretch_4_3_Fit_Cropped:
            settings3DS.CropPixels = 8;
        case SettingScreenStretch_4_3_Fit:
            settings3DS.StretchWidth = 320;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;

        case SettingScreenStretch_Full_Cropped:
            settings3DS.CropPixels = 8;
        case SettingScreenStretch_Full:
            settings3DS.StretchWidth = settings3DS.GameScreen == GFX_TOP ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;

        case SettingScreenStretch_8_7_Fit:
            settings3DS.StretchWidth = 274;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;
    }
}


void settings3dsUpdate(bool includeGameSettings)
{
    settings3dsApplyScreenStretch();

    if (includeGameSettings)
    {
        // Update frame rate
        //
        if (Settings.PAL) {
            settings3DS.TicksPerFrame = TICKS_PER_FRAME_PAL;
            settings3DS.ForceFrameRate = SettingFramerate_Accurate;
        } else {
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
}

const char *settings3dsGetAppVersion(const char *prefix) {
    static char version[64];

    if (VERSION_MICRO > 0) {
        snprintf(version, sizeof(version), "%s%d.%d.%d", prefix, VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    } else {
        snprintf(version, sizeof(version), "%s%d.%d", prefix, VERSION_MAJOR, VERSION_MINOR);
    }

    return version;
}