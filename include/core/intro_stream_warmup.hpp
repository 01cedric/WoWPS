#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace wowee::core {
class IntroStreamWarmup {
public:
    void reset() { shot_ = std::numeric_limits<size_t>::max(); readySeconds_ = 0; frames_ = 0; }
    bool ready(size_t shot, bool cameraTileReady, bool uploadsPending, float elapsed) {
        if (shot != shot_) { reset(); shot_ = shot; }
        if (!cameraTileReady) { readySeconds_ = 0; frames_ = 0; return false; }
        if (std::isfinite(elapsed) && elapsed > 0) readySeconds_ += std::min(elapsed, 1.0f);
        ++frames_;
        // Give queued GPU/model work at least three frames before narration.
        // Neighbour tiles can be absent or continually streaming: they must not
        // prevent playback indefinitely. The camera tile itself is mandatory.
        return frames_ >= 3 && (!uploadsPending || readySeconds_ >= 3.0f);
    }
private:
    size_t shot_ = std::numeric_limits<size_t>::max();
    float readySeconds_ = 0;
    unsigned frames_ = 0;
};
} // namespace wowee::core
