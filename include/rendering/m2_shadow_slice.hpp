#pragma once
#include <cstddef>
#include <cstdint>

namespace wowee::rendering {
// Descriptor covers the entire immutable buffer. Push offsets select a pass;
// firstInstance must remain zero in indexed instanced draws.
struct M2ShadowSlice {
    static constexpr uint32_t capacity = 32768;
    static constexpr uint32_t passes = 2;
    static constexpr size_t matrixBytes = 64;
    static constexpr size_t bufferBytes = size_t(capacity) * passes * matrixBytes;
    uint32_t matrixOffset = 0;
    size_t byteOffset = 0;
    size_t byteSize = 0;
    bool valid = false;
};
inline M2ShadowSlice m2ShadowSlice(uint32_t pass, size_t count) {
    if (pass >= M2ShadowSlice::passes || count > M2ShadowSlice::capacity) return {};
    const uint32_t base = pass * M2ShadowSlice::capacity;
    return {base, size_t(base) * M2ShadowSlice::matrixBytes,
            count * M2ShadowSlice::matrixBytes, true};
}
}
