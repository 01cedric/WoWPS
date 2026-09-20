#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace wowee::core {
class IntroStreamWarmup {
public:
    void reset() { shot_ = std::numeric_limits<size_t>::max(); readySeconds_ = 0; frames_ = 0; warmed_ = false; }
    bool ready(size_t shot, bool cameraTileReady, bool uploadsPending, float elapsed) {
        if (shot != shot_) { reset(); shot_ = shot; }
        if (!cameraTileReady) { readySeconds_ = 0; frames_ = 0; warmed_ = false; return false; }
        // Once this shot has presented a complete scene, unrelated neighbour
        // uploads must not re-arm the initial warmup and black out playback.
        // A real required-scene loss still resets the gate above.
        if (warmed_) return true;
        if (std::isfinite(elapsed) && elapsed > 0) readySeconds_ += std::min(elapsed, 1.0f);
        ++frames_;
        // Give queued GPU/model work at least three frames before narration.
        // Neighbour tiles can be absent or continually streaming: they must not
        // prevent playback indefinitely. The camera tile itself is mandatory.
        warmed_ = frames_ >= 3 && (!uploadsPending || readySeconds_ >= 3.0f);
        return warmed_;
    }
private:
    size_t shot_ = std::numeric_limits<size_t>::max();
    float readySeconds_ = 0;
    unsigned frames_ = 0;
    bool warmed_ = false;
};
} // namespace wowee::core
