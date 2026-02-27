# FORK NOTES: SNES9x-3DS Stereoscopic 3D

**Repo:** https://github.com/matbo87/snes9x_3ds
**Analyzed:** February 2026
**Implementation:** February 2026 — build clean, pending hardware validation
**Purpose:** Answers to the 8 key research questions + implementation notes

---

## Answers to Key Questions

### Q1: Does matbo87's fork already create separate left/right eye render targets?

**No.** Stereoscopic is intentionally disabled during gameplay.

Evidence in `source/3dsgpu.cpp`:
```cpp
// Line 443 — init
gfxSet3D(false);  // disabled

// Line 2239-2245 in 3dsmain.cpp — only enabled in menus:
if (!settings3DS.Disable3DSlider) {
    gfxSet3D(true);
    gpu3dsCheckSlider();
} else {
    gfxSet3D(false);
}
```

The `gpu3dsTransferToScreenBuffer()` function always passes `GFX_LEFT` and
never touches `GFX_RIGHT`. There is zero right-eye rendering in the codebase.

The `gpu3dsCheckSlider()` function exists and reads the slider (via direct
memory address `0x1FF81080` instead of `osGet3DSliderState()`), but its only
action is to enable/disable the parallax barrier above a 0.6 threshold — it
never adjusts rendering.

---

### Q2: At what point in the render pipeline does layer compositing happen — CPU/C++ or GPU?

**GPU-accelerated throughout.** This fork is hardware-rendered.

There are two distinct stages:

**Stage 1 — Tile geometry generation (CPU)**
During `S9xMainLoop()` → `FLUSH_REDRAW()` → `S9xUpdateScreenHardware()` →
`S9xRenderScreenHardware()`, the SNES PPU state is read and each visible tile
is converted to a 2-vertex entry in a CPU-side vertex buffer
(`GPU3DSExt.tileVertexes.List`). This is fast integer math.

**Stage 2 — GPU draw (PICA200)**
At the end of each `S9xRenderScreenHardware()` call, `gpu3dsDrawVertexes()` is
called. This adds a `GPU_DrawArray()` command to the GPU command buffer. The
geometry shader (`shaderfast2_shbin`) expands each 2-vertex point into a
6-vertex textured quad. The tile cache texture (`snesTileCacheTexture`) is
sampled for pixel data. Output goes to `snesMainScreenTarget` (256×256 VRAM).

**Stage 3 — Composite (GPU)**
After `S9xMainLoop()`, a full-screen quad is drawn in `impl3dsRunOneFrame()`
that scales `snesMainScreenTarget` (256×240 of the 256×256) to `GPU3DS.frameBuffer`
(400×240). This handles the 256→400 width stretch to fill the 3DS top screen.

**Stage 4 — Display transfer (GX DMA)**
`GX_DisplayTransfer()` converts the tiled VRAM framebuffer to the linear LCD
buffer format. This is DMA, not CPU or GPU shader work.

---

### Q3: Are SNES BG layers rendered sequentially and accessible as separate outputs?

**Yes, they are rendered sequentially.** But there is a critical constraint:
**each layer draws immediately inside its own DRAW_* macro** — there is no
single batch draw at the end of `S9xRenderScreenHardware`.

In `S9xRenderScreenHardware()` for Mode 1 (most common):

```cpp
S9xDrawBackdropHardware(sub, BackAlpha);       // solid color fill
DRAW_OBJS(0);                                  // sprite layer  ← calls gpu3dsDrawVertexes() internally
DRAW_16COLOR_BG_INLINE(0, 0, 8, 11);           // BG0           ← calls gpu3dsDrawVertexes() internally
DRAW_16COLOR_BG_INLINE(1, 0, 7, 10);           // BG1           ← calls gpu3dsDrawVertexes() internally
DRAW_4COLOR_BG_INLINE(2, 0, 2, 5);             // BG2           ← calls gpu3dsDrawVertexes() internally
```

This means: the render target at draw time is whatever was last set. To get
separate L/R eye renders, **the entire main screen must be rendered twice** —
once with the L eye target, once with the R eye target. A `g_stereoEyeSign`
global controls which horizontal offset direction is applied in
`gpu3dsAddTileVertexes()` on each pass.

This is implemented as the "double-render" approach — see DESIGN.md for
the full architecture.

