#pragma once
#include <cmath>
namespace wowee::game {
// Double precision anchor: elapsed real seconds advance a 24-hour local day.
class LocalDayClock {
public:
    bool synchronize(float hours, double now) {
        if (!std::isfinite(hours) || hours < 0 || hours >= 24 || !std::isfinite(now)) return false;
        hour_ = hours; anchor_ = now; return true;
    }
    float hours(double now) const {
        if (!std::isfinite(now)) return float(hour_);
        const double elapsed = now > anchor_ ? now-anchor_ : 0;
        return float(std::fmod(hour_ + elapsed / 3600.0, 24.0));
    }
private:
    double hour_ = 12, anchor_ = 0;
};
} // namespace wowee::game
