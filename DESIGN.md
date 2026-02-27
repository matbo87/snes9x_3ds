# DESIGN: SNES9x-3DS Stereoscopic 3D

**Status:** Implementation complete — build clean, pending hardware validation
**Based on:** Full source analysis of matbo87/snes9x_3ds (Feb 2026)
**Approach:** Double-render with per-eye horizontal offset (g_stereoEyeSign)

---

## Background & Goal

The Nintendo 3DS top screen is physically 800×240 pixels. A parallax barrier
sits in front of the LCD, directing alternating pixel columns to each eye. The
hardware makes two 400×240 views — left eye and right eye — appear at different
horizontal positions, creating depth perception without glasses.

The goal is to expose SNES background layer separation as real stereoscopic
depth, in the style of M2's Sega 3D Classics series.

SNES games naturally compose their scenes from up to 4 background layers (BG0–
BG3) and a sprite layer (OBJ). In Mode 1 (used by ~90% of games), the layer
order is:

```
Front ← Sprites → BG0 → BG1 → BG2 → Back
```

By assigning different horizontal parallax offsets to each layer — positive
offsets recede into the screen, negative offsets pop toward the viewer — we get
true per-layer stereoscopic depth from the SNES's own layer structure.

---

## How the Existing Renderer Works

Understanding the current pipeline is essential before modifying it.

### GPU API

The codebase uses **raw PICA200 GPU commands** (`GPUCMD_*`, `GPU_DrawArray`,
`GX_DisplayTransfer`) — NOT citro3D or citro2D. There is a custom GPU
abstraction layer in `source/3dsgpu.{h,cpp}`.

### Vertex double-buffering

Every `SVertexList` (tile vertex buffer, etc.) maintains two memory allocations,
`List` and `PrevList`, swapped each frame via `gpu3dsSwapVertexListForNextFrame`.

- **CPU fills `List`** during the current frame (geometry for frame N).
- **GPU draws from `PrevList`** during the current frame (rendering frame N-1).
- At the end of the frame, swap: frame N's `List` becomes next frame's `PrevList`.

This gives a 1-frame render lag and allows CPU/GPU to overlap. It also means
the previous frame's vertex data is preserved in `PrevList` for the duration
of the current frame.

### Per-layer immediate draw (critical constraint)

**Important:** tiles are NOT batched across all layers and drawn once at the end
of `S9xRenderScreenHardware`. Instead:

Each `DRAW_*` macro calls a BG/OBJ draw function that **internally calls
`gpu3dsDrawVertexes(false, bg)`**. This submits vertices to the GPU command
buffer immediately at the end of each layer. The render target baked into the
command at submission time is whatever was last set.

This rules out the simpler "accumulate all layers, then choose which buffer to
draw to" approach. Instead, to get separate L/R eye renders, the entire main
screen must be rendered twice — once with the L eye target set, once with R.

### Frame render pipeline

