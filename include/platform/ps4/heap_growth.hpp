#pragma once
#include <cstddef>
#include <cstdint>

namespace wowee::platform::ps4 {
// OpenOrbis musl's fallback doubles the minimum mmap size every two heap
// extensions. With a bounded flexible-memory pool this eventually reserves
// 64/128 MiB for an individual small allocation. Keep the allocator's bins and
// locking; bound only the unused reserve requested from the kernel.
inline constexpr std::size_t kHeapPageBytes = 16 * 1024;
inline constexpr std::size_t kHeapGrowthReserveBytes = 2 * 1024 * 1024;
struct HeapGrowthStats {
    std::size_t mappedBytes;   // Cumulative arena mappings, NOT live allocations.
    std::size_t extensions;
    std::size_t largestMapping;
    std::size_t mappingFailures;
};
HeapGrowthStats heapGrowthStats() noexcept;
} // namespace wowee::platform::ps4
