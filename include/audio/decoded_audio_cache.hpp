#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace wowee::audio {

// Holds a bounded amount of decoded PCM, independently of how long clips are.
// Entry owns pcmData through shared_ptr; evicting a cache entry cannot invalidate
// samples that an active miniaudio voice is still playing. Callers serialize access.
template <typename Entry>
class DecodedAudioCache {
public:
    explicit DecodedAudioCache(size_t budgetBytes, size_t maxEntries = 256)
        : budgetBytes_(budgetBytes), maxEntries_(maxEntries) {}

    bool find(uint64_t key, Entry& out) {
        auto it = entries_.find(key);
        if (it == entries_.end()) return false;
        it->second.lastUse = ++clock_;
        out = it->second.entry;
        return true;
    }

    void insert(uint64_t key, const Entry& entry) {
        if (!entry.pcmData) return;
        const size_t bytes = entry.pcmData->capacity();
        if (bytes > budgetBytes_ || maxEntries_ == 0) return;
        auto existing = entries_.find(key);
        if (existing != entries_.end()) {
            bytes_ -= existing->second.bytes;
            entries_.erase(existing);
        }
        while (!entries_.empty() &&
               (bytes_ > budgetBytes_ - bytes || entries_.size() >= maxEntries_)) {
            auto oldest = entries_.begin();
            for (auto it = entries_.begin(); it != entries_.end(); ++it) {
                if (it->second.lastUse < oldest->second.lastUse) oldest = it;
            }
            bytes_ -= oldest->second.bytes;
            entries_.erase(oldest);
        }
        entries_.emplace(key, Cached{entry, bytes, ++clock_});
        bytes_ += bytes;
    }

    void clear() { entries_.clear(); bytes_ = 0; clock_ = 0; }
    size_t bytes() const { return bytes_; }
    size_t size() const { return entries_.size(); }

private:
    struct Cached { Entry entry; size_t bytes; uint64_t lastUse; };
    std::unordered_map<uint64_t, Cached> entries_;
    size_t budgetBytes_;
    size_t maxEntries_;
    size_t bytes_ = 0;
    uint64_t clock_ = 0;
};

} // namespace wowee::audio
