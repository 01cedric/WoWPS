#pragma once
#include <chrono>

namespace wowee::core {
// Frame-thread gate: temporary streaming gaps are not failed logins. Deadlines
// use monotonic wall time, not clamped simulation dt, and never extend because
// a worker remains pending. A failed initial load still rejects immediately.
class LocalWorldEntryGate {
public:
    using Clock = std::chrono::steady_clock;
    enum class Result { Ready, Waiting, Failed };
    void reset() { *this = {}; }
    void beginRelocation() { graceAllowed_ = true; waiting_ = false; }
    bool waiting() const { return waiting_; }
    Result update(bool avatarReady, bool geometryReady, bool streamingPending,
                  Clock::time_point now = Clock::now()) {
        if (avatarReady && geometryReady) {
            graceAllowed_ = true;
            waiting_ = false;
            return Result::Ready;
        }
        if (!graceAllowed_ && !streamingPending) return Result::Failed;
        if (!waiting_) { waiting_ = true; missingSince_ = now; }
        const auto limit = streamingPending ? std::chrono::seconds(60) : std::chrono::seconds(5);
        return now - missingSince_ >= limit ? Result::Failed : Result::Waiting;
    }
private:
    bool graceAllowed_ = false;
    bool waiting_ = false;
    Clock::time_point missingSince_{};
};
}
