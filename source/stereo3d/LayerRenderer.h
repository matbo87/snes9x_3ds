#pragma once

//=============================================================================
// LayerRenderer.h
// Stereoscopic 3D layer rendering for SNES9x-3DS.
//
// Architecture: double-render with per-eye horizontal offset.
//   - S9xUpdateScreenHardware (gfxhw.cpp) renders the main screen TWICE:
//       L eye: render target = stereo3dsMainScreenTargetL, g_stereoEyeSign = +1
//       R eye: render target = stereo3dsMainScreenTargetR, g_stereoEyeSign = -1
//   - gpu3dsAddTileVertexes() reads g_stereoEyeSign and g_stereoLayerDepths[]
//     to apply a per-layer horizontal offset to each tile vertex.
//   - g_stereoCurrentLayer is set before each DRAW_* macro so the correct
//     per-layer depth is used.
//   - impl3dsRunOneFrame composites each target to frameBuffer / frameBufferR
//     and transfers to GFX_LEFT / GFX_RIGHT via GX_DisplayTransfer.
//
// Integration points:
//   1. source/3dsimpl_gpu.h      — g_stereoEyeSign/CurrentLayer externs;
//                                  stereo offset in gpu3dsAddTileVertexes()
//   2. source/3dsimpl.cpp        — allocate L/R VRAM textures (stereo3dsInit);
//                                  dual composite/transfer in impl3dsRunOneFrame
//   3. source/Snes9x/gfxhw.cpp  — set g_stereoCurrentLayer before each DRAW_*;
//                                  double-render main screen for L/R eyes in
//                                  S9xUpdateScreenHardware
//   4. source/3dsmain.cpp        — call stereo3dsUpdateSlider() each frame
//   5. Makefile                  — add source/stereo3d/LayerRenderer.cpp
//=============================================================================

#include <3ds.h>
#include "../3dssnes9x.h"
#include "DepthProfiles.h"

//-----------------------------------------------------------------------------
// Left/right eye VRAM render targets (allocated in stereo3dsInit)
// Used as render targets by S9xUpdateScreenHardware and as source textures
// by impl3dsRunOneFrame for the composite step.
//-----------------------------------------------------------------------------
extern SGPUTexture *stereo3dsMainScreenTargetL;
extern SGPUTexture *stereo3dsMainScreenTargetR;


//-----------------------------------------------------------------------------
// Global state
//   g_stereoCurrentLayer — set from gfxhw.cpp before each DRAW_* call
//   g_stereoEnabled      — true when slider > threshold and BGMode != 7
//   g_stereoEffective    — pre-scaled slider value (slider * maxDepth * COMFORT_SCALE)
//   g_stereoLayerDepths  — per-layer signed pixel depth at effective=1.0
//   g_stereoEyeSign      — set per-eye by S9xUpdateScreenHardware: +1 L, -1 R, 0 mono
//
// These are also declared extern in 3dsimpl_gpu.h for access inside the
// gpu3dsAddTileVertexes() inline.
//-----------------------------------------------------------------------------
extern int   g_stereoCurrentLayer;
extern bool  g_stereoEnabled;
extern float g_stereoEffective;
extern float g_stereoLayerDepths[STEREO_LAYER_COUNT];
extern int   g_stereoEyeSign;


//-----------------------------------------------------------------------------
// Lifecycle
//-----------------------------------------------------------------------------

// Call once during impl3dsInitializeCore() after GPU is up.
// Allocates stereo3dsMainScreenTargetL/R in VRAM.
// Returns false if VRAM allocation fails (non-fatal — stereo stays disabled).
bool stereo3dsInit();

// Call during impl3dsFinalize().
void stereo3dsFinalize();


//-----------------------------------------------------------------------------
// Per-frame update
//-----------------------------------------------------------------------------

// Call once per frame before impl3dsRunOneFrame(), from the main gameplay loop.
// Reads osGet3DSliderState(), updates g_stereoEnabled and g_stereoEffective.
// Pass isModeSevenFrame=true to force mono for Mode 7 frames.
void stereo3dsUpdateSlider(bool isModeSevenFrame);

// Load a depth profile by index (0..DEPTH_PROFILE_COUNT-1).
// Copies profile depths into g_stereoLayerDepths[].
void stereo3dsSetDepthProfile(int profileIndex);


//-----------------------------------------------------------------------------
// Per-frame vertex list reset (called from impl3dsPrepareForNewFrame)
//-----------------------------------------------------------------------------

// Resets g_stereoCurrentLayer and g_stereoEyeSign to their between-frame
// defaults.  Call alongside the mono gpu3dsSwapVertexListForNextFrame calls.
void stereo3dsSwapVertexListsForNextFrame();
