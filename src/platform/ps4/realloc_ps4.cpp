// PS4 realloc shrink guard for the bundled OpenOrbis libc.
//
// Its mapped-allocation path falls back from the unimplemented mremap to
// malloc(new_size), memcpy(old_usable_size), free(old). When new_size is smaller,
// that copies past the replacement allocation. Lua's compiler and collector
// both legitimately shrink buffers, including during original FrameXML load.
//
// Keep the native grow path, but handle shrinking before it can reach libc.
// A failed shrink allocation retains the original block: all requested bytes
// already fit, and Lua 5.1 requires shrinking allocations not to fail.
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>

extern "C" void* __real_realloc(void* pointer, size_t bytes);

extern "C" void* __wrap_realloc(void* pointer, size_t bytes) {
    if (!pointer) return malloc(bytes);
    if (!bytes) {
        free(pointer);
        return nullptr;
    }

    const size_t capacity = malloc_usable_size(pointer);
    if (bytes == capacity) return pointer;
    if (bytes < capacity) {
        const int previousErrno = errno;
        void* replacement = malloc(bytes);
        if (!replacement) {
            errno = previousErrno;
            return pointer;
        }
        memcpy(replacement, pointer, bytes);
        free(pointer);
        return replacement;
    }
    return __real_realloc(pointer, bytes);
}
