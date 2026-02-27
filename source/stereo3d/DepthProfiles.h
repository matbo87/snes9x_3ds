#pragma once

//=============================================================================
// DepthProfiles.h
// Per-layer depth table for stereoscopic 3D rendering.
//
// Depth sign convention:
//   Positive value → layer recedes INTO the screen (convergence)
//   Negative value → layer pops OUT toward the viewer (divergence)
//   Zero           → at screen plane
//
// Depth values are in pixel units at slider=1.0, maxDepth=1.0, comfortScale=0.5.
// Effective shift = depth * slider * maxDepth * STEREO_COMFORT_SCALE.
//
// Layer indices (STEREO_LAYER_*):
//   0 = BG0 (SNES BG1 — main gameplay layer in Mode 1: ground, platforms)
//   1 = BG1 (SNES BG2 — parallax scenery: hills, clouds, distant elements)
//   2 = BG2 (SNES BG3 — lowest-priority BG: far layer or status elements)
//   3 = BG3 (HUD / priority tiles — Mode 1 BG3 is often the HUD layer)
//   4 = OBJ / Sprites (characters, enemies, projectiles)
//=============================================================================

#define STEREO_LAYER_BG0        0
#define STEREO_LAYER_BG1        1
#define STEREO_LAYER_BG2        2
#define STEREO_LAYER_BG3        3
#define STEREO_LAYER_OBJ        4
#define STEREO_LAYER_COUNT      5

#define STEREO_COMFORT_SCALE    0.5f   // Global safety factor — reduce if eye strain

//-----------------------------------------------------------------------------
// SDepthProfile — depth values for a single game/mode configuration
//-----------------------------------------------------------------------------
typedef struct {
    const char *name;                       // Profile name for display / logging
    float depth[STEREO_LAYER_COUNT];        // [BG0, BG1, BG2, BG3, OBJ]
} SDepthProfile;


//-----------------------------------------------------------------------------
// Default profile — Mode 1 (BG0/BG1 16-color, BG2 4-color)
// Used for the majority of SNES games.
//
// In Mode 1, the typical layer usage for platformers (SMW, DKC) is:
//   BG0 (SNES BG1) = main gameplay layer (ground, platforms) — moderate depth
//   BG1 (SNES BG2) = parallax scenery (hills, clouds) — deepest
//   BG2 (SNES BG3) = distant layer or status elements — slight depth
//   BG3            = HUD / priority tiles — at screen plane
//   OBJ            = sprites — pop toward viewer
//-----------------------------------------------------------------------------
static const SDepthProfile DEPTH_PROFILE_DEFAULT = {
    "Default (Mode 1)",
    { + 5.0f,   // BG0 — main gameplay (ground, platforms)
      +10.0f,   // BG1 — distant scenery (hills, sky)
      + 2.0f,   // BG2 — far elements or status
        0.0f,   // BG3 — HUD / screen plane
      - 8.0f }  // OBJ — sprites pop out
};

//-----------------------------------------------------------------------------
// Platform / action game profile
// For games where BG3 is distant scenery rather than HUD.
//-----------------------------------------------------------------------------
static const SDepthProfile DEPTH_PROFILE_ACTION = {
    "Action/Platform",
    { + 8.0f,   // BG0
      + 5.0f,   // BG1
      + 2.0f,   // BG2
      + 1.0f,   // BG3 — slight depth (not HUD)
      -14.0f }  // OBJ
};

//-----------------------------------------------------------------------------
// RPG profile
// For games with large map views where sprites are part of the ground plane.
//-----------------------------------------------------------------------------
static const SDepthProfile DEPTH_PROFILE_RPG = {
    "RPG (top-down)",
    { + 6.0f,   // BG0 — map layer
      + 3.0f,   // BG1 — under-layer decoration
        0.0f,   // BG2 — shadows / effects
        0.0f,   // BG3 — HUD
      - 8.0f }  // OBJ — characters pop slightly
};

//-----------------------------------------------------------------------------
// Flat / HUD-heavy profile
// For games where everything sits at the screen plane with minimal depth.
// Useful as a test baseline or for puzzle games.
//-----------------------------------------------------------------------------
static const SDepthProfile DEPTH_PROFILE_FLAT = {
    "Flat (minimal depth)",
    { + 3.0f,
      + 2.0f,
      + 1.0f,
        0.0f,
      - 4.0f }
};

//-----------------------------------------------------------------------------
// Mode 0 profile (all 4 BGs active, 4-color each)
// Uncommon — used by a handful of games (Gradius III, Soul Blazer)
//-----------------------------------------------------------------------------
static const SDepthProfile DEPTH_PROFILE_MODE0 = {
    "Mode 0 (4-BG)",
    { +12.0f,   // BG0 — highest priority BG
      + 8.0f,   // BG1
      + 4.0f,   // BG2
      + 1.0f,   // BG3 — lowest BG
      -12.0f }  // OBJ
};

//-----------------------------------------------------------------------------
// All profiles in one array for menu selection
//-----------------------------------------------------------------------------
#define DEPTH_PROFILE_COUNT 5
static const SDepthProfile *DEPTH_PROFILES[DEPTH_PROFILE_COUNT] = {
    &DEPTH_PROFILE_DEFAULT,
    &DEPTH_PROFILE_ACTION,
    &DEPTH_PROFILE_RPG,
    &DEPTH_PROFILE_FLAT,
    &DEPTH_PROFILE_MODE0
};

//-----------------------------------------------------------------------------
// Per-game override table
// TODO: Populate during hardware testing for specific titles that need tuning.
//
// Usage: at ROM load, scan this table for the ROM's internal header name.
// If found, use that profile index instead of the user-selected default.
//
// struct SGameDepthOverride {
//     const char *romInternalName;   // First 21 chars from ROM header (0xFFC0)
//     int         profileIndex;      // Index into DEPTH_PROFILES[]
// };
//
// static const SGameDepthOverride GAME_OVERRIDES[] = {
//     { "SUPER MARIO WORLD     ", 1 },  // Action profile
//     { "FINAL FANTASY 3      ",  2 },  // RPG profile
//     // ... add entries as tested
//     { NULL, 0 }                       // sentinel
// };
//
// NOTE: This entire override system is TODO — left as scaffold for v2.
//-----------------------------------------------------------------------------
