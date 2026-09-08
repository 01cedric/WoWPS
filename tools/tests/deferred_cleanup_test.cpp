#include "rendering/deferred_cleanup.hpp"

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <new>

namespace {
long allocationsUntilFailure = -1;
}

void* operator new(std::size_t bytes) {
    if (allocationsUntilFailure == 0) throw std::bad_alloc();
    if (allocationsUntilFailure > 0) --allocationsUntilFailure;
    if (void* ptr = std::malloc(bytes ? bytes : 1)) return ptr;
    throw std::bad_alloc();
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }

int main() {
    unsigned failed = 0, succeeded = 0;
    // Fail each allocation in turn, including the second frame's callback
    // and vector growth. Failure must leave *both* ownership queues unchanged.
    for (long failAt = 0; failAt < 20; ++failAt) {
        std::vector<std::function<void()>> queues[2];
        unsigned completed = 0;
        bool callerOwns = true;
        std::function<void()> release = [&] { ++completed; };
        allocationsUntilFailure = failAt;
        bool success = false;
        try {
            wowee::rendering::enqueueAfterAllFences(queues, std::move(release));
            callerOwns = false;
            success = true;
        } catch (const std::bad_alloc&) {
            ++failed;
        }
        allocationsUntilFailure = -1;
        if (!success) {
            assert(callerOwns && queues[0].empty() && queues[1].empty());
            assert(completed == 0);
            // The caller can retry with its retained GPU handles.
            wowee::rendering::enqueueAfterAllFences(queues, [&] { ++completed; });
            callerOwns = false;
        } else ++succeeded;
        assert(!callerOwns && queues[0].size() == 1 && queues[1].size() == 1);
        queues[1][0]();
        assert(completed == 0); // one frame completing is insufficient
        queues[0][0]();
        assert(completed == 1);
    }
    assert(failed >= 3 && succeeded > 0);
    std::vector<std::function<void()>> queues[2];
    unsigned completed = 0;
    for (unsigned i = 0; i < 10000; ++i)
        wowee::rendering::enqueueAfterAllFences(queues, [&] { ++completed; });
    for (auto& release : queues[0]) release();
    assert(completed == 0);
    for (auto& release : queues[1]) release();
    assert(completed == 10000);
    std::printf("PASS transactional GPU retirement: %u injected allocation failures, 10000 two-fence retirements\n", failed);
}
