# Mode 7 Perspective Stereo 3D — Design Spec

**Date:** 2026-03-13
**Branch:** `stereo-v2-mode7` (from `stereo-v2`)
**Status:** Design approved, pending implementation

## Problem

Mode 7 backgrounds (F-Zero road, Pilotwings ground, Super Mario Kart track) render as mono in the current V2 stereo implementation. The geometry shader's Mode 7 branch (`jmpc cmp.x, mode7` in `shader_tiles.g.pica:54-55`) fires before the `stereoOffset` application code, making Mode 7 tiles inherently unaffected by the stereo uniform.

## Goal

Add perspective depth to Mode 7 backgrounds: near scanlines (bottom of screen) sit at the screen plane, far scanlines (top/horizon) recede into the screen. This creates the effect of looking down at a ground plane through the 3DS parallax barrier — the "perspective depth" effect that M2's 3D Classics achieve for Mode 7 games.

## Approach: GPU-Only Y-Based Depth Scaling

Apply the `stereoOffset` uniform inside the Mode 7 geometry shader branch, scaled by projected screen Y position. No CPU-side vertex duplication, no extra sections, no extra draw calls.

### Why not CPU-side duplication (John's approach)?

John's `codex/stereo-v3-safe` branch implements Mode 7 stereo by duplicating Mode 7 scanline vertices at the CPU level — shifting the affine transform X input per eye and tagging sections with `eyeMask` (LEFT/RIGHT/BOTH). This is physically more correct (uses the actual Mode 7 matrix) but carries significant overhead:

- Doubles Mode 7 vertex count (480 → 960 per frame)
- Doubles Mode 7 section count (1-2 → 2-4 per BG)
- Adds `eyeMask` filtering in the draw loop, bypassing `gpu3dsDrawLayerByIndices` batching
- ~560 lines of new/modified code across 7 files
- Introduces `SLayerBuildState`, `SMode7Projection`, rewritten `gpu3dsCommitLayerSection` reuse path
- EXTBG guard (`S9xCanReuseMode7FullFromSub`) needed to prevent broken section reuse

The GPU approach achieves the same visual effect in ~40 lines across 6 files, with zero CPU overhead and zero architectural changes.

### Why Y-linear is sufficient

The "correct" perspective depth follows a 1/Z relationship. But on a 240-scanline SNES display behind a 400x240 parallax barrier, the visual difference between linear-in-Y and 1/Z depth curves is imperceptible. M2 hand-tunes depth curves per game regardless — a user-adjustable `StereoMode7Scale` gauge provides the same artistic control.

## Design

### 1. Geometry Shader Change (`shader_tiles.g.pica`)

Insert 5 instructions at the top of the `mode7:` branch, before the first `setemit`:

```pica
mode7:
    ; ── Stereo 3D perspective depth for Mode 7 ──
    ; r0.y = projected screen Y (clip space, Mtx_Ortho(0,256,0,256))
    ;   top (Y=0, far ground):    r0.y = -1.0
    ;   bottom (Y=239, near):     r0.y = +0.867  (not +1.0; vpHeight=256)
    ; depthScale = (1 - r0.y) → [0.133..2.0]: ~0 at bottom, 2 at top
    ; CPU sets stereoOffset.x = mode7Scale * iod * eyeSign * stretchComp * (1/256)
    ; The ~2x range roughly absorbs the factor of 2 vs tile layers' 2/256 constant.
    ; Bottom residual (0.133) is ~0.26px at typical settings — imperceptible.

    mov     r8, stereoOffset        ; PICA200 geom ALU: must mov uniform to temp
    add     r9.x, r7.y, -r0.y      ; r9.x = 1.0 - projectedY  (r7.y = 1.0)
    mul     r8.x, r8.x, r9.x       ; finalOffset = baseOffset * depthScale
    add     r0.x, r0.x, r8.x       ; shift left edge
    add     r3.x, r3.x, r8.x       ; shift right edge

    add     r3.y, r0.y, r7.w       ; (existing) r3.y = r0.y + 1 scanline
```

**Register usage:**
- `r7.y` = 1.0 (from `c_base`, loaded at line 25)
- `r8` = temp for stereoOffset (same register used in tile path)
- `r9.x` = temp for depthScale (unused register in Mode 7 branch)
- `r0`, `r3` = projected vertex positions (already in use)

**Mono fast path:** When slider=0 or Mode7Scale=0, `stereoOffset.x = 0`. The `mul` produces 0, and the two `add r_.x` instructions are no-ops. No branching needed.

### 2. Draw Loop Change (`3dsimpl_gpu.cpp`)

In `gpu3dsDrawLayers()`, the per-layer stereo offset block currently sets offset for tile BGs and 0 for everything else. Add Mode 7 detection:

```cpp
if (stereoEnabled) {
    if (PPU.BGMode == 7 && id <= LAYER_BG1) {
        // Mode 7 BG: shader applies per-scanline Y-scaled depth.
        // Both BG0 and EXTBG BG1 use Mode 7 scanline vertices
        // (VBO_SCENE_MODE7_LINE via S9xDrawBackgroundMode7Hardware),
        // so both go through the geometry shader's mode7 branch.
        float mode7Scale = settings3DS.StereoMode7Scale / 20.0f;
        gpu3dsSetStereoOffset(mode7Scale * iod * eyeSign * stretchCompensation * (1.0f / 256.0f));
    } else {
        float depthFactor = getStereoDepthFactor(id);
        float layerScale = getStereoLayerScale(id);
        gpu3dsSetStereoOffset(depthFactor * layerScale * iod * eyeSign * stretchCompensation * (2.0f / 256.0f));
    }
}
```

