#pragma once

namespace wowee::core {
// A session boundary must release capacity as well as elements. Preserve the
// allocator so a stateful allocator never receives another owner's storage.
// Call only after producers and all borrowers have stopped.
template<class Container>
void releaseCacheStorage(Container& cache) {
    Container empty(cache.get_allocator());
    cache.swap(empty);
}
} // namespace wowee::core
