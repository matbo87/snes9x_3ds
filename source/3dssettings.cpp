
#include <cstring>

#include "snes9x.h"
#include "3dssettings.h"
#include "3dslcd.h"
#include "3dsui.h"

S9xSettings3DS settings3DS;

void settings3dsResetGlobalDefaults() {
    settings3DS.RootDir = "sdmc:/3ds/snes9x_3ds";
    
    memset(settings3DS.defaultDir, 0, sizeof(settings3DS.defaultDir));
    memset(settings3DS.lastSelectedDir, 0, sizeof(settings3DS.lastSelectedDir));
    memset(settings3DS.lastSelectedFilename, 0, sizeof(settings3DS.lastSelectedFilename));
    
    settings3DS.Theme = Setting::Theme::DarkMode;
    settings3DS.Font  = Setting::Font::Tempesta;
    settings3DS.GameThumbnailType = Setting::ThumbnailMode::None;
    settings3DS.GameScreen = GFX_TOP;
    
    settings3DS.Disable3DSlider = false;
    settings3DS.LogFileEnabled = false;

    settings3DS.ScreenStretch = Setting::ScreenStretch::Aspect_4_3;
    settings3DS.ScreenFilter = Setting::ScreenFilter::Smooth;
    settings3DS.CropTop = 0;
    settings3DS.CropBottom = 0;
    settings3DS.Overscan = false;
    settings3dsApplyScreenStretch();
    
    settings3DS.TicksPerFrame = TICKS_PER_FRAME_SNES_NTSC;
    settings3DS.GlobalVolume = 4;

    settings3DS.GameOverlay = Setting::AssetMode::None;
    settings3DS.GameOverlayAutoFit = false;
    settings3DS.GameScreenBg = Setting::AssetMode::Adaptive;
    settings3DS.GameScreenBgOpacity = OPACITY_STEPS / 2;
    settings3DS.SecondScreenBg = Setting::AssetMode::Adaptive;
    settings3DS.SecondScreenBgOpacity = OPACITY_STEPS / 2;

    settings3DS.ShowFPS = false;

    settings3DS.UseGlobalEmuControlKeys = true;
    settings3DS.UseGlobalBindCirclePad = true;
    settings3DS.UseGlobalButtonMappings = true;
    settings3DS.UseGlobalTurbo = false;
    settings3DS.UseGlobalVolume = false;

    u32 defaultButtonMapping[] = { 
      SNES_A_MASK, SNES_B_MASK, SNES_X_MASK, SNES_Y_MASK, SNES_TL_MASK, SNES_TR_MASK, 0, 0, SNES_SELECT_MASK, SNES_START_MASK 
    };

    for (int i = 0; i < 10; i++)
      settings3DS.GlobalButtonMapping[i][0] = defaultButtonMapping[i];

    settings3DS.GlobalBindCirclePad = true;

    for (int i = 0; i < HOTKEYS_COUNT; ++i)
      settings3DS.ButtonHotkeys[i].SetSingleMapping(0);

    for (int i = 0; i < 8; i++)
      settings3DS.GlobalTurbo[i] = 0;

    settings3DS.isDirty = true;
}

void settings3dsResetGameDefaults() {
    settings3DS.Framerate = Setting::Framerate::UseRomRegion;
    settings3DS.PaletteFix = 0;
    settings3DS.Volume = settings3DS.GlobalVolume;
    settings3DS.MaxFrameSkips = 1;
    settings3DS.CurrentSaveSlot = 1;
    settings3DS.AutoSavestate = false;
    settings3DS.SRAMSaveInterval = 0;
    settings3DS.ForceSRAMWriteOnPause = false;

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

    switch (settings3DS.ScreenStretch)
    {
        case Setting::ScreenStretch::None:
            break;

        case Setting::ScreenStretch::Aspect_4_3:
            settings3DS.StretchWidth = 298;
            break;

        case Setting::ScreenStretch::CrtAspect:
            settings3DS.StretchWidth = 292;
            break;

        case Setting::ScreenStretch::Fit_4_3:
            settings3DS.StretchWidth = 320;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;

        case Setting::ScreenStretch::Full:
            settings3DS.StretchWidth = settings3DS.GameScreen == GFX_TOP ? SCREEN_TOP_WIDTH : SCREEN_BOTTOM_WIDTH;
            settings3DS.StretchHeight = SCREEN_HEIGHT;
            break;

        case Setting::ScreenStretch::Fit_8_7:
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
            settings3DS.TicksPerFrame = settings3DS.Framerate == Setting::Framerate::ForceFps60 ? TICKS_PER_FRAME_SNES_NTSC : TICKS_PER_FRAME_SNES_PAL;
        } else {
            settings3DS.TicksPerFrame = TICKS_PER_FRAME_SNES_NTSC;
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

const char *settings3dsGetAppVersion(const char *prefix, const char *suffix) {
    static char version[64];

    if (VERSION_MICRO > 0) {
        snprintf(version, sizeof(version), "%s%d.%d.%d%s", prefix, VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO, suffix != NULL ? suffix : "");
    } else {
        snprintf(version, sizeof(version), "%s%d.%d%s", prefix, VERSION_MAJOR, VERSION_MINOR, suffix != NULL ? suffix : "");
    }

    return version;
}
