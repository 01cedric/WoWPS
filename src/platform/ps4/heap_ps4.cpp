// B32: bounded flexible-memory arena growth for the supplied OpenOrbis libc.
// The native allocator owns block headers, free lists, coalescing and locks.
// Only its __expand_heap kernel-mapping backend is replaced. In particular,
// mapped large allocations and B31's bounded realloc shrink remain unchanged.
#include "platform/ps4/heap_growth.hpp"
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <sys/mman.h>
#include <sys/types.h>

extern "C" void* __mmap(void*, std::size_t, int, int, int, off_t);
namespace {
static_assert(std::atomic<std::size_t>::is_always_lock_free);
std::atomic<std::size_t> mappedBytes{0}, extensions{0}, largestMapping{0}, mappingFailures{0};
void recordMapping(std::size_t bytes) noexcept {
    mappedBytes.fetch_add(bytes, std::memory_order_relaxed);
    extensions.fetch_add(1, std::memory_order_relaxed);
    auto largest = largestMapping.load(std::memory_order_relaxed);
    while (largest < bytes && !largestMapping.compare_exchange_weak(
        largest, bytes, std::memory_order_relaxed)) {}
}
}
namespace wowee::platform::ps4 {
HeapGrowthStats heapGrowthStats() noexcept {
    return {mappedBytes.load(std::memory_order_relaxed),
            extensions.load(std::memory_order_relaxed),
            largestMapping.load(std::memory_order_relaxed),
            mappingFailures.load(std::memory_order_relaxed)};
}
}

extern "C" void* __wrap___expand_heap(std::size_t* requested) {
    using namespace wowee::platform::ps4;
    if (!requested) { errno = EINVAL; return nullptr; }
    // Match musl's signed-pointer-size bound before rounding to a PS4 page.
    constexpr auto limit = static_cast<std::size_t>(
        std::numeric_limits<std::ptrdiff_t>::max()) - kHeapPageBytes;
    if (*requested > limit) { errno = ENOMEM; return nullptr; }
    const std::size_t needed = (*requested + kHeapPageBytes - 1) & ~(kHeapPageBytes - 1);
    const std::size_t preferred = needed > kHeapGrowthReserveBytes
        ? needed : kHeapGrowthReserveBytes;
    const int savedErrno = errno;
    void* block = __mmap(nullptr, preferred, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    std::size_t mapped = preferred;
    if (block == MAP_FAILED) {
        mappingFailures.fetch_add(1, std::memory_order_relaxed);
        // A failure to reserve slack does not mean the actual request cannot
        // fit. Try exactly the rounded request, but never retry size zero.
        if (!needed || needed == preferred) return nullptr;
        block = __mmap(nullptr, needed, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (block == MAP_FAILED) {
            mappingFailures.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        mapped = needed;
    }
    *requested = mapped; // Native malloc must know the entire mapping size.
    recordMapping(mapped);
    errno = savedErrno;
    return block;
}