All 8 BG modes (0–7) follow the same pattern. Mode 7 is the exception —
it uses a completely different scanline-based rendering path and is out of
scope for v1 stereo.

---

### Q4: Does the codebase use citro3D, citro2D, or raw GPU commands?

**Raw GPU commands exclusively.**

The file `source/gpulib.h` is a wrapper around deprecated raw GPU functions
(`GPU_DrawArray`, `GPU_SetViewport`, `GPU_SetTexture`, `GPU_SetBlendingColor`,
etc.). These are called directly without citro3D abstractions.

GPU command buffers (`gpuCommandBuffer1/2`) are managed manually. `GPUCMD_*`
macros (e.g., `GPUCMD_AddWrite`, `GPUCMD_AddMaskedWrite`) directly write PICA200
GPU register values.

The custom abstraction layer is in `source/3dsgpu.{h,cpp}`. It provides:
- `gpu3dsSetRenderTargetToTexture()` — sets GPUREG_COLORBUFFER_LOC etc.
- `gpu3dsBindTexture()` — sets GPUREG_TEXUNIT0_* registers
- `gpu3dsFlush()` — submits command buffer, waits for P3D completion
- `GX_DisplayTransfer()` — tiled GPU output → linear LCD buffer

This means we **cannot** use `C3D_RenderTargetCreate()` or
`Mtx_PerspStereoTilt()` from citro3D. All stereo infrastructure must be built
using the same raw GPU API already in use.

---

### Q5: What is the current frame rendering path from PPU output → 3DS screen?

Full path (simplified):

```
SNES PPU register writes
  → FLUSH_REDRAW() (inline, ppu.h)
    → S9xUpdateScreenHardware() (gfxhw.cpp:4056)
      → [sub screen if needed]
        → gpu3dsSetRenderTargetToSubScreenTexture()
        → S9xRenderScreenHardware(sub=true)      builds sub vertices
          → gpu3dsDrawVertexes()                 draws prev sub vertices → subScreenTarget
      → gpu3dsSetRenderTargetToMainScreenTexture()
      → S9xRenderScreenHardware(sub=false)       builds main vertices
        → gpu3dsDrawVertexes()                   draws prev main vertices → mainScreenTarget

[after S9xMainLoop returns, in impl3dsRunOneFrame:]
  → gpu3dsSetRenderTargetToFrameBuffer(GFX_TOP)
  → gpu3dsBindTextureMainScreen()                binds snesMainScreenTarget
  → gpu3dsAddQuadVertexes(...)                   scale 256→400, center
  → gpu3dsDrawVertexes()                         composite to GPU3DS.frameBuffer (400×240)
  → gpu3dsTransferToScreenBuffer()
    → GX_DisplayTransfer(frameBuffer → gfxGetFramebuffer(GFX_TOP, GFX_LEFT))
  → gfxScreenSwapBuffers(GFX_TOP, false)         LCD display buffer swap
  → gpu3dsFlush()                                execute accumulated GPU commands
```

Key facts:
- There is a **1-frame render lag** due to vertex double-buffering. Frame N's
  geometry is rendered while frame N+1's geometry is being built.
- The GPU command buffer is not submitted until `gpu3dsFlush()` at the very end.
  All the `gpu3dsSet*` and `gpu3dsDrawVertexes` calls during the frame are
  enqueuing commands, not executing immediately.
- `snesMainScreenTarget` is 256×256 (SNES native width is 256). The 400px
  width comes from the composite quad in `impl3dsRunOneFrame`.

---

### Q6: How does M2's implementation likely work?

M2 spent 18 months on 3D Space Harrier alone. Their methodology:

1. **Game analysis first.** Understand the original game's internal object
   hierarchy before touching rendering. For Genesis games (layers A, B, Window,
   sprites), they identified which elements belong at which Z-depth.

2. **Per-element depth assignment.** For Super Scaler arcade games (Space
   Harrier, Afterburner), sprite scale directly maps to Z. For Genesis,
   background planes and sprite priority groups map to depth planes.

3. **Dual rendering.** The scene is rendered twice with opposite horizontal
   offsets per depth plane. Left eye and right eye get different horizontal
   shifts per layer.

4. **Slider integration.** All offsets scale linearly with `osGet3DSliderState()`.
   At 0 the game renders identically to the mono version (no performance cost).

