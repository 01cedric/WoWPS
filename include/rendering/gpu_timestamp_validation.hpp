#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

namespace wowee::rendering {

// Convert only a driver-calibrated counter. Modular subtraction handles one
// wrap at the advertised width. Reject an ambiguous half-range/backwards
// interval and very old samples; missing availability is handled by the caller.
inline bool gpuTimestampDeltaMs(uint64_t begin, uint64_t end, uint32_t validBits,
                                double periodNs, double& milliseconds) {
    if (validBits == 0 || validBits > 64 || !std::isfinite(periodNs) || periodNs <= 0.0)
        return false;
    const uint64_t mask = validBits == 64 ? std::numeric_limits<uint64_t>::max()
                                         : (uint64_t{1} << validBits) - 1;
    const uint64_t delta = (end - begin) & mask;
    if (delta > (mask >> 1)) return false;
    const double result = static_cast<double>(delta) * periodNs / 1.0e6;
    if (!std::isfinite(result) || result > 1000.0) return false;
    milliseconds = result;
    return true;
}

} // namespace wowee::rendering
