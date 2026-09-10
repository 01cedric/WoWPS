#pragma once
#include <cstddef>
#include <cstdlib>

namespace wowee::addons {
// Bound the Lua VM's live allocations, not the kernel's unmapped page count.
// Lua supplies allocation sizes; ptr==nullptr uses osize as a type tag.
struct LuaMemoryBudget {
    size_t limit = 64ull * 1024 * 1024;
    size_t used = 0, peak = 0, failures = 0;
    static void* allocate(void* context, void* ptr, size_t oldSize, size_t newSize) noexcept {
        auto& budget = *static_cast<LuaMemoryBudget*>(context);
        if (!ptr) oldSize = 0;
        if (newSize == 0) {
            std::free(ptr);
            budget.used -= oldSize;
            return nullptr;
        }
        const size_t retained = budget.used - oldSize;
        // Lua 5.1 assumes that shrinking cannot fail. This also allows the VM
        // to recover when its configured limit is below current live usage.
        // If realloc cannot shrink physically, retain the original block;
        // accounting follows Lua's requested sizes, not allocator capacity.
        if (ptr && newSize <= oldSize) {
            void* replacement = std::realloc(ptr, newSize);
            budget.used = retained + newSize;
            return replacement ? replacement : ptr;
        }
        if (retained > budget.limit || newSize > budget.limit - retained) {
            ++budget.failures;
            return nullptr;
        }
        void* replacement = std::realloc(ptr, newSize);
        if (!replacement) { ++budget.failures; return nullptr; }
        budget.used = retained + newSize;
        if (budget.used > budget.peak) budget.peak = budget.used;
        return replacement;
    }
};
}
