#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <orbis/libkernel.h>

// The budgets, and the decisions taken from them. They moved into their own
// header because this one cannot be compiled anywhere but the console - the
// include above is the whole reason - and a memory policy that no test can
// execute is a policy nobody ever checks. Every existing user of
// kCpuMemoryBudgetLimit and its neighbours keeps including this file and keeps
// seeing them.
#include "platform/ps4/cpu_budget.hpp"

namespace wowee::platform::ps4 {

struct AvailableCpuMemory {
    size_t bytes = 0;
    int32_t status = 0;
    bool measured = false;
};

inline AvailableCpuMemory queryAvailableCpuMemory() {
    static_assert(sizeof(size_t) == sizeof(void*) && sizeof(size_t) == 8,
                  "PS4 flexible-memory output is a 64-bit size/pointer");
    // OpenOrbis declares the argument as size_t, but it contains the address of
    // a uint64_t output, not a requested size. The pointer-output ABI is also
    // implemented by shadPS4's src/core/libraries/kernel/memory.cpp; see the
    // retained source reference in validation/b10-flexible-memory-abi-source.json.
    // Pass its bits through the SDK declaration, avoiding a mismatched function
    // pointer cast. The sentinel detects a successful call that did not write.
    size_t available = static_cast<size_t>(-1);
    const int32_t status = sceKernelAvailableFlexibleMemorySize(
        reinterpret_cast<size_t>(&available));
    if (status != 0 || available > 8ull * 1024 * 1024 * 1024) {
        return {0, status, false};
    }
    // This measures currently unmapped flexible pages. Reusable blocks held
    // inside libc's malloc arenas are not added: this is conservative headroom.
    return {available < kCpuMemoryBudgetLimit ? available : kCpuMemoryBudgetLimit,
            status, true};
}

/// The largest of `sizes` the C++ heap will actually hand back right now.
///
/// Asked by asking. This toolchain's libc offers no arena statistics at all -
/// its <malloc.h> declares malloc, calloc, realloc, free, valloc, memalign and
/// malloc_usable_size, and nothing else; there is no mallinfo to call and no
/// free-list to walk. The alternative to a probe is therefore not a better
/// measurement, it is the flexible-page residue again, which is what put a
/// headroom gate in front of an allocation it does not measure.
///
/// The probe is cheap and it is clean. Every size here is far above musl's
/// mmap threshold, so malloc serves it by mapping the block whole and free
/// unmaps it: the arena is not grown, nothing is left in a bin, and the answer
/// is about the same mechanism that fails - one contiguous mapping of exactly
/// this many bytes, or nullptr.
///
/// Both ends are written before the block is released. Flexible memory is
/// committed at map time on this platform, so a returned pointer is already
/// backed; the writes cost two page faults and remove any question of a lazy
/// mapping answering yes on memory that is not there. They are volatile
/// because an untouched malloc/free pair is a thing a compiler may delete.
inline std::size_t probeLargestHeapBlock(const std::size_t* sizes, std::size_t count) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        if (sizes[i] == 0) continue;
        void* block = std::malloc(sizes[i]);
        if (!block) continue;
        volatile unsigned char* bytes = static_cast<volatile unsigned char*>(block);
        bytes[0] = 0;
        bytes[sizes[i] - 1] = 0;
        std::free(block);
        return sizes[i];
    }
    return 0;
}

/// What a character skin composite may cost right now.
///
/// One probe in the common case: 4 MiB is what a 1024 atlas needs in one piece,
/// and a console with room says yes to it immediately. The ladder below only
/// runs when that fails, which is exactly when the answer matters.
inline CompositeAdmission probeCompositeAdmission() noexcept {
    const std::size_t ladder[] = {compositeBytesFor(kCompositeEdgeFull),
                                  compositeBytesFor(kCompositeEdgeHalf),
                                  compositeBytesFor(kCompositeEdgeMin)};
    const std::size_t largest =
        probeLargestHeapBlock(ladder, sizeof(ladder) / sizeof(ladder[0]));
    const auto free = queryAvailableCpuMemory();
    return compositeAdmissionFor(largest, free.bytes, free.measured);
}

} // namespace wowee::platform::ps4
