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
// Poll wall time separately from simulation time. Only the day/night anchor
// moves: transport, cooldown, networking and save timers never see a wall jump.
class LocalWallClockFollower {
public:
    enum class Result { Idle, ReadFailed, Unchanged, Synchronized };
    template<class Reader>
    Result poll(bool authoritative, double monotonicSeconds, double simulationNow,
                LocalDayClock& clock, Reader&& readHours) {
        if (!authoritative || !std::isfinite(monotonicSeconds) || !std::isfinite(simulationNow))
            return Result::Idle;
        if (polled_ && monotonicSeconds >= lastPoll_ && monotonicSeconds - lastPoll_ < 1.0)
            return Result::Idle;
        polled_ = true; lastPoll_ = monotonicSeconds;
        float wallHours = 0;
        if (!readHours(wallHours) || !std::isfinite(wallHours) || wallHours < 0 || wallHours >= 24)
            return Result::ReadFailed;
        // Circular distance avoids treating normal midnight rollover as a jump.
        const double deltaSeconds = std::remainder(double(wallHours) - clock.hours(simulationNow), 24.0) * 3600.0;
        // RTC is second-granular. Keep smooth interpolation between samples and
        // tolerate quantization; apply actual clock/timezone changes next poll.
        if (synchronized_ && std::abs(deltaSeconds) <= 2.0) return Result::Unchanged;
        if (!clock.synchronize(wallHours, simulationNow)) return Result::ReadFailed;
        synchronized_ = true;
        return Result::Synchronized;
    }
private:
    bool polled_ = false, synchronized_ = false;
    double lastPoll_ = 0;
};
} // namespace wowee::game
