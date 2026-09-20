#pragma once
#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>
#include <vector>

namespace wowee::platform {
// City geometry is CPU read/write data, not a GPU resource. On PS4 large
// buffers use a bounded write-back Onion allocation instead of exhausting the
// much smaller flexible heap shared by Lua, gameplay and asset decoding.
inline constexpr size_t kCpuGeometryThreshold = 16 * 1024;
inline constexpr size_t kCpuGeometryLimit = 128 * 1024 * 1024;
struct CpuGeometryStats { size_t mappedBytes, peakBytes, allocationFailures; };
#ifdef WOWEE_PS4
void* allocateCpuGeometry(size_t bytes);
void freeCpuGeometry(void* pointer, size_t bytes) noexcept;
CpuGeometryStats cpuGeometryStats() noexcept;
template<class T> struct CpuGeometryAllocator {
    using value_type = T;
    using is_always_equal = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    CpuGeometryAllocator() noexcept = default;
    template<class U> CpuGeometryAllocator(const CpuGeometryAllocator<U>&) noexcept {}
    T* allocate(size_t count) {
        static_assert(alignof(T) <= alignof(std::max_align_t));
        if (count > std::numeric_limits<size_t>::max() / sizeof(T))
            throw std::bad_array_new_length();
        return static_cast<T*>(allocateCpuGeometry(count * sizeof(T)));
    }
    void deallocate(T* pointer, size_t count) noexcept {
        freeCpuGeometry(pointer, count * sizeof(T));
    }
    template<class U> bool operator==(const CpuGeometryAllocator<U>&) const noexcept { return true; }
    template<class U> bool operator!=(const CpuGeometryAllocator<U>&) const noexcept { return false; }
};
template<class T> using CpuGeometryVector = std::vector<T, CpuGeometryAllocator<T>>;
#else
template<class T> using CpuGeometryVector = std::vector<T>;
inline CpuGeometryStats cpuGeometryStats() noexcept { return {}; }
#endif
} // namespace wowee::platform