For SNES specifically, the layer structure maps cleanly:
- SNES BG0–BG3 and OBJ are already the "depth planes" M2 would have had to
  reverse-engineer in non-layered games.
- We have the exact layer index available at the time each tile is drawn.
- No reverse engineering required — the emulator knows everything.

The key difference from our approach: M2 had access to original source code
and rebuilt the rendering pipeline. We're modifying an emulator that already
separates layers during rendering, making our task easier.

---

### Q7: What is the VRAM budget on New 3DS, and can it support the extra targets?

**New 3DS has 6 MB VRAM. We need ~907 KB extra. This is fine.**

Current VRAM allocations (approximate):
```
GPU3DS.frameBuffer          400×240 RGBA8 = 375 KB
GPU3DS.frameDepthBuffer     400×240 RGBA8 = 375 KB
snesMainScreenTarget        256×256 RGBA8 = 256 KB
snesSubScreenTarget         256×256 RGBA8 = 256 KB
snesDepthForScreens         256×256 RGBA8 = 256 KB
snesDepthForOtherTextures   512×512 RGBA8 = 1024 KB
snesMode7Tile0Texture       16×16   RGBA4 = 0.5 KB
snesMode7FullTexture        1024×1024 RGBA4 = 2048 KB  ← largest single item
borderTexture               (optional, ~256 KB)

LCD framebuffers            (managed by GSP, ~1.5 MB total for all buffers)
```

Estimated total current VRAM usage: ~4.5–5 MB out of 6 MB.

**Additional requirements for stereo:**
```
snesMainScreenTargetL       256×256 RGBA8 = 256 KB   (new)
snesMainScreenTargetR       256×256 RGBA8 = 256 KB   (new)
snesDepthForScreensL        256×256 RGBA8 = 256 KB   (new)
snesDepthForScreensR        256×256 RGBA8 = 256 KB   (new, or share depthForScreens)
```

That's 768 KB–1 MB additional. At worst case (existing ~5 MB + 1 MB new = 6 MB),
this is tight but should work. The depth buffer for both screen targets can
potentially share one texture (`snesDepthForScreens`) since L and R renders
happen sequentially, not simultaneously.

**Vertex buffer memory** (linear RAM, not VRAM):
Current `REAL3DS_TILE_BUFFER_SIZE = 3 MB`. Two stereo buffers would be 6 MB.
This is in linear RAM (not VRAM), so the 256 MB FCRAM is not a concern.

---

### Q8: Are there existing 3DS homebrew projects that do per-layer stereoscopic rendering?

**No working examples found.** Only two relevant projects exist:

**pianomanty/snes9x_3ds_3D** — The only snes9x fork with "3D" in the name.
Analysis reveals it does NOT implement true stereoscopic rendering. It only:
- Enables the hardware parallax barrier via `gfxSet3D(true)`
- Reads the slider position to decide whether to enable/disable the barrier
- Transfers the same 2D image to `GFX_LEFT` only
- Both eyes see an identical image → zero depth effect

This is "3D hardware on but nothing displayed to right eye" — a non-implementation.

**devkitPro 3ds-examples/graphics/gpu/stereoscopic_2d** — Uses citro3D, not
raw GPU commands. The pattern (dual render targets, slider-based offset,
`gfxSet3D(true)`) is architecturally correct but cannot be directly adopted
since we use a different GPU API.

**Conclusion:** We are writing genuinely new code. There is no prior art to copy.

---

## What Was Changed (Implementation)

### Architecture: double-render with g_stereoEyeSign

The initially designed "dual vertex buffer" approach (accumulate L and R tile
geometry simultaneously) was discarded after detailed code analysis revealed
that tiles are drawn immediately per-layer inside each DRAW_* macro — there is
no single batch point to split L/R.

The implemented approach: render the main screen **twice per frame**. The
`g_stereoEyeSign` global (+1 for L, -1 for R, 0 for mono) is read inside
`gpu3dsAddTileVertexes()` to apply the per-eye horizontal offset using the
existing single `tileVertexes` list.

### Files changed

