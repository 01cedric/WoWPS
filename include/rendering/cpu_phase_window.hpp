#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace wowee::rendering {

// Fixed-size CPU wall-time aggregation; never a GPU timer. Callers submit
// complete successful samples only and reset after publishing a full window.
template <std::size_t Phases, uint32_t Window = 120>
struct CpuPhaseWindow {
    static_assert(Phases > 0 && Window > 0, "non-empty profiling window required");
    std::array<uint64_t, Phases> sumUs{};
    std::array<uint64_t, Phases> maxUs{};
    uint32_t samples = 0;

    bool add(const std::array<uint64_t, Phases>& elapsedUs) noexcept {
        for (std::size_t i = 0; i < Phases; ++i) {
            sumUs[i] += elapsedUs[i];
            if (elapsedUs[i] > maxUs[i]) maxUs[i] = elapsedUs[i];
        }
        return ++samples == Window;
    }

    uint64_t meanUs(std::size_t phase) const noexcept {
        return samples ? sumUs[phase] / samples : 0;
    }

    void reset() noexcept { *this = {}; }
};

} // namespace wowee::rendering