**Why `1.0f / 256.0f` instead of `2.0f / 256.0f`:** The shader's `(1 - projectedY)` range is [0.133, 2.0] (not [0, 2] — see Y range note below), providing roughly the factor of 2 that tile layers get from the `2/256` constant. This keeps Mode 7 scale gauge values perceptually equivalent to tile layer scale gauges.

**Why `id <= LAYER_BG1` with no EXTBG guard:** Both BG0 (standard Mode 7) and EXTBG BG1 use Mode 7 scanline vertices via `S9xDrawBackgroundMode7Hardware()` → `VBO_SCENE_MODE7_LINE`. The geometry shader's `-16384` sentinel triggers the `mode7:` branch for both. EXTBG BG1 should get the same perspective depth treatment as BG0.

**Projected Y range correction:** The SNES_MAIN render target is 256x256 (next-power-of-2), so `Mtx_Ortho(0, 256, 0, 256)` maps screen Y ∈ [0, 239] to clip Y ∈ [-1.0, +0.867], NOT [-1.0, +1.0]. The vertex shader (`shader_tiles.v.pica` lines 84-92) decodes the packed `Y+depth` value into screenY ∈ [0, 255] before passing to the geometry shader. Consequence: `(1 - r0.y)` at Y=239 (bottom/near) = 0.133, not 0. This means the bottom of the screen has ~6.6% of maximum parallax instead of exactly zero. At typical settings (Mode 7 Scale = 20, IOD = 0.5), this is ~0.26 pixels of disparity — well below the perceptible threshold on the 3DS parallax barrier. The CPU-side scale factor absorbs this: users control the absolute parallax via the Mode 7 Scale gauge.

### 3. Settings (`3dssettings.h`, `3dssettings.cpp`, `3dsmain.cpp`, `3dsconfig.h`)

Cherry-pick John's settings additions (they follow existing patterns exactly):

- **Fields:** `StereoMode7Scale` + `GlobalStereoMode7Scale` (int, range 0-40, default 0)
- **Menu:** Gauge labeled "Mode 7 Scale" between OBJ Scale and Backdrop Scale
- **Config:** `StereoMode7Scale=%d\n` and `GlobalStereoMode7Scale=%d\n` in read/write lists
- **Defaults:** 0 (disabled — safe default, user opts in)
- **Reset:** Included in the "Reset all to default" action, resets to **0** (not 20). Mode 7 stereo is opt-in because not all Mode 7 games look right at default depth — unlike tile layers which have emulator-derived depth factors, Mode 7 depth is a single linear gradient. Resetting to 0 is the safe choice.
- **UseGlobal sync:** Synced in both directions (global↔per-game) in the `makeOptionMenu` toggle callback AND in `settings3dsUpdate()` (the config-load path). The `settings3dsUpdate()` sync line is required for UseGlobal to take effect on game load, not just interactive toggle.
- **Config versions:** Bump to 1.5 (global) and 1.3 (game)

### 4. OBJ Scale Label Update (`3dsmain.cpp`)

John also updated the OBJ Scale gauge label from `"OBJ Scale (no effect yet)"` to `"OBJ Scale"`. We take this change since it's on `stereo-v2-features` already and represents the intent to activate OBJ stereo.

## Commit Plan

1. `feat(shader): add perspective stereo offset to Mode 7 geometry shader branch`
2. `feat(stereo): apply Mode 7 depth offset in draw loop`
3. `feat(settings): add Mode 7 Scale gauge and config persistence`

## Files Changed

| File | Change | Est. Lines |
|------|--------|-----------|
| `source/shader_tiles.g.pica` | 5 new instructions + comments in Mode 7 branch | ~10 |
| `source/3dsimpl_gpu.cpp` | Mode 7 detection + offset in stereo draw loop | ~8 |
| `source/3dssettings.h` | 2 new int fields | ~2 |
| `source/3dssettings.cpp` | Defaults (0) + UseGlobal sync in `settings3dsUpdate()` | ~5 |
| `source/3dsmain.cpp` | Menu gauge + config R/W + reset + OBJ label | ~20 |
| `source/3dsconfig.h` | Config version bump 1.4→1.5, 1.2→1.3 | ~2 |

**Total: ~46 lines** across 6 files.

## What This Does NOT Include

- OBJ per-priority stereo (John's OBJ work — separate branch/review)
- `eyeMask` / `SECTION_EYE_*` system (not needed for GPU approach)
- `SLayerBuildState` tracking (not needed without multi-section OBJ)
- Changes to `gpu3dsCommitLayerSection` reuse path (not needed)
- `S9xProjectMode7Line` / `S9xDrawMode7SectionHardware` (not needed)

## Hardware Testing

1. **F-Zero** — Slide Mode 7 Scale to 10-20, verify road recedes into screen with perspective gradient
2. **F-Zero** — Mode 7 Scale = 0, verify identical to current mono (no regression)
3. **Pilotwings** — EXTBG Mode 7, verify BG0 and BG1 both get depth
4. **Super Mario Kart** — Verify Mode 7 rotation doesn't break depth direction
5. **Any Mode 7 game with HFlip** — Verify depth still works (Y-based, independent of texture flip)
6. **Performance** — Compare FPS with stereo-v2 baseline, expect zero measurable difference
7. **Settings** — Adjust Mode 7 Scale gauge, verify visual change. Toggle UseGlobal, verify sync.

## Risk Assessment

**Low risk.** The change is additive (5 shader instructions, gated by a uniform that defaults to 0). The mono fast path is unchanged. No architectural modifications to the layer/section/VBO pipeline. Config version bump is the only breaking change (resets saved settings on first load with new version).
