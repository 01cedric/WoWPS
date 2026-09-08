#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>

// What an M2 particle frame is allowed to cost.
//
// Every number here used to be one constant, MAX_M2_PARTICLES, doing three
// unrelated jobs: the per-instance emission cap, the vertex buffer's capacity,
// and the size of a draw chunk. That reads as a budget and is not one. It is a
// *per instance* cap, so the frame's actual cost is that number times however
// many emitters happen to be in range - and a city log shows m2Instances=16382.
// A few hundred of those with emitters is enough for the live population, the
// vertex data built from it, and the time spent walking both to grow without a
// ceiling, on a console that has already died of std::bad_alloc.
//
// So the frame gets a budget, the budget is shared out, and the vertex buffer
// admits what fits and stops. Under memory pressure the budget shrinks rather
// than the effect disappearing: half a fire still reads as a fire, and a fire
// that costs nothing when memory is gone is worth more than one that is always
// full.
namespace wowee::rendering::m2particles {

// Nine floats per point sprite: position, colour, alpha, scale, tile index.
inline constexpr std::size_t kFloatsPerParticle = 9;

// The console runs ~20 FPS with a small flexible heap, so it gets a fraction
// of the desktop budget. Both are vertex-data figures first: 6000 particles is
// 216 KiB of writes per frame, 24000 is 864 KiB.
inline constexpr std::size_t kConsoleFrameBudget = 6000;
inline constexpr std::size_t kDesktopFrameBudget = 24000;

// No instance may hold more than this however few emitters are in range. The
// old MAX_M2_PARTICLES, kept as the ceiling it always was.
inline constexpr std::size_t kMaxParticlesPerInstance = 4000;

// ...and no fewer than this while it is in range at all. Below roughly this
// many live particles a flame stops reading as a flame - the same threshold
// emitParticles' rate floor exists to defend - so an instance that survives
// the distance cull keeps enough to be worth drawing, and the frame stays
// bounded by culling emitters rather than by starving all of them.
inline constexpr std::size_t kMinParticlesPerInstance = 16;

/// The frame's particle budget after memory pressure is taken into account.
/// Severe pressure is the state that preceded the std::bad_alloc crashes, so
/// it takes the budget down hard rather than nudging it.
[[nodiscard]] inline constexpr std::size_t frameBudget(std::size_t base,
                                                       bool pressure,
                                                       bool severePressure) noexcept {
    if (severePressure) return base / 8u;
    if (pressure) return base / 2u;
    return base;
}

/// How many live particles one emitting instance may hold this frame.
///
/// An even share, floored so a lone survivor of the cull still looks right and
/// capped so a single emitter cannot claim a budget sized for a whole scene.
/// With the floor in play the total can exceed the budget; that is deliberate
/// and bounded - the draw path admits only what the vertex buffer holds, and
/// the simulation cost is then linear in the number of emitters, which is what
/// the distance cull bounds.
[[nodiscard]] inline constexpr std::size_t instanceShare(
    std::size_t budget, std::size_t emittingInstances) noexcept {
    if (emittingInstances == 0) return 0;
    const std::size_t even = budget / emittingInstances;
    if (even < kMinParticlesPerInstance) return kMinParticlesPerInstance;
    if (even > kMaxParticlesPerInstance) return kMaxParticlesPerInstance;
    return even;
}

/// Particle simulation range. Particles are small, additive and screen-space
/// sub-pixel long before a doodad's own geometry stops being drawn, so they
/// stop earlier than the model does - and the more emitters are in range, the
/// earlier, because the per-instance share is what pays for the extra ones.
///
/// The count fed in is the previous frame's, which is what makes this cheap:
/// no pre-pass, and a scene that gets busier tightens over a frame rather than
/// all at once. It never widens past the caller's own limit.
[[nodiscard]] inline constexpr float emitterCullDistSq(
    float modelMaxDistSq, std::size_t emittersLastFrame,
    std::size_t softLimit) noexcept {
    if (softLimit == 0 || emittersLastFrame <= softLimit) return modelMaxDistSq;
    // Squared distance scales with area, so halving the admitted count wants a
    // half-area circle - the ratio applies directly to the squared radius.
    const float ratio = static_cast<float>(softLimit) /
                        static_cast<float>(emittersLastFrame);
    // A floor, so a dense city never culls the campfire the player stands in.
    const float floorSq = 30.0f * 30.0f;
    const float scaled = modelMaxDistSq * ratio;
    return scaled < floorSq ? std::min(modelMaxDistSq, floorSq) : scaled;
}

/// Smooth the emitter count the cull is driven by.
///
/// Without this the cull is a one-frame feedback loop and oscillates: a busy
/// frame tightens the radius, the tighter radius admits fewer emitters, the
/// smaller count relaxes the radius, and the fires at the boundary blink in
/// and out at frame rate. An exponential average over roughly a second of
/// console frames damps that to a drift the eye does not catch, and it settles
/// on the true count when the scene stops changing.
///
/// Integer, and always at least one, so a small difference converges instead
/// of stalling forever on a truncated eighth.
[[nodiscard]] inline constexpr std::size_t smoothEmitterCount(
    std::size_t previous, std::size_t observed) noexcept {
    if (observed == previous) return previous;
    const std::size_t difference = observed > previous ? observed - previous
                                                       : previous - observed;
    const std::size_t step = difference < 8u ? 1u : difference / 8u;
    return observed > previous ? previous + step : previous - step;
}

/// Vertices one frame may write into the shared particle vertex buffer.
/// Both frames in flight own a disjoint half: the buffer is persistently
/// mapped and written at record time, so a single region would let frame N+1's
/// memcpy overwrite what frame N's queued draws have not read yet.
[[nodiscard]] inline constexpr std::size_t frameVertexCapacity(
    std::size_t bufferVertices, std::size_t framesInFlight) noexcept {
    if (framesInFlight == 0) return bufferVertices;
    return bufferVertices / framesInFlight;
}

/// First vertex of a frame's region.
[[nodiscard]] inline constexpr std::size_t frameVertexBase(
    std::size_t frameIndex, std::size_t framesInFlight,
    std::size_t bufferVertices) noexcept {
    if (framesInFlight == 0) return 0;
    return (frameIndex % framesInFlight) *
           frameVertexCapacity(bufferVertices, framesInFlight);
}

/// How many of a group's particles fit in what is left of the frame's region.
///
/// The draw path used to memcpy every group to offset zero and draw every one
/// of them from vertex zero. All the draws execute after recording ends, so
/// every group rendered whatever the last group's memcpy had left there - the
/// bug is invisible with one group and unmissable with two, which is why it
/// survived behind a switch that kept the whole path off.
[[nodiscard]] inline constexpr std::size_t admitVertices(
    std::size_t written, std::size_t want, std::size_t capacity) noexcept {
    if (written >= capacity) return 0;
    const std::size_t room = capacity - written;
    return want < room ? want : room;
}

} // namespace wowee::rendering::m2particles