| File | Summary of change |
|------|-------------------|
| `source/stereo3d/LayerRenderer.h` | NEW — globals, function declarations (C++ only, no extern "C") |
| `source/stereo3d/LayerRenderer.cpp` | NEW — lifecycle, slider, depth profile |
| `source/stereo3d/DepthProfiles.h` | NEW — 5 depth profiles, STEREO_LAYER_* constants |
| `source/3dsimpl_gpu.h` | Stereo externs; g_stereoEyeSign offset in gpu3dsAddTileVertexes() |
| `source/3dsimpl_gpu.cpp` | gpu3dsSetRenderTargetToStereoEyeTexture() helper |
| `source/3dsimpl.cpp` | stereo3dsInit/Finalize/Swap calls; dual composite and transfer |
| `source/3dsgpu.h` | frameBufferR field; right-eye functions |
| `source/3dsgpu.cpp` | frameBufferR allocation; gpu3dsTransferRightEyeToScreenBuffer() |
| `source/Snes9x/gfxhw.cpp` | g_stereoCurrentLayer before all DRAW_*; double-render in S9xUpdateScreenHardware |
| `source/3dsmain.cpp` | stereo3dsUpdateSlider() per frame; gfxSet3D(true) |
| `Makefile` | Added stereo3d/LayerRenderer.cpp; mkdir -p build/stereo3d |

### Build status

✅ **Compiles clean** — no errors, no warnings from cross-compiler (devkitARM).
Host clang shows false positives for 3DS-specific identifiers; these are harmless.

⚠️ **NOT YET HARDWARE TESTED** — see DESIGN.md Hardware Testing Checklist.

---

## Implementation Order (completed)

1. ✅ Create `stereo3d/` scaffold files
2. ✅ Add `frameBufferR` — allocate from existing frameBuffer allocation (zero extra VRAM)
3. ✅ Allocate stereo VRAM textures in `stereo3dsInit()`
4. ✅ `gfxSet3D(true)` + `gfxScreenSwapBuffers(GFX_TOP, true)` in stereo path
5. ✅ Implement `g_stereoEyeSign` offset in `gpu3dsAddTileVertexes()`
6. ✅ Add `g_stereoCurrentLayer` tracking in all 8 BGMode cases in `gfxhw.cpp`
7. ✅ Double-render main screen (L/R eye) in `S9xUpdateScreenHardware`
8. ✅ Dual composite + dual transfer in `impl3dsRunOneFrame`
9. ✅ `stereo3dsUpdateSlider()` in main loop
10. ✅ Build clean
11. ⬜ Hardware validation

---

## Licensing

snes9x-3ds is based on snes9x (GPL-licensed). matbo87's fork is also GPL.
Any fork of this project must remain GPL-compatible. The `stereo3d/`
scaffold code written for this project inherits the same license.

There are no licensing conflicts with the approach. We are not incorporating
any code from closed-source projects (M2, etc.) — only the architectural
concept.

---

## The 10 Most Important Files to Modify/Understand

| Priority | File | Why |
|----------|------|-----|
| 1 | `source/3dsimpl_gpu.h` | Contains `gpu3dsAddTileVertexes` (the core injection point) and `SGPU3DSExtended` (needs L/R vertex lists) |
| 2 | `source/Snes9x/gfxhw.cpp` | Layer rendering loop — needs `g_stereoCurrentLayer` tracking and the modified draw dispatch at line ~4045 |
| 3 | `source/3dsimpl.cpp` | Frame orchestration — dual composite + dual transfer |
| 4 | `source/stereo3d/LayerRenderer.h` | NEW — globals, constants, function declarations |
| 5 | `source/stereo3d/LayerRenderer.cpp` | NEW — `stereo3dsInit`, `stereo3dsDrawBothEyes`, `stereo3dsTransferToEye` |
| 6 | `source/stereo3d/DepthProfiles.h` | NEW — depth table, game-specific overrides |
| 7 | `source/3dsimpl_gpu.cpp` | Stereo vertex list alloc/dealloc, swap helper |
| 8 | `source/3dsgpu.cpp` | `gfxSet3D(true)` and `hasStereo=true` in swap |
| 9 | `source/3dsgpu.h` | May need additional fields in `SGPU3DS` if tracking stereo state there |
| 10 | `source/Snes9x/ppu.h` | `FLUSH_REDRAW` — understand when `S9xUpdateScreenHardware` fires (per scanline group, not per frame) |
