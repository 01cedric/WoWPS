#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace wowee::rendering {

// Register ownership with every frame fence atomically. If any allocation
// fails, no callback is installed and the caller still owns its handles.
// Sequential emplace_back used to leave only a subset of fences registered
// after bad_alloc, leaking resources or invalidating later cleanup attempts.
template <size_t Frames>
void enqueueAfterAllFences(std::vector<std::function<void()>> (&queues)[Frames],
                           std::function<void()>&& task) {
    static_assert(Frames > 0);
    struct Completion {
        size_t remaining;
        std::function<void()> task;
    };
    auto completion = std::make_shared<Completion>(Completion{Frames, std::move(task)});
    std::array<std::function<void()>, Frames> callbacks;
    for (auto& callback : callbacks) {
        callback = [completion]() {
            if (--completion->remaining == 0) completion->task();
        };
    }
    // All allocations precede the commit. Moving std::function into already
    // reserved storage cannot throw, including on the console's libc++.
    for (auto& queue : queues) {
        if (queue.size() != queue.capacity()) continue;
        const size_t capacity = queue.capacity();
        const size_t next = capacity == 0 ? 8 :
            (capacity <= queue.max_size() / 2 ? capacity * 2 : queue.max_size());
        queue.reserve(next > capacity ? next : queue.size() + 1);
    }
    for (size_t frame = 0; frame < Frames; ++frame)
        queues[frame].push_back(std::move(callbacks[frame]));
}

} // namespace wowee::rendering
