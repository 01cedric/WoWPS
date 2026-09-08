#pragma once

#include <future>
#include <vector>

namespace wowee::core {

// A future returned by ThreadPool::submit does not join on destruction.
// These guards preserve the lifetime of data captured by jobs if another
// operation throws before the normal get(). Waiting never consumes the result
// or rethrows a task exception. Construct before submitting any protected job.
class FutureWaitGuard {
public:
    explicit FutureWaitGuard(std::future<void>& future) noexcept : future_(future) {}
    ~FutureWaitGuard() { if (future_.valid()) future_.wait(); }
    FutureWaitGuard(const FutureWaitGuard&) = delete;
    FutureWaitGuard& operator=(const FutureWaitGuard&) = delete;
private:
    std::future<void>& future_;
};

class FutureGroupWaitGuard {
public:
    explicit FutureGroupWaitGuard(std::vector<std::future<void>>& futures) noexcept : futures_(futures) {}
    ~FutureGroupWaitGuard() {
        for (auto& future : futures_) if (future.valid()) future.wait();
    }
    FutureGroupWaitGuard(const FutureGroupWaitGuard&) = delete;
    FutureGroupWaitGuard& operator=(const FutureGroupWaitGuard&) = delete;
private:
    std::vector<std::future<void>>& futures_;
};

} // namespace wowee::core