```
impl3dsRunOneFrame()
│
├─ impl3dsPrepareForNewFrame()        ← swap vertex double-buffers
│
├─ gpu3dsSetRenderTargetToMainScreenTexture()   ← render target = snesMainScreenTarget
├─ gpu3dsUseShader(1)                           ← tile geometry shader
│
├─ S9xMainLoop()
│   └─ [per scanline group, via FLUSH_REDRAW → S9xUpdateScreenHardware]
│       ├─ [if subscreen needed]
│       │   ├─ gpu3dsSetRenderTargetToSubScreenTexture()
│       │   └─ S9xRenderScreenHardware(sub=true)    ← builds+draws sub layer-by-layer
│       │
│       └─ [main screen — stereo or mono]
│           ├─ [STEREO: render TWICE]
│           │   ├─ L eye: target=stereo3dsMainScreenTargetL, g_stereoEyeSign=+1
│           │   │   └─ S9xRenderScreenHardware(sub=false)  ← draws each layer with +dx
│           │   └─ R eye: target=stereo3dsMainScreenTargetR, g_stereoEyeSign=-1
│           │       └─ S9xRenderScreenHardware(sub=false)  ← draws each layer with -dx
│           │
│           └─ [MONO: render ONCE]
│               └─ S9xRenderScreenHardware(sub=false)
│
├─ [Composite to frameBuffer / frameBufferR (400×240)]
│   ├─ [STEREO]
│   │   ├─ Bind stereo3dsMainScreenTargetL → quad draw → frameBuffer
│   │   └─ Bind stereo3dsMainScreenTargetR → quad draw → frameBufferR
│   └─ [MONO]
│       └─ Bind snesMainScreenTarget → quad draw → frameBuffer
│
├─ [Transfer to LCD]
│   ├─ [STEREO]
│   │   ├─ GX_DisplayTransfer(frameBuffer  → GFX_LEFT)
│   │   ├─ GX_DisplayTransfer(frameBufferR → GFX_RIGHT)
│   │   └─ gfxScreenSwapBuffers(GFX_TOP, true)   ← enables parallax barrier
│   └─ [MONO]
│       ├─ GX_DisplayTransfer(frameBuffer → GFX_LEFT)
│       └─ gfxScreenSwapBuffers(GFX_TOP, false)
│
└─ gpu3dsFlush()                      ← submit accumulated GPU commands to hardware
```

### Key vertex structure

```cpp
struct STileVertex {
    SVector3i  Position;    // x, y, z  (z = GPU depth/priority for depth test)
    STexCoord2i TexCoord;   // u, v
};
```

Tiles are submitted as **2-vertex point primitives**. The geometry shader
(`shaderfast2_shbin`) expands each pair into a 6-vertex quad (2 triangles).

### VRAM allocations

| Texture | Size | Purpose |
|---------|------|---------|
| `snesMainScreenTarget` | 256×256 RGBA8 = 256 KB | Main screen render target (mono / color math) |
| `snesSubScreenTarget` | 256×256 RGBA8 = 256 KB | Sub screen render target |
| `snesDepthForScreens` | 256×256 RGBA8 = 256 KB | Depth buffer — shared by L+R (sequential) |
| `GPU3DS.frameBuffer` | 400×240 RGBA8 = 375 KB | L eye composite + mono composite |
| `GPU3DS.frameBufferR` | 400×240 RGBA8 = 375 KB | R eye composite (upper half of frameBuffer alloc) |
| `snesMode7FullTexture` | 1024×1024 RGBA4 = 2 MB | Mode 7 map |
| `snesTileCacheTexture` | 1024×1024 RGBA5551 = 2 MB | (linear RAM) Tile atlas |
| `stereo3dsMainScreenTargetL` | 256×256 RGBA8 = 256 KB | **NEW** L eye render target |
| `stereo3dsMainScreenTargetR` | 256×256 RGBA8 = 256 KB | **NEW** R eye render target |

`frameBufferR` costs zero extra VRAM — it is the upper half of the existing
768 KB `frameBuffer` allocation (`frameBuffer + SCREEN_TOP_WIDTH * SCREEN_HEIGHT`).

---

## Stereo Implementation

### Core concept

**Double-render with g_stereoEyeSign.**

The main screen is rendered **twice** per frame:

1. L eye: set render target → `stereo3dsMainScreenTargetL`, set `g_stereoEyeSign = +1`,
   call `S9xRenderScreenHardware(sub=false, ...)`.
2. R eye: set render target → `stereo3dsMainScreenTargetR`, set `g_stereoEyeSign = -1`,
   call `S9xRenderScreenHardware(sub=false, ...)` again.

Inside `gpu3dsAddTileVertexes()`, the offset is computed and applied to every tile:

```cpp
int16 stereo_dx = 0;
if (g_stereoEnabled && g_stereoEyeSign != 0 && g_stereoCurrentLayer >= 0)
{
    stereo_dx = (int16)(g_stereoLayerDepths[g_stereoCurrentLayer]
                        * g_stereoEffective * (float)g_stereoEyeSign);
}
// ... apply stereo_dx to x0, x1 of the mono tileVertexes ...
```

