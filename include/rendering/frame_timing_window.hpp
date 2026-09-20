#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace wowee::rendering {
// Constant storage, real elapsed time (never simulation's clamped delta).
// Counts loop iterations; discarded GPU frames are reported separately.
struct FrameTimingWindow {
    uint32_t frames = 0, over40ms = 0;
    // A 40 FPS target permits 25 ms per complete loop iteration.
    uint32_t over25ms = 0, updateOver25ms = 0, renderOver25ms = 0;
    double otherAndPacingMs = 0;
    double elapsed = 0, updateMs = 0, renderMs = 0, maxFrameMs = 0;
    // Exact nearest-rank percentiles for a bounded five-second window. 2048
    // covers the normal 240-Hz cap without allocating in the render loop.
    static constexpr size_t capacity = 2048;
    std::array<float, capacity> frameMs{};
    size_t sampleCount = 0;
    void add(double seconds, double update, double render) {
        if (!std::isfinite(seconds) || seconds <= 0) return;
        ++frames;
        elapsed += seconds;
        updateMs += update;
        renderMs += render;
        const double ms = seconds * 1000.0;
        if (ms > 25.0) ++over25ms;
        if (update > 25.0) ++updateOver25ms;
        if (render > 25.0) ++renderOver25ms;
        // Event handling and pacing are outside update/render. This residual
        // is CPU wall time, not a GPU duration or an extra independent pass.
        otherAndPacingMs += std::max(0.0, ms - update - render);
        if (sampleCount < capacity) frameMs[sampleCount++] = static_cast<float>(ms);
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
    double percentile(double fraction) const {
        if (!sampleCount) return 0;
        if (!std::isfinite(fraction)) return 0;
        auto sorted = frameMs;
        const size_t rank = static_cast<size_t>(std::ceil(std::clamp(fraction, 0.0, 1.0) * sampleCount));
        const size_t index = rank > 0 ? rank - 1 : 0;
        std::nth_element(sorted.begin(), sorted.begin() + index, sorted.begin() + sampleCount);
        return sorted[index];
    }
    void reset() { *this = {}; }
};
} // namespace wowee::rendering
