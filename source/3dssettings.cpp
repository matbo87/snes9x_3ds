#include "3dssettings.h"

bool S9xSettings3DS::operator==(const S9xSettings3DS& other) const {
  return ((GameScreen == other.GameScreen) &&
          (MaxFrameSkips == other.MaxFrameSkips) &&
          (SecondScreenContent == other.SecondScreenContent) &&
          (SecondScreenOpacity == other.SecondScreenOpacity) &&
          (GameBorder == other.GameBorder) &&
          (GameBorderOpacity == other.GameBorderOpacity) &&
          (Font == other.Font) &&
          (ScreenStretch == other.ScreenStretch) &&
          (ForceFrameRate == other.ForceFrameRate) &&
          (StretchWidth == other.StretchWidth) &&
          (StretchHeight == other.StretchHeight) &&
          (CropPixels == other.CropPixels) &&
          (Turbo == other.Turbo) &&
          (Volume == other.Volume) &&
          (TicksPerFrame == other.TicksPerFrame) &&
          (PaletteFix == other.PaletteFix) &&
          (AutoSavestate == other.AutoSavestate) &&
          (CurrentSaveSlot == other.CurrentSaveSlot) &&
          (SRAMSaveInterval == other.SRAMSaveInterval) &&
          (ForceSRAMWriteOnPause == other.ForceSRAMWriteOnPause) &&
          (BindCirclePad == other.BindCirclePad) &&
          (GlobalButtonMapping == other.GlobalButtonMapping) &&
          (ButtonMapping == other.ButtonMapping) &&
          (ButtonHotkeys == other.ButtonHotkeys) &&
          (GlobalButtonHotkeys == other.GlobalButtonHotkeys) &&
          (UseGlobalButtonMappings == other.UseGlobalButtonMappings) &&
          (UseGlobalTurbo == other.UseGlobalTurbo) &&
          (UseGlobalVolume == other.UseGlobalVolume) &&
          (UseGlobalEmuControlKeys == other.UseGlobalEmuControlKeys) &&
          (GlobalTurbo == other.GlobalTurbo) &&
          (GlobalVolume == other.GlobalVolume) &&
          (GlobalBindCirclePad == other.GlobalBindCirclePad) &&
          (RomFsLoaded == other.RomFsLoaded) &&
          (Disable3DSlider == other.Disable3DSlider) &&
          (GameThumbnailType == other.GameThumbnailType));
}

bool S9xSettings3DS::operator!=(const S9xSettings3DS& other) const {
  return !(*this == other);
}
