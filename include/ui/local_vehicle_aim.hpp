#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace wowee::ui {

// One seat's local edit buffer. A published matching aim releases the next
// coalesced edit; idle frames never manufacture network commands.
struct LocalVehicleAimInput {
    uint64_t owner = 0, vehicle = 0;
    uint8_t seat = 0;
    float yaw = 0, pitch = 0, sentYaw = 0, sentPitch = 0;
    double sentAt = 0, nextSendAt = 0;
    bool pending = false, dirty = false;

    static bool same(float a, float b) { return std::abs(a-b) < .001f; }
    void observe(uint64_t who, uint64_t hull, uint8_t at, float confirmedYaw,
                 float confirmedPitch, double now) {
        if (!hull || owner != who || vehicle != hull || seat != at || now < sentAt) {
            *this = {};
            owner = who; vehicle = hull; seat = at;
            yaw = confirmedYaw; pitch = confirmedPitch;
            return;
        }
        if (pending && same(confirmedYaw,sentYaw) && same(confirmedPitch,sentPitch))
            pending = false;
        else if (pending && now-sentAt > 2.0) {
            // A rejected/stale command must not keep an unconfirmed preview
            // alive, or automatically retry it after a seat handoff.
            pending = dirty = false;
        }
        if (!pending && !dirty) { yaw = confirmedYaw; pitch = confirmedPitch; }
    }
    void edit(float nextYaw, float nextPitch, float minPitch, float maxPitch) {
        constexpr float pi = 3.14159265358979323846f;
        if (!std::isfinite(nextYaw) || !std::isfinite(nextPitch)) return;
        nextYaw = std::remainder(nextYaw,2*pi);
        nextPitch = std::clamp(nextPitch,minPitch,maxPitch);
        if (same(yaw,nextYaw) && same(pitch,nextPitch)) return;
        yaw = nextYaw; pitch = nextPitch; dirty = true;
    }
    bool ready(double now) const { return vehicle && dirty && !pending && now >= nextSendAt; }
    void submitted(double now, bool accepted) {
        nextSendAt = now+.25;
        if (!accepted) return;
        sentYaw = yaw; sentPitch = pitch; sentAt = now;
        dirty = false; pending = true;
    }
    bool settled() const { return !pending && !dirty; }
};

} // namespace wowee::ui
