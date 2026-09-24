#pragma once

namespace wowee::rendering {

// Animated instances are advanced in two passes: the common texture clock
// first, then the per-instance speed correction.  A zero speed is a hard
// freeze, so neither pass may touch the sampled sequence time.  Keeping this
// policy in one small header also makes the end-pose contract testable without
// constructing a Vulkan renderer.
inline bool m2AnimationClockRuns(float speed) noexcept { return speed != 0.0f; }

inline void m2AdvanceAnimationBase(float& time, float elapsedMs, float speed) noexcept {
    if (m2AnimationClockRuns(speed)) time += elapsedMs;
}

inline void m2ApplyAnimationSpeed(float& time, float elapsedMs, float speed) noexcept {
    if (m2AnimationClockRuns(speed)) time += elapsedMs * (speed - 1.0f);
}

} // namespace wowee::rendering
