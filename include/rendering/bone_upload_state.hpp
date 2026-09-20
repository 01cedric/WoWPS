#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wowee::rendering {

// Each frame slot owns an independent buffer. A pose change dirties both;
// consuming one slot must not make the other slot appear up to date.
class BoneUploadState {
public:
    void invalidate() { dirtyMask_ = 3u; }
    void invalidateSlot(uint32_t slot) {
        if (slot < 2u) dirtyMask_ |= (1u << slot);
    }

    // Call only after the frame-slot fence, as with any mapped SSBO write.
    // Failed/missing mappings leave the pose pending for the next attempt.
    bool upload(uint32_t slot, void* destination, const void* source, size_t bytes) {
        if (slot >= 2u || !(dirtyMask_ & (1u << slot)) ||
            !destination || !source || bytes == 0) return false;
        std::memcpy(destination, source, bytes);
        dirtyMask_ &= ~(1u << slot);
        return true;
    }

private:
    uint32_t dirtyMask_ = 3u;
};

} // namespace wowee::rendering
