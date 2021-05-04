#include "3dssettings.h"

bool S9xSettings3DS::operator==(const S9xSettings3DS& other) const {
  return ((this->GameScreen == other.GameScreen) &&
          (this->MaxFrameSkips == other.MaxFrameSkips) &&
          (this->SecondScreenContent == other.SecondScreenContent) &&
          (this->SecondScreenOpacity == other.SecondScreenOpacity) &&
          (this->ShowGameBorder == other.ShowGameBorder) &&
          (this->GameBorderOpacity == other.GameBorderOpacity) &&
          (this->Font == other.Font) &&
          (this->ScreenStretch == other.ScreenStretch) &&
          (this->ForceFrameRate == other.ForceFrameRate) &&
          (this->StretchWidth == other.StretchWidth) &&
          (this->StretchHeight == other.StretchHeight) &&
          (this->CropPixels == other.CropPixels) &&
          (this->Turbo == other.Turbo) &&
          (this->Volume == other.Volume) &&
          (this->TicksPerFrame == other.TicksPerFrame) &&
          (this->PaletteFix == other.PaletteFix) &&
          (this->AutoSavestate == other.AutoSavestate) &&
          (this->CurrentSaveSlot == other.CurrentSaveSlot) &&
          (this->SRAMSaveInterval == other.SRAMSaveInterval) &&
          (this->ForceSRAMWriteOnPause == other.ForceSRAMWriteOnPause) &&
          (this->BindCirclePad == other.BindCirclePad) &&
          (this->GlobalButtonMapping == other.GlobalButtonMapping) &&
          (this->ButtonMapping == other.ButtonMapping) &&
          (this->ButtonHotkeys == other.ButtonHotkeys) &&
          (this->GlobalButtonHotkeys == other.GlobalButtonHotkeys) &&
          (this->UseGlobalButtonMappings == other.UseGlobalButtonMappings) &&
          (this->UseGlobalTurbo == other.UseGlobalTurbo) &&
          (this->UseGlobalVolume == other.UseGlobalVolume) &&
          (this->UseGlobalEmuControlKeys == other.UseGlobalEmuControlKeys) &&
          (this->GlobalTurbo == other.GlobalTurbo) &&
          (this->GlobalVolume == other.GlobalVolume) &&
          (this->GlobalBindCirclePad == other.GlobalBindCirclePad) &&
          (this->RomFsLoaded == other.RomFsLoaded) &&
          (this->Disable3DSlider == other.Disable3DSlider));
}

bool S9xSettings3DS::operator!=(const S9xSettings3DS& other) const {
  return !(*this == other);
}
