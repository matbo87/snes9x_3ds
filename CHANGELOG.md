# Change Log
Notable changes to this project will be documented in this file.

## v1.51

### Features
* **Theme option:** add Dark mode and RetroArch theme ([#4](https://github.com/matbo87/snes9x_3ds/issues/2)) ([d343ca6](https://github.com/matbo87/snes9x_3ds/commit/d343ca60fb0e380fa9b4239c7ebf346e0ff86e6c))
* **File menu:** adjust navigation pattern + provide more options in file menu tab ([#4](https://github.com/matbo87/snes9x_3ds/issues/2)) ([d343ca6](https://github.com/matbo87/snes9x_3ds/commit/d343ca60fb0e380fa9b4239c7ebf346e0ff86e6c), [ea2cd3f](https://github.com/matbo87/snes9x_3ds/commit/ea2cd3fa970f81a4384ebf0c7b014b429d4d7d34))
  * Going up a directory by pressing B
  * Option to set a default starting folder
  * Delete game option
  * Random game option
* **Pause screen:** show a decent pause screen when menu is open during gameplay ([4c9f3ec](https://github.com/matbo87/snes9x_3ds/commit/4c9f3ecb333eaf23da85e9199bdbbfa3511312dd))

### Bug Fixes
* **O2DS**: fix crash on O2DS (and probably O3DS as well) when saving SRAM ([#2](https://github.com/matbo87/snes9x_3ds/issues/2)) ([02788b1](https://github.com/matbo87/snes9x_3ds/commit/02788b17d038e30e612dcbf0719ec45a8fc54a43))

### Code Refactoring
* **Menu**: reduce redundant code + preserve selected item index per tab ([493c1a2](https://github.com/matbo87/snes9x_3ds/commit/493c1a22b3975c7cb39a55dbd38140e5e3cd2a14), [4d6378a](https://github.com/matbo87/snes9x_3ds/commit/4d6378a507cb77571e4444abb6fbd0df3ff5f555))
* **Dialogs**: remove unnecessary animations for a snappier appearance ([2bb82c6](https://github.com/matbo87/snes9x_3ds/commit/2bb82c69512a2ef894ee5bb049be13ba567b6e89))
* **Second screen content**: clean up + prevent flickering when info dialog appears/disappears ([c1899df](https://github.com/matbo87/snes9x_3ds/commit/c1899df01828b9653c3c635695e61d1ce4fbeaee))


## v1.50

### Features
* **Game preview option:** boxart, title or gameplay
* **Improved cheat menu:** now with available/activated indicator
* **Updated Banner:** based on SNES VC banner
* **Menu clean up:**
  * Reset config(s) option
  * Autosave is now game-specific
  * Show saving dialog instead of _freezed_ menu

### Bug Fixes
* **Cheats**: Fix cheats not loaded/saved properly
* **Default button mappings**: Fix missing default controls 
* **Home menu button:** make emulator quit properly when user exits via home menu button
* **Pixel perfect mode:** Fix blurry image (mentioned [here](https://github.com/asdolo/snes9x_3ds_forwarder/pull/1))
* **Long game lists**: Fix app crash on exit

### Code Refactoring
* **Makefile & app.rsf:** use TricksterGuy's [3ds-template](https://github.com/TricksterGuy/3ds-template), update compiler options
* **Image loading/rendering:** use stb_image instead of lodepng for faster image decoding, unify image rendering logic
* **Second screen content**: improve performance

### Breaking changes
* **Folder structure:** All game related files are now in "3ds/snes9x_3ds", similar to RetroArch folder structure


---
## Older releases

### v1.45
- Buffered file writer for faster config saves (thanks to [willjow](https://github.com/willjow/snes9x_3ds))

### v1.42
- Fixed screen tearing
- Added option to disable 3D Slider (thanks to ramzinouri)

### v1.41
- Fixed hotkey for making screenshot 
- Fixed quick save/load (no data abort exceptions anymore)
- Fixed Errors if cover image is missing
- Updated assets (icon, banner, border, cover)

### v1.40

- Added Swap Game Screen option
- Added switch controller option like in official Virtual Console (SF2 "Training Mode", Konami cheat, ...)
- Custom second screen image and border for every game (thanks to ramzinouri and Asdolo)
- Game Info option for second screen
- Provide more Hotkeys (Quick Save/Load, Swap Controllers)
- Disable Analog to Digital Type option which allows you to use circle pad for hotkeys as well
- All game related files like cheats or save states are now in a single folder (folder name = rom name)
- Screenshots are now in PNG format (thanks to ramzinouri)
- Removed BlargSNES DSP Core, updated dsp-1, added dsp-2 -3 and -4 (thanks to ramzinouri)

### [bubble2k Change Log](https://github.com/bubble2k16/snes9x_3ds#change-history)