The **single** `tileVertexes` list is reused for both eyes — each render pass
rebuilds it from scratch with the correct sign applied.

### layerDrawn[] reset

`layerDrawn[]` is a local variable in `S9xUpdateScreenHardware` that caches
whether the sub screen was drawn for a given bg, to avoid double-drawing during
the main screen pass. Before each eye render, it must be reset:

```cpp
for (int i = 0; i < 7; i++) layerDrawn[i] = false;
```

This prevents the L eye render from replaying mono-offset sub screen draws.

### Depth sign convention

```
L eye x-offset  = +depth * g_stereoEffective   (sign = +1)
R eye x-offset  = -depth * g_stereoEffective   (sign = -1)
```

Positive depth → element shifts right for L eye, left for R eye → recedes into screen.
Negative depth → element shifts left for L eye, right for R eye → pops toward viewer.

```
Layer      Default depth   Effect
─────────────────────────────────────────────────────────────────
BG3          0.0           At screen plane (HUD / reference)
BG2         +3.0           Slightly into screen
BG1         +6.0           Mid-background
BG0        +10.0           Near background
Sprites    -12.0           Pop out toward viewer
```

### Parallax formula

```cpp
float slider    = osGet3DSliderState();          // 0.0 – 1.0
float maxDepth  = 1.0f;                          // user cap (hardcoded for v1)
float effective = slider * maxDepth * STEREO_COMFORT_SCALE;  // COMFORT_SCALE = 0.5

int dx = (int)(g_stereoLayerDepths[layer] * effective * g_stereoEyeSign);
```

`STEREO_COMFORT_SCALE = 0.5` keeps the default max offset ~6px for sprites.

### Layer index tracking

`g_stereoCurrentLayer` is set in `gfxhw.cpp` before each `DRAW_*` macro:

```cpp
// All 8 BGModes have this treatment. Example for Mode 1:
g_stereoCurrentLayer = STEREO_LAYER_OBJ;  DRAW_OBJS(0);
g_stereoCurrentLayer = STEREO_LAYER_BG0;  DRAW_16COLOR_BG_INLINE(0, 0, 8, 11);
g_stereoCurrentLayer = STEREO_LAYER_BG1;  DRAW_16COLOR_BG_INLINE(1, 0, 7, 10);
g_stereoCurrentLayer = STEREO_LAYER_BG2;  DRAW_4COLOR_BG_INLINE(2, 0, 2, 5);
```

---

## Files Modified

| File | Change |
|------|--------|
| `source/stereo3d/LayerRenderer.h` | NEW — globals, function declarations |
| `source/stereo3d/LayerRenderer.cpp` | NEW — lifecycle, slider update, depth profile |
| `source/stereo3d/DepthProfiles.h` | NEW — depth table for 5 game profiles |
| `source/3dsimpl_gpu.h` | Added stereo externs + `g_stereoEyeSign` offset in `gpu3dsAddTileVertexes` |
| `source/3dsimpl_gpu.cpp` | Added `gpu3dsSetRenderTargetToStereoEyeTexture()` helper |
| `source/3dsimpl.cpp` | `stereo3dsInit/Finalize/Swap` calls; dual composite + dual transfer |
| `source/3dsgpu.h` | Added `frameBufferR`; `gpu3dsSetRenderTargetToFrameBufferR`; `gpu3dsTransferRightEyeToScreenBuffer` |
| `source/3dsgpu.cpp` | Allocates `frameBufferR`; implements right-eye transfer |
| `source/Snes9x/gfxhw.cpp` | `g_stereoCurrentLayer` before all DRAW_*; double-render in `S9xUpdateScreenHardware` |
| `source/3dsmain.cpp` | `stereo3dsUpdateSlider()` call per frame; `gfxSet3D(true)` |
| `Makefile` | Added `stereo3d/LayerRenderer.cpp`; `mkdir -p build/stereo3d` |

---

## Performance Analysis

### Frame budget

