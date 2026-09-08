#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace wowee::rendering {
// Constant storage, real elapsed time (never simulation's clamped delta).
// Counts loop iterations; discarded GPU frames are reported separately.
struct FrameTimingWindow {
    uint32_t frames = 0, over40ms = 0;
    double elapsed = 0, updateMs = 0, renderMs = 0, maxFrameMs = 0;
    void add(double seconds, double update, double render) {
        if (!std::isfinite(seconds) || seconds <= 0) return;
        ++frames;
        elapsed += seconds;
        updateMs += update;
        renderMs += render;
        const double ms = seconds * 1000.0;
        maxFrameMs = std::max(maxFrameMs, ms);
        if (ms > 40.0) ++over40ms;
    }
    void addFrame(std::chrono::steady_clock::time_point start,
                  std::chrono::steady_clock::time_point updateStart,
                  std::chrono::steady_clock::time_point renderStart,
                  std::chrono::steady_clock::time_point renderEnd,
                  std::chrono::steady_clock::time_point end) {
        // Measure this iteration, including its pacing. The simulation delta
        // belongs to the preceding iteration and can include the world load.
        add(std::chrono::duration<double>(end - start).count(),
            std::chrono::duration<double, std::milli>(renderStart - updateStart).count(),
            std::chrono::duration<double, std::milli>(renderEnd - renderStart).count());
    }
    bool ready() const { return elapsed >= 5.0 && frames > 0; }
    double fps() const { return elapsed > 0 ? frames / elapsed : 0; }
    void reset() { *this = {}; }
};
} // namespace wowee::rendering
