//=============================================================================
// LayerRenderer.cpp
// Stereoscopic 3D layer rendering for SNES9x-3DS.
//
// Architecture: double-render with per-eye horizontal offset.
//   - Main screen is rendered TWICE per frame: once to stereo3dsMainScreenTargetL
//     (g_stereoEyeSign = +1) and once to stereo3dsMainScreenTargetR (g_stereoEyeSign = -1).
//   - gpu3dsAddTileVertexes() reads g_stereoEyeSign and g_stereoLayerDepths to offset
//     each tile's x coordinate for the active eye.
//   - g_stereoCurrentLayer is set before each DRAW_* macro in S9xRenderScreenHardware
//     so the correct per-layer depth is used.
//   - impl3dsRunOneFrame() composites each texture to frameBuffer / frameBufferR
//     and transfers to GFX_LEFT / GFX_RIGHT via GX_DisplayTransfer.
//=============================================================================

#include <3ds.h>
#include <string.h>

#include "../3dssnes9x.h"
#include "../3dsgpu.h"
#include "../3dsimpl_gpu.h"
#include "../3dsimpl.h"
#include "LayerRenderer.h"
#include "DepthProfiles.h"

//=============================================================================
// Public eye render targets (declared extern in LayerRenderer.h)
//=============================================================================

// Accessed by S9xUpdateScreenHardware (gfxhw.cpp) as render targets,
// and by impl3dsRunOneFrame() for the composite bind step.
SGPUTexture *stereo3dsMainScreenTargetL = NULL;
SGPUTexture *stereo3dsMainScreenTargetR = NULL;

//=============================================================================
// Public globals (declared extern in LayerRenderer.h / 3dsimpl_gpu.h)
//=============================================================================

int   g_stereoCurrentLayer = -1;
bool  g_stereoEnabled       = false;
float g_stereoEffective      = 0.0f;
int   g_stereoEyeSign        = 0;   // +1 = L eye, -1 = R eye, 0 = mono
float g_stereoLayerDepths[STEREO_LAYER_COUNT] = {
    +10.0f,   // BG0 — deep background
    + 6.0f,   // BG1 — mid-ground
    + 3.0f,   // BG2 — near background
      0.0f,   // BG3 — screen plane
    -12.0f    // OBJ — pop toward viewer
};


//=============================================================================
// stereo3dsInit
// Call once during impl3dsInitializeCore() after GPU textures are created.
//=============================================================================
bool stereo3dsInit()
{
    stereo3dsMainScreenTargetL = gpu3dsCreateTextureInVRAM(256, 256, GPU_RGBA8);
    if (!stereo3dsMainScreenTargetL) {
        printf("stereo3dsInit: failed to allocate stereo3dsMainScreenTargetL\n");
        return false;
    }

    stereo3dsMainScreenTargetR = gpu3dsCreateTextureInVRAM(256, 256, GPU_RGBA8);
    if (!stereo3dsMainScreenTargetR) {
        printf("stereo3dsInit: failed to allocate stereo3dsMainScreenTargetR\n");
        gpu3dsDestroyTextureFromVRAM(stereo3dsMainScreenTargetL);
        stereo3dsMainScreenTargetL = NULL;
        return false;
    }

    // No extra vertex list allocation needed: we reuse the existing mono
    // tileVertexes with a per-eye x-offset applied in gpu3dsAddTileVertexes().

    stereo3dsSetDepthProfile(0);
    return true;
}


//=============================================================================
// stereo3dsFinalize
// Call during impl3dsFinalize().
//=============================================================================
void stereo3dsFinalize()
{
    if (stereo3dsMainScreenTargetL) { gpu3dsDestroyTextureFromVRAM(stereo3dsMainScreenTargetL); stereo3dsMainScreenTargetL = NULL; }
    if (stereo3dsMainScreenTargetR) { gpu3dsDestroyTextureFromVRAM(stereo3dsMainScreenTargetR); stereo3dsMainScreenTargetR = NULL; }

    g_stereoEnabled   = false;
    g_stereoEffective = 0.0f;
    g_stereoEyeSign   = 0;
}


//=============================================================================
// stereo3dsUpdateSlider
// Call once per frame in the main loop, before impl3dsRunOneFrame().
//=============================================================================
void stereo3dsUpdateSlider(bool isModeSevenFrame)
{
    if (isModeSevenFrame) {
        g_stereoEnabled   = false;
        g_stereoEffective = 0.0f;
        g_stereoEyeSign   = 0;
        return;
    }

    // Hardcoded until UI settings are wired:
    const bool  userEnabled  = true;
    const float userMaxDepth = 1.0f;

    if (!userEnabled) {
        g_stereoEnabled   = false;
        g_stereoEffective = 0.0f;
        g_stereoEyeSign   = 0;
        return;
    }

    float slider = osGet3DSliderState();  // 0.0 – 1.0

    if (slider < 0.01f) {
        g_stereoEnabled   = false;
        g_stereoEffective = 0.0f;
        g_stereoEyeSign   = 0;
    } else {
        g_stereoEnabled   = true;
        g_stereoEffective = slider * userMaxDepth * STEREO_COMFORT_SCALE;
        // g_stereoEyeSign is set per-eye by S9xUpdateScreenHardware; leave it alone here.
    }
}


//=============================================================================
// stereo3dsSetDepthProfile
//=============================================================================
void stereo3dsSetDepthProfile(int profileIndex)
{
    if (profileIndex < 0 || profileIndex >= DEPTH_PROFILE_COUNT)
        profileIndex = 0;

    const SDepthProfile *p = DEPTH_PROFILES[profileIndex];
    for (int i = 0; i < STEREO_LAYER_COUNT; i++) {
        g_stereoLayerDepths[i] = p->depth[i];
    }
}


//=============================================================================
// stereo3dsSwapVertexListsForNextFrame
// Call from impl3dsPrepareForNewFrame() alongside the mono list swaps.
// Resets per-frame stereo state so the next frame starts clean.
//=============================================================================
void stereo3dsSwapVertexListsForNextFrame()
{
    g_stereoCurrentLayer = -1;
    g_stereoEyeSign      = 0;
}
