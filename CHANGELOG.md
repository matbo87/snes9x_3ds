# Changelog
Notable changes to this project will be documented in this file.

## v1.60

### Major Changes
* **Rendering backend migration**: move from legacy GPU code to citro3d
* **Draw-call batching overhaul**: fewer draw calls via batched rendering and XOR-based packed render-state diffing
* **GPU decoupling**: separate `gfxhw` state preparation from the GPU submission path for a cleaner rendering pipeline

### Performance
* **Rendering throughput**:
  * layer/section collection and merged backdrop/color-math passes to reduce redundant draws
  * uniform upload and render-state update optimizations (including patched citro3d max-dirty behavior)
* **I/O and memory**:
  * faster save/config writes and improved file I/O architecture
  * reduced heap fragmentation pressure
  * menu/file navigation streamlined, with snappier behavior on old 2DS/3DS models
  * improved ROM list caching
* **Asset handling**:
  * background assets on 16-bit texture formats (RGB565) to reduce memory bandwidth/footprint
  * replace `stb_image` with a `libpng`-based path that uses a shared file scratch buffer (`g_fileBuffer`) and an aligned shared stream buffer (`g_streamBuffer`) to reduce heap churn/fragmentation

### Features
* **Thumbnail system**:
  * replace fragile thumbnail background-thread loading with on-demand reads from one cache file per thumbnail type
  * removes shared-state race issues and keeps thumbnail loading fast and stable for large ROM folders
* **SNES-accurate refresh-rate matching**:
  * when gameplay starts/resumes, 3DS LCD timing is set to the game's native SNES rate (NTSC ~60.1 Hz / PAL 50 Hz)
* **On-Screen Display**:
  * bezel overlay with auto-fit support
  * FPS overlay option
  * GPU-accelerated notifications
* **Stereoscopic 3D additions**:
  * basic 3D support for splash screen, in-game scene background, and pause overlay

### Stability & Code Quality
* **Code-quality cleanup**: broad typing, const/sign correctness, return-path, warning cleanup across both 3DS frontend and SNES core
* **Build warning policy upgrade**: remove old global warning suppression (`-w`) and move to enabled warnings enforcing `-Werror` by default

### Breaking Changes
* **Config migration**: `settings.cfg` may not migrate cleanly in all cases; defaults can be applied
* **Thumbnail assets**: legacy per-image thumbnail folders are obsolete; thumbnails now load from `*.cache` files (`boxart.cache`, `gameplay.cache`, `title.cache`)
* **Background asset paths**:
  * `snes9x_3ds/borders` -> `snes9x_3ds/backgrounds/game_screen`
  * `snes9x_3ds/covers` -> `snes9x_3ds/backgrounds/second_screen`


## v1.52

### Bug Fixes
* **Thread safety**: prevent cache thread from accessing `romFileNames` while it is being modified by the main thread ([#32](https://github.com/matbo87/snes9x_3ds/issues/32)) ([ea806c5](https://github.com/matbo87/snes9x_3ds/commit/ea806c5d89d186f1d9018a8d1850190d422ad4ca))
* **Shutdown stability**: ensure all global/static stores are cleared properly at exit to avoid late destruction‑order crashes ([#32](https://github.com/matbo87/snes9x_3ds/issues/32)) ([ea806c5](https://github.com/matbo87/snes9x_3ds/commit/ea806c5d89d186f1d9018a8d1850190d422ad4ca))
* **ROM mapping**: Fix incorrect memory bank mapping for Mega Man X (and probably other ROMs) ([#26](https://github.com/matbo87/snes9x_3ds/issues/26)) ([05c6663](https://github.com/matbo87/snes9x_3ds/commit/05c6663ae2d944c4c232838ac4f5bf0d8c6c98aa))

### Features
* **Screen stretch**: add "8:7 Fit" scaling option ([#28](https://github.com/matbo87/snes9x_3ds/pull/28)) ([526d62f](https://github.com/matbo87/snes9x_3ds/commit/526d62f9250421ed867c43a675c226c63b718f19))
* **Screen filter**: add "linear filtering" option ([#28](https://github.com/matbo87/snes9x_3ds/pull/28)) ([9744318](https://github.com/matbo87/snes9x_3ds/commit/9744318cc747adeaf0c7decff94c8126584fa8b4))

### Code Refactoring
* **Performance**: revert commit 8d50f5 due to negative performance impact ([d50de94](https://github.com/matbo87/snes9x_3ds/commit/d50de943ff0415eb70f8cec3dfc7e50fe1490886))
* **Shader**: remove unused shaders + adjust makefile ([3aa0377](https://github.com/matbo87/snes9x_3ds/commit/3aa03772cecf17264e4cfee00545360286c15a42))


## v1.51.1

### Bug Fixes
* **Old 3DS, Old 2DS**: fix crash on O3DS/O2DS when user opens menu after game has loaded ([#11](https://github.com/matbo87/snes9x_3ds/issues/11)) ([71ed471](https://github.com/matbo87/snes9x_3ds/commit/71ed471f3f9bfea74f42105ddbbbf3b9e9a94c07))


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
