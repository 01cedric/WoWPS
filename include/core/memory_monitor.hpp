#pragma once

#include <cstddef>
#include <cstdint>

namespace wowee {
namespace core {

/**
 * Monitors system memory and provides dynamic cache sizing
 */
class MemoryMonitor {
public:
    static MemoryMonitor& getInstance();

    /**
     * Initialize memory monitoring
     */
    void initialize();

    /**
     * Get total system RAM in bytes (PS4: CPU flexible-memory budgeting ceiling)
     */
    [[nodiscard]] size_t getTotalRAM() const { return totalRAM_; }

    /**
     * Get currently available RAM in bytes. PS4 reports free flexible pages,
     * capped at the CPU ceiling, and returns zero if measurement fails.
     */
    [[nodiscard]] size_t getAvailableRAM() const;

    /**
     * Get recommended cache budget (desktop: 50%, capped at 16 GiB;
     * PS4: 25% of free flexible pages, capped at 32 MiB).
     */
    [[nodiscard]] size_t getRecommendedCacheBudget() const;

    /**
     * Check if system is under memory pressure (< 10% desktop RAM;
     * PS4: measured flexible headroom below 32 MiB).
     */
    [[nodiscard]] bool isMemoryPressure() const;

    /**
     * Check if system is under severe memory pressure (< 15% desktop RAM;
     * PS4: measured flexible headroom below 16 MiB).
     * At this level, background loading should pause entirely until memory
     * is freed - continuing to allocate risks OOM-killing other applications.
     */
    [[nodiscard]] bool isSevereMemoryPressure() const;

private:
    MemoryMonitor() = default;
    size_t totalRAM_ = 0;
};

} // namespace core
} // namespace wowee
