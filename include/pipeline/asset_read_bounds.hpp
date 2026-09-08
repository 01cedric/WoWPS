#pragma once

#include "core/logger.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace wowee::pipeline {

// Rejecting an oversized asset is expected to remain cheap even when callers
// probe it repeatedly. Keep diagnostics bounded without retaining asset names.
inline void logAssetReadLimit(const char* source, const std::string& path,
                              uint64_t bytes, size_t maxBytes) {
    static std::atomic<uint64_t> reports{0};
    const auto report = reports.fetch_add(1, std::memory_order_relaxed);
    if (report < 16) {
        LOG_WARNING(source, ": bounded read rejected ", std::string_view(path).substr(0, 192),
                    " (", bytes, " bytes; limit ", maxBytes, ")");
    } else if (report == 16) {
        LOG_WARNING("Further oversized-asset read diagnostics suppressed");
    }
}

} // namespace wowee::pipeline
