# Snes9x for 3DS

## Overview

This project is a fork of the legacy snes9x_3ds codebase by [bubble2k](https://github.com/bubble2k16/snes9x_3ds) and continues that work with a modernized architecture and improved user experience.
It builds with current devkitARM, libctru and citro3d releases (as of March 2026). Optional assets are available in the dedicated asset repository: [snes9x_3ds-assets](https://github.com/matbo87/snes9x_3ds-assets).

It works on all 2DS and 3DS models. Old 2DS/3DS can struggle with demanding games (e.g. Super FX titles like Star Fox), but many SNES games still run well.

Feedback, bug reports and contributions are welcome.

## Main features

* Thumbnail support (box art, title and gameplay)
* Per-game backgrounds for top and bottom screens, overlays with auto-fit option
* SNES refresh rate matching (60.1 Hz for NTSC, 50 Hz for PAL)
* Theme support
* Improved cheat management
* Clean, RetroArch-style folder structure
* Directory caching for faster ROM list loading
* Extended hotkey options and screen swap support

## Setup

* A modded 3DS is required.
* Install via [Universal Updater](https://universal-team.net/projects/universal-updater.html), or install the latest `.cia` from [Releases](https://github.com/matbo87/snes9x_3ds/releases)
* Optional: download asset packs from [snes9x_3ds-assets releases](https://github.com/matbo87/snes9x_3ds-assets/releases).

ROMs can be stored in any folder. ZIP files are not supported.

Supported ROM formats:
* `.smc`
* `.sfc`
* `.fig`

Configs, saves and imported assets are stored in `sd:/3ds/snes9x_3ds`.

### 3DSX version

* Copy `snes9x_3ds.3dsx` to `sd:/3ds/snes9x_3ds`
* Start it from the Homebrew Launcher

## Assets (images and cheats)

Assets are provided in dedicated asset repository:
* [matbo87/snes9x_3ds-assets](https://github.com/matbo87/snes9x_3ds-assets)

Notes:

* The repository follows a 1G1R-style selection.
* Naming is strict No-Intro style for matching.

## Building from source

* Install devkitPro and 3DS toolchain packages (including devkitARM, libctru, citro3d). If needed, follow the [devkitPro pacman guide](https://devkitpro.org/wiki/devkitPro_pacman).
* The Makefile is based on TricksterGuy's [3ds-template](https://github.com/TricksterGuy/3ds-template).

Required command-line tools in `PATH`:

* For `3dsx` builds: `tex3ds`, `smdhtool`, `3dsxtool` (from the devkitPro 3DS toolchain).
* For `cia` builds: `makerom` in addition to the above.

Common build targets:

* `make 3dsx`
* `make citra`
* `make 3dslink` (sends the `.3dsx` to your Homebrew Launcher)

This repository bundles `makerom` binaries under `makerom/` for convenience.
Bundled binary provenance is documented in `makerom/BINARY_SOURCES.md`.

### Emulator status

* Citra macOS (Nightly 1989): incomplete (no audio, Mode 7 textures only partially visible)
* Other 3DS emulators: not tested by me

## Screenshots

<table>
  <tr>
    <td width="50%" align="center"><img src="screenshots/dark-mode-file-menu.png" alt="Aladdin" valign="bottom"></td>
    <td width="50%" align="center"><img src="screenshots/retroarch-pause-screen.png" alt="Donkey Kong Country" valign="bottom"></td>
  </tr>
  <tr>
    <td valign="top" width="50%">Start screen, File menu tab with "Game Thumbnail" option enabled</td>
    <td valign="top" width="50%">Pause screen, RetroArch theme, per-game overlay enabled</td>
  </tr>
  <tr><td colspan="2"></td></tr>
  <tr></tr>
  <tr>
    <td width="50%" align="center"><img src="screenshots/aladdin-pp-cheats.png" alt="Aladdin" valign="bottom"></td>
    <td width="50%" align="center"><img src="screenshots/dkc-tvstyle-hotkeys.png" alt="Donkey Kong Country" valign="bottom"></td>
  </tr>
  <tr>
    <td valign="top" width="50%">Original theme, pixel-perfect video, cheats enabled</td>
    <td valign="top" width="50%">TV-style video, applied hotkeys + "Analog to Digital Type" disabled</td>
  </tr>
  <tr><td colspan="2"></td></tr>
  <tr></tr>
  <tr>
    <td width="50%" align="center"><img src="screenshots/sf2-cropped-border-cover.png" alt="Super Street Fighter II" valign="bottom"></td>
    <td width="50%" align="center"><img src="screenshots/issd-screen-swap-konami-cheat.png" alt="International Superstar Soccer Deluxe" valign="bottom"></td>
  </tr>
  <tr>
    <td valign="top" width="50%">Cropped 4:3 video, game-specific cover</td>
    <td valign="top" width="50%">4:3 video, swapped screen and Konami cheat via 2-Player-Switch</td>
 </tr>
 </table>

## Frequently Asked Questions

### A game runs slow. How can I improve performance?

* Increase `Frameskips` (try 1-2 first)
* Set `SRAM Auto-Save Delay` to 60 seconds or disable it
* Set `In-Frame Palette Changes` to `Disabled Style 1` or `Disabled Style 2`
* Disable 3D and/or on-screen display settings
* If available, try the PAL version of the game (50 FPS instead of 60 FPS)
* Super FX games often run poorly on old 2DS/3DS models
* Check the [Compatibility List](http://wiki.gbatemp.net/wiki/Snes9x_for_3DS) with care, it may be outdated

### A game looks broken or has visual glitches. What can I try?

* Set `In-Frame Palette Changes` to `Enabled`
* Enabled cheats can break visuals or gameplay; disable cheats and reload the game
* Check if your ROM is valid (No-Intro is highly recommended; ROM hacks may have issues)
* Check the [Compatibility List](http://wiki.gbatemp.net/wiki/Snes9x_for_3DS) with care, it may be outdated

### Cheats are not working properly

* The cheat set is only roughly tested
* Some cheats may not work correctly and may damage save data
* Always use cheats with caution


## Snes core features

* Graphic modes 0 * 7.
* Frame skipping.
* Stretch to full screen / 4:3 ratio
* SDD1 chip (Street Fighter 2 Alpha, Star Ocean)
* SFX1/2 chip (Yoshi's Island, but slow on old 3DS)
* CX4 chip (Megaman X-2, Megaman X-3)
* DSP chips (Super Mario Kart)
* SA-1 chip (Super Mario RPG, Kirby Superstar)
* Sound emulation (32KHz, with echo and gaussian interpolation)

## What's missing / needs to be improved

* Citra support is incomplete (no audio, partial Mode 7 rendering)
* Audio backend depends on deprecated CSND service
* Minor sound emulation errors
* Poor performance in some Super FX games (for example Doom)
* Mosaic effects
* In-frame palette changes (a small number of games show color issues)

## License

Some files may carry their own license headers, but because this project includes the Snes9x core (`source/Snes9x/`), redistribution of the combined project follows the Snes9x non-commercial license terms.

See:
* [LICENSE.md](LICENSE.md)
* [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)

## Credits

* bubble2k for [snes9x_3ds](https://github.com/bubble2k16/snes9x_3ds)
* ramzinouri for [snes9x_3ds fork](https://github.com/ramzinouri/snes9x_3ds)
* willjow for [snes9x_3ds fork](https://github.com/willjow/snes9x_3ds)
* Wyatt-James for warning/safety/audio fixes adapted from [Wyatt-James/snes9x_3ds](https://github.com/Wyatt-James/snes9x_3ds):
  * [69fecab](https://github.com/matbo87/snes9x_3ds/commit/69fecab) adapted from [7d24837](https://github.com/Wyatt-James/snes9x_3ds/commit/7d24837)
  * [7e58c74](https://github.com/matbo87/snes9x_3ds/commit/7e58c74) adapted from [95fb508](https://github.com/Wyatt-James/snes9x_3ds/commit/95fb508) and [37200a1](https://github.com/Wyatt-James/snes9x_3ds/commit/37200a1)
  * [5d43961](https://github.com/matbo87/snes9x_3ds/commit/5d43961) adapted from [be69369](https://github.com/Wyatt-James/snes9x_3ds/commit/be69369)
  * [4c01c47](https://github.com/matbo87/snes9x_3ds/commit/4c01c47) adapted from [ed8bcf3](https://github.com/Wyatt-James/snes9x_3ds/commit/ed8bcf3)
  * [24939f2](https://github.com/matbo87/snes9x_3ds/commit/24939f2) adapted from [c78e091](https://github.com/Wyatt-James/snes9x_3ds/commit/c78e091)
  * [aaa2e83](https://github.com/matbo87/snes9x_3ds/commit/aaa2e83) adapted from [344b2d7](https://github.com/Wyatt-James/snes9x_3ds/commit/344b2d7)
  * [6bd66de](https://github.com/matbo87/snes9x_3ds/commit/6bd66de) adapted from [f4244af](https://github.com/Wyatt-James/snes9x_3ds/commit/f4244af)
