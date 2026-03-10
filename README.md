# Snes9x for 3DS — Stereoscopic 3D Fork

This is a fork of [matbo87/snes9x_3ds](https://github.com/matbo87/snes9x_3ds) that adds **real stereoscopic 3D** to SNES games on the Nintendo 3DS. Background layers are separated at different depths using the 3DS parallax barrier display, similar to how SEGA's M2 studio handled their [3D Classics](https://en.wikipedia.org/wiki/3D_Classics) series.

Slide the 3D slider up and SNES backgrounds gain real depth — sprites stay at the screen plane.

## How to use

1. Download `matbo87-snes9x_3ds.3dsx` from the [latest release](https://github.com/f4mrfaux/snes9x_3ds/releases)
2. Copy to `sd:/3ds/snes9x_3ds/snes9x.3dsx` on your 3DS SD card
3. Launch from Homebrew Launcher
4. Load a game and slide the 3D slider up

With the slider at 0, the emulator behaves identically to upstream — zero overhead.

## What's different from upstream?

[matbo87/snes9x_3ds](https://github.com/matbo87/snes9x_3ds) is an excellent SNES emulator with themes, thumbnails, bezels, cheats, and a citro3D GPU rendering pipeline. This fork preserves all of that and adds a stereoscopic 3D rendering layer on top.

| | matbo87 (upstream) | This fork |
|---|---|---|
| SNES emulation | Full | Full (identical) |
| Themes, thumbnails, bezels, cheats | Yes | Yes |
| Stereoscopic 3D | No | Yes |
| 3D slider control | No | Yes — controls depth intensity |
| Slider at 0 | N/A | Identical to upstream (mono fast path) |

## How it works: Shader-Uniform Replay

The SNES renders graphics in layers — up to four background planes (BG0-BG3), a sprite layer (OBJ), and a backdrop color. Normally these are composited flat onto a single 2D framebuffer. This fork renders each main-screen layer **twice** (once per eye) with a small horizontal offset to create parallax.

The critical design choice is that this is done **entirely on the GPU**:

1. **CPU builds tile vertices once** — no extra CPU work compared to mono rendering
2. **GPU draws main-screen layers twice** with different `stereoOffset` geometry shader uniforms — one pass for the left eye, one for the right
3. Each eye's result goes to a separate 256x256 texture (`SNES_MAIN` for left, `SNES_MAIN_R` for right)
4. The textures are composited to the 3DS top screen's left and right framebuffers via the parallax barrier

Window/clip regions and sub-screen transparency layers are drawn only once (they don't need stereo separation).

### Depth assignment

Layer depths are derived from the emulator's own compositing priority values — the `depth0`/`depth1` parameters already used by the `DRAW_*` macros in `gfxhw.cpp`. These encode which layers are in front of or behind others, so we reuse them directly as parallax depth factors. No per-game profiles are needed.

For **Mode 1** (the most common SNES mode — Super Mario World, Zelda, Mega Man X, etc.):

| Layer | Stereo Effect |
|-------|---------------|
| BG0 (foreground tiles) | Pops toward the viewer |
| BG1 (mid-ground scenery) | Pops slightly toward the viewer |
| BG2 (far background) | Recedes into the screen |
| Sprites (OBJ) | Screen plane — zero parallax (averaged across 4 priority levels) |
| Backdrop | Deepest — behind everything |

Depth values change automatically per SNES graphics mode (Modes 0-6). Mode 7 games (F-Zero, Super Mario Kart, Pilotwings) stay mono automatically — the geometry shader branches before the stereo offset code, so Mode 7 tiles are never shifted.

### Key technical details

- **Geometry shader uniform**: `stereoOffset` at register #5 in `shader_tiles.g.pica`, applied to projected X coordinates after the projection matrix multiply
- **Clip-space scaling**: The offset is converted from pixel units to clip space (`* 2.0f / 256.0f`) since the orthographic projection maps 0..256 to -1..+1
- **Right-eye texture redirect**: `SNES_MAIN_R` is a texture ID only, not a member of the `SGPU_TARGET_ID` enum (adding it would overflow the 3-bit `BW_TARGET` bitfield in the packed render state union). The redirect happens in `gpu3dsApplyRenderState()` when `GPU3DS.stereoRightEye` is set
- **Depth buffer isolation**: The shared `SNES_DEPTH` buffer is cleared and window_lr clip regions are re-rendered between eye passes, preventing left-eye depth values from contaminating right-eye depth testing
- **IOD range**: 0-3 pixels maximum displacement per layer, controlled linearly by the 3D slider position
- **PICA200 ALU limitation**: The geometry shader cannot reference uniforms as direct operands in `add` — the uniform value is `mov`'d to a temp register first

### Files changed (vs. upstream)

| File | Change |
|------|--------|
| `source/shader_tiles.g.pica` | `stereoOffset` uniform + per-vertex X offset |
| `source/3dsgpu.h` | Stereo state fields, right-eye redirect in render state application |
| `source/3dsgpu.cpp` | Uniform registration, `gpu3dsSetStereoOffset()` setter |
| `source/3dsimpl_gpu.cpp` | `getStereoDepthFactor()`, per-eye draw loop in `gpu3dsDrawLayers()` |
| `source/3dsimpl.cpp` | `SNES_MAIN_R` texture allocation, depth buffer sharing |
| `source/3dsmain.cpp` | `gfxSet3D(true)` to enable parallax barrier |
| `source/3dsui_img.cpp` | Non-fatal VRAM allocation for external UI textures |

## FAQ

### Does this affect performance?

Minimally. The CPU builds vertices once regardless of stereo mode. The GPU draws main-screen layers a second time, but the PICA200 handles 256x240 tile rendering comfortably. Target is 50-60 FPS with stereo enabled.

### Does this work with all SNES games?

It works with all games that use standard tile-based BG modes (Modes 0-6), which covers the vast majority of the SNES library. Mode 7 games render in mono automatically — no crash, no visual issues.

### Can I turn it off?

Yes. Slide the 3D slider to 0. The emulator runs an identical mono code path with zero overhead — the eye loop executes once and no stereo uniforms are written.

### What about the CIA version?

Currently only the `.3dsx` (Homebrew Launcher) version is provided.

## Building from source

Requires devkitARM (`$DEVKITARM` must be set) and `3ds-libpng`:

```bash
sudo pacman -S 3ds-libpng  # if not already installed
make
```

Output: `output/matbo87-snes9x_3ds.3dsx`

See the [devkitPro installation guide](https://devkitpro.org/wiki/devkitPro_pacman) for toolchain setup.

## Upstream features (inherited from matbo87)

* Game thumbnails (boxart, title, gameplay)
* Border (bezel) and second screen image (cover) for each game
* Themes (dark mode, RetroArch-style)
* Improved cheat menu with [no-intro sets](https://github.com/matbo87/snes9x_3ds-assets)
* RetroArch-ish folder structure
* Swap screen and hotkey options
* Graphic modes 0-7, SDD1, SFX1/2, CX4, DSP, SA-1 chip support
* Sound emulation at 32KHz with echo and gaussian interpolation

## Screenshots

<table>
  <tr>
    <td width="50%" align="center"><img src="screenshots/dark-mode-file-menu.png" alt="File menu" valign="bottom"></td>
    <td width="50%" align="center"><img src="screenshots/retroarch-pause-screen.png" alt="Pause screen" valign="bottom"></td>
  </tr>
  <tr>
    <td valign="top" width="50%">Start screen with game thumbnails</td>
    <td valign="top" width="50%">Pause screen with RetroArch theme</td>
  </tr>
</table>

## Credits

* [matbo87](https://github.com/matbo87/snes9x_3ds) — upstream snes9x_3ds with citro3D overhaul, themes, thumbnails, and UI
* [bubble2k](https://github.com/bubble2k16/snes9x_3ds) — original snes9x_3ds emulator
* [ramzinouri](https://github.com/ramzinouri/snes9x_3ds) — earlier snes9x_3ds fork
* [Asdolo](https://github.com/Asdolo/snes9x_3ds_forwarder) — CIA forwarder
* Stereoscopic 3D implementation by [f4mrfaux](https://github.com/f4mrfaux) with [Claude Code](https://claude.com/claude-code)