- New 3DS XL: ARM11 MPCore at 804 MHz, PICA200 GPU at 268 MHz
- Target: 60 FPS = 16.67 ms/frame
- Mono render: ~8–10 ms (typical for most games)

### Stereo overhead

| Component | Overhead | Notes |
|-----------|----------|-------|
| Second main screen render (CPU) | ~30–40% | `S9xRenderScreenHardware` runs twice |
| Second GPU draw (PICA200) | ~30–40% | Two sets of tile draw commands queued |
| Extra composite + transfer | ~5–10% | Second quad + second GX_DisplayTransfer |
| **Total** | **~70–90%** | Sub screen remains mono |

With the slider at 0, `g_stereoEnabled = false` and the mono fast path is taken —
**zero overhead** vs baseline.

### Mode 7 handling

Mode 7 forces `stereo3dsUpdateSlider(isModeSevenFrame=true)` → `g_stereoEnabled = false`.
Mode 7 renders mono, same as upstream.

---

## Scope Limitations (v1)

1. **Sub screen is mono** — the sub-screen itself renders without stereo offset,
   but color math (transparency, additive/subtractive blending) and brightness
   (fade to/from black) are now applied to each stereo eye texture via the
   `g_stereoMainScreenOverride` redirect. Screen transitions and DKC water
   effects should work correctly. Acceptable fringing at layer edges where sub-
   screen blending meets offset geometry. Known limitation.

2. **Mode 7 games are mono** — F-Zero, Super Mario Kart, Pilotwings get no 3D.

3. **High-res modes (BGMode 5/6) are mono** — rare usage.

4. **No in-game depth tuning UI** — depth profiles are set by the `stereo3dsSetDepthProfile`
   call. Per-game overrides require editing `DepthProfiles.h`.

---

## Expected Visual Result (Mode 1, Default Depths)

With the 3D slider at 100% and `STEREO_COMFORT_SCALE = 0.5`:

```
Mario  (OBJ, depth -8)  ............... pops OUT ~2px
Ground (BG0, depth +5)  ............... ~1.25px INTO screen
Hills  (BG1, depth +10) ............... ~2.5px INTO screen (deepest)
Sky    (BG2, depth +2)  ............... ~0.5px INTO screen
HUD    (BG3, depth 0)   ............... at screen plane
```

---

## Hardware Testing Checklist

⚠️ **NOT YET TESTED** — pending first hardware validation.

- [ ] Slider at 0: game looks identical to upstream mono build
- [ ] Slider at 25%: backgrounds drift slightly into screen, sprites pop forward
- [ ] Slider at 100%: check for eye strain; verify no visible seam/artifact
- [ ] Color math game (Axelay, FFVI): check for color fringing on layer edges
- [ ] Mode 7 game (F-Zero): verify mono fallback works correctly, no regression
- [ ] HUD stays at screen plane in games using BG3 for HUD
- [ ] VRAM allocation succeeds (no NULL texture handles at startup)
- [ ] Right-eye buffer visible (parallax barrier enabled)
- [ ] Frame rate acceptable with stereo on New 3DS

---

## Open Questions (for hardware testing)

1. Are the default depth values comfortable at full slider, or do they need
   reducing? (STEREO_COMFORT_SCALE = 0.5 is conservative; tune from there.)

2. Do x-offset values outside [0, 255] clip correctly at the PICA200 viewport,
   or does the geometry shader produce artifacts?

3. Does `gfxScreenSwapBuffers(GFX_TOP, true)` fully enable the parallax barrier,
   or is additional GSPGPU register setup required?

4. Are there games where BG3 is used for gameplay elements (not HUD) that need
   depth > 0? (RPGs like Final Fantasy VI use BG3 for world map terrain.)

---

## References

- libctru `gfx.h`: `gfxSet3D`, `osGet3DSliderState`, `gfxGetFramebuffer`
- devkitPro `3ds-examples/graphics/gpu/stereoscopic_2d`: canonical stereo pattern
- M2 interview (Game Developer 2013): per-element depth assignment methodology
- SNES PPU: SNESdev wiki — backgrounds, priority ordering, Mode 1 layer stack
