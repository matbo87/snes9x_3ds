# Changelog
Notable changes to this project will be documented in this file.

## Unreleased — Stereoscopic 3D (f4mrfaux fork)

Full stereoscopic 3D rendering for SNES gameplay on the 3DS's dual-eye top
screen, with per-game depth tuning. Built on top of matbo87 v1.60.1.

### Features
* **Per-layer stereoscopic depth**: each SNES BG layer, sprite layer, Mode 7
  plane and backdrop sits at its own stereoscopic depth derived from the
  emulator's internal compositing order. Foreground tiles pop toward the
  viewer; distant backgrounds recede into the screen.
* **Per-tile depth within BG layers**: tiles within a single BG layer fan out
  across a depth range based on their compositing priority — not just a flat
  plane per layer.
* **Mode 7 perspective stereo**: the road / ground plane in Mode 7 scenes
  (F-Zero, Pilotwings, Mario Kart) recedes with Y-scaled perspective depth.
* **OBJ per-priority depth**: sprites at different SNES OBJ priorities sit at
  different depths automatically.
* **Per-game depth sliders**: each layer has a signed Depth gauge
  (−40..0..+40). Center is Auto (SNES-detected). Slide LEFT to push into the
  screen, RIGHT to pop toward the viewer. Saved per game; independent per
  BG0 / BG1 / BG2 / BG3 / Sprites (OBJ) / Mode 7 / Backdrop.
* **Pass-through geometry shader on shader_screen** — staged as a defensive
  workaround for citro3d issue #56 (mixed GS/non-GS program hang); currently
  compiled but not attached at runtime pending hardware validation.
* **Documentation of shader invariants** — `shader_tiles.g.pica` now carries
  a uniform-block comment spelling out the stereoOffset name/component/register
  invariants to protect against silent-failure refactors.

### Bug fixes (from rebase integration)
* **c_depth constant in tile vertex shader** corrected from 1/32 to 1/16 — the
  wrong value halved per-tile depth variation and shifted the zero-parallax
  plane to an unreachable depth, inverting the apparent pop/recede direction
  of foreground BG layers.
* **BG layer base offset missing depthFactor** in the normal `dRange ≤ 6`
  path — all Mode 1 BG layers landed at identical stereo offsets, producing
  visibly flat inter-layer separation even when the depth table said otherwise.
* **Right-eye compositing when menu stereo is off** — when the user disabled
  stereo in the menu but left the hardware 3D slider up, the right screen
  composited an empty texture (dark right eye). Composite now matches draw
  conditions.
* **Stereo config reads dropped on older config files** — the initial rebase
  version-gated reads for pre-existing stereo settings fields, silently
  discarding user-saved Stereo3DEnabled and per-layer scales. Gates removed.
* **Integration of matbo87 v1.60.1 VTotal fix** (issue #54) — toggling Disable
  3D mid-game no longer crashes / speeds up the game.

### Breaking changes
* `StereoBG*Scale` / `StereoOBJScale` / `StereoMode7Scale` / `StereoBackdropScale`
  config fields change semantics from 0..40 (magnitude only, direction
  auto-derived from SNES) to -40..+40 (signed: sign = direction, magnitude =
  strength, 0 = Auto). Existing saved values from v2.1 reload as positive
  (force pop) which doesn't match Auto for every layer. Fix: in-game menu
  → `Reset all to Auto (center)` to start from Auto defaults.
* Game config version unchanged (stays at matbo's 1.3) — the signed-gauge
  semantics change is a reinterpretation of existing keys, not new keys,
  so no version bump is needed to suppress parse warnings.

### Upstream integration
* Rebased onto matbo87 `ce600fc` (v1.60.1). Picks up the Disable 3D freeze fix
  (#54), WindowLR overlap tagging fix, Fast-Forward Hold hotkey, per-game
  Framerate override (Auto / Force 60 FPS), Screen Smoothing toggle.


## v1.60.1

### Bug Fixes
* Fixed VRAM read control flow regression ([#46](https://github.com/matbo87/snes9x_3ds/issues/46)) ([c32c5ab](https://github.com/matbo87/snes9x_3ds/commit/c32c5ab))
* Fixed WindowLR overlap tagging when trimming black scanlines ([#46](https://github.com/matbo87/snes9x_3ds/issues/46)) ([d536983](https://github.com/matbo87/snes9x_3ds/commit/d536983))

### Reintroduced Features
* Reintroduced optional screen smoothing for stretched modes ([#51](https://github.com/matbo87/snes9x_3ds/issues/51)) ([432d202](https://github.com/matbo87/snes9x_3ds/commit/432d202))
* Reintroduced per-game framerate override (Auto or Force 60 FPS) ([#50](https://github.com/matbo87/snes9x_3ds/issues/50)) ([b4f45e8](https://github.com/matbo87/snes9x_3ds/commit/b4f45e8))

### Maintenance
* Document bundled makerom sources for provenance ([#47](https://github.com/matbo87/snes9x_3ds/issues/47)) ([4cae630](https://github.com/matbo87/snes9x_3ds/commit/4cae630))
* CI/tooling updates for GitHub Actions compatibility ([475042a](https://github.com/matbo87/snes9x_3ds/commit/475042a), [7e1a91a](https://github.com/matbo87/snes9x_3ds/commit/7e1a91a))


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
