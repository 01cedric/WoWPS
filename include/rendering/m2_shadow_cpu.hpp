#pragma once
#include "rendering/m2_texture_transform.hpp"
#include <cstdint>
#include <cstring>

namespace wowee::rendering {
// Exact clock reuse scoped to one immutable model/material and one pass's lava clock.
// Keep the adjacent-key fast path and a bounded table for interleaved clocks.
// Collisions only evict reuse opportunities; every hit checks all key bits.
// Do not retain across frames: models and their animation tracks can change.
// Bitwise clock keys retain signed zeros and never quantize animation time.
class M2ShadowUvClockCache {
public:
    template<class Instance, class Sample>
    M2UvTransform sample(const Instance& instance, Sample&& compute) {
        uint32_t time, globalTime;
        static_assert(sizeof(time) == sizeof(instance.animTime));
        static_assert(sizeof(globalTime) == sizeof(instance.globalSequenceTime));
        std::memcpy(&time, &instance.animTime, sizeof(time));
        std::memcpy(&globalTime, &instance.globalSequenceTime, sizeof(globalTime));
        if (valid_ && sequence_ == instance.currentSequenceIndex &&
            time_ == time && globalTime_ == globalTime) {
            ++reused_;
            return uv_;
        }
        if (!tableEnabled_) {
            uv_ = compute();
            ++samples_;
        } else {
            // Mix float bit patterns: fractional clocks often share their low bits.
            uint32_t hash = time * 0x9e3779b1u;
            hash ^= globalTime + 0x85ebca6bu + (hash << 6) + (hash >> 2);
            hash ^= static_cast<uint32_t>(instance.currentSequenceIndex) * 0xc2b2ae35u;
            hash ^= hash >> 16;
            hash *= 0x7feb352du;
            hash ^= hash >> 15;
            const uint32_t slot = hash & 63u;
            const uint64_t bit = uint64_t{1} << slot;
            auto& entry = entries_[slot];
            if ((occupied_ & bit) && entry.sequence == instance.currentSequenceIndex &&
                entry.time == time && entry.globalTime == globalTime) {
                uv_.linear = {entry.uv[0], entry.uv[1], entry.uv[2], entry.uv[3]};
                uv_.offset = {entry.uv[4], entry.uv[5]};
                ++reused_;
                ++tableHits_;
            } else {
                uv_ = compute();
                entry.sequence = instance.currentSequenceIndex;
                entry.time = time;
                entry.globalTime = globalTime;
                for (unsigned i = 0; i < 4; ++i) entry.uv[i] = uv_.linear[i];
                for (unsigned i = 0; i < 2; ++i) entry.uv[4 + i] = uv_.offset[i];
                occupied_ |= bit;
                ++samples_;
            }
            // After 128 non-adjacent requests, stop paying hashing/storage costs
            // for effectively unique clocks. This only affects reuse, never values.
            if (++tableRequests_ == 128u && tableHits_ < 8u) tableEnabled_ = false;
        }
        sequence_ = instance.currentSequenceIndex;
        time_ = time;
        globalTime_ = globalTime;
        valid_ = true;
        return uv_;
    }
    uint32_t samples() const { return samples_; }
    uint32_t reused() const { return reused_; }
private:
    // Unoccupied entries are never read. Avoid clearing 64 UV transforms at
    // every material boundary; the mask alone establishes initialized slots.
    struct Entry { int sequence; uint32_t time, globalTime; float uv[6]; };
    Entry entries_[64];
    uint64_t occupied_ = 0;
    uint32_t tableRequests_ = 0, tableHits_ = 0;
    bool tableEnabled_ = true;
    M2UvTransform uv_{};
    int sequence_ = 0;
    uint32_t time_ = 0, globalTime_ = 0, samples_ = 0, reused_ = 0;
    bool valid_ = false;
};
} // namespace wowee::rendering
