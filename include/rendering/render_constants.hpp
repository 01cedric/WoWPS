#pragma once

#include <cstdint>

// Rendering-domain constants: distances, LOD thresholds, particle tuning.

namespace wowee {
namespace rendering {

// ---------------------------------------------------------------------------
// M2 instance-count → render-distance mapping
// ---------------------------------------------------------------------------
constexpr uint32_t M2_HIGH_DENSITY_INSTANCE_THRESHOLD = 2000;
constexpr float    M2_MAX_RENDER_DISTANCE_HIGH_DENSITY = 800.0f;
constexpr float    M2_MAX_RENDER_DISTANCE_LOW_DENSITY  = 2800.0f;

// ---------------------------------------------------------------------------
// M2 LOD / bone-update distance thresholds (world units)
// ---------------------------------------------------------------------------
constexpr float M2_LOD3_DISTANCE        = 150.0f;  // Beyond this: no bone updates
constexpr float M2_BONE_SKIP_DIST_FAR   = 100.0f;  // Beyond this: every 4th frame
constexpr float M2_BONE_SKIP_DIST_MID   = 50.0f;   // Beyond this: every 2nd frame
// Flying ambient models have obvious, rapid wing motion.  They are exempt from
// bone frame-skipping and the LOD3 bone freeze (their flight path is baked into
// bone animation), so they stay fully animated out to this range instead of
// despawning at the generic no-bone LOD boundary.
constexpr float M2_SKY_BIRD_MAX_RENDER_DISTANCE = 320.0f;

// ---------------------------------------------------------------------------
// M2 culling geometry
// ---------------------------------------------------------------------------
constexpr float M2_CULL_RADIUS_SCALE_DIVISOR = 12.0f;
constexpr float M2_PADDED_RADIUS_SCALE       = 1.5f;
constexpr float M2_PADDED_RADIUS_MIN_MARGIN  = 3.0f;

// Distance floor for server game objects (mailboxes, chests, nodes, doors).
// The adaptive doodad distance collapses to its densest-scene value in any
// populated area - a city holds ~100k M2 instances, so ambient doodads are cut
// at 300 * viewDistanceScale, i.e. 200 world units at a mid view-distance
// setting. Applying that to game objects hides objects the player interacts
// with. The server only sends a game object when it is inside its own
// visibility radius, so anything we have been told about is worth drawing:
// this floor sits comfortably beyond any server visibility setting, leaving
// frustum and occlusion culling to do the real work.
constexpr float M2_GAME_OBJECT_MIN_RENDER_DISTANCE = 600.0f;

// ---------------------------------------------------------------------------
// M2 variation / idle animation timing (milliseconds)
// ---------------------------------------------------------------------------
constexpr float M2_VARIATION_TIMER_MIN_MS      = 3000.0f;
constexpr float M2_VARIATION_TIMER_MAX_MS      = 11000.0f;
constexpr float M2_LOOP_VARIATION_TIMER_MIN_MS = 4000.0f;
constexpr float M2_LOOP_VARIATION_TIMER_MAX_MS = 10000.0f;
constexpr float M2_IDLE_VARIATION_TIMER_MIN_MS = 2000.0f;
constexpr float M2_IDLE_VARIATION_TIMER_MAX_MS = 6000.0f;
constexpr float M2_DEFAULT_PARTICLE_ANIM_MS    = 3333.0f;

// ---------------------------------------------------------------------------
// HiZ occlusion culling
// ---------------------------------------------------------------------------
// VP matrix diff threshold - below this HiZ is considered safe.
// Typical tracking camera (following a walking character) produces 0.05–0.25.
constexpr float HIZ_VP_DIFF_THRESHOLD = 0.5f;

// ---------------------------------------------------------------------------
// Smoke / spark particle tuning
// ---------------------------------------------------------------------------
constexpr float SMOKE_OFFSET_XY_MIN  = -0.4f;
constexpr float SMOKE_OFFSET_XY_MAX  =  0.4f;
constexpr float SMOKE_VEL_Z_MIN      =  3.0f;
constexpr float SMOKE_VEL_Z_MAX      =  5.0f;
constexpr float SMOKE_LIFETIME_MIN   =  4.0f;
constexpr float SMOKE_LIFETIME_MAX   =  7.0f;
constexpr float SMOKE_Z_VEL_DAMPING  =  0.98f;
constexpr float SMOKE_SIZE_START     =  1.0f;
constexpr float SMOKE_SIZE_GROWTH    =  2.5f;

constexpr int   SPARK_PROBABILITY_DENOM = 8;      // 1-in-8 chance per frame
constexpr float SPARK_LIFE_BASE         = 0.8f;
constexpr float SPARK_LIFE_RANGE        = 1.2f;

// ---------------------------------------------------------------------------
// Character rendering
// ---------------------------------------------------------------------------
// Default frustum-cull radius when model bounds are unavailable (world units).
// 4.0 covers Tauren, mounted characters, and most creature models.

} // namespace rendering
} // namespace wowee
