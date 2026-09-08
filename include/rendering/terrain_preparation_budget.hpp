#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <utility>

namespace wowee::rendering {

// The caller serializes admissions with TerrainManager::queueMutex. A permit
// follows the decoded payload through preparing -> ready -> finalizing;
// moving between those queues never makes room for another payload.
class TerrainPreparationBudget {
public:
    class Lease {
    public:
        Lease() = default;
        ~Lease() { if (budget_) budget_->release(); }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept : budget_(std::exchange(other.budget_, nullptr)) {}
        Lease& operator=(Lease&& other) noexcept {
            if (this != &other) {
                if (budget_) budget_->release();
                budget_ = std::exchange(other.budget_, nullptr);
            }
            return *this;
        }
    private:
        friend class TerrainPreparationBudget;
        explicit Lease(TerrainPreparationBudget* budget) : budget_(budget) {}
        TerrainPreparationBudget* budget_ = nullptr;
    };
    enum class Admission { Wait, Normal, PressureProbe };
    Admission acquire(size_t limit, bool pressure, uint64_t nowMs) {
        const size_t outstanding = outstanding_.load(std::memory_order_acquire);
        if (outstanding >= limit) return Admission::Wait;
        if (pressure) {
            // Kernel free-page telemetry excludes malloc's reusable arenas.
            // With no live payload that could finish and release memory, an
            // endless pressure wait cannot progress. Retry one allocation at
            // most once a second; the worker trims cache first and catches OOM.
            if (outstanding != 0 || nowMs < nextPressureProbeMs_) return Admission::Wait;
            nextPressureProbeMs_ = nowMs + 1000;
        }
        outstanding_.fetch_add(1, std::memory_order_acq_rel);
        return pressure ? Admission::PressureProbe : Admission::Normal;
    }
    // Adopt exactly one successful admission, without another allocation.
    [[nodiscard]] Lease lease() { return Lease(this); }
    void release(size_t count = 1) noexcept {
        size_t previous = outstanding_.load(std::memory_order_relaxed);
        while (!outstanding_.compare_exchange_weak(previous,
                count < previous ? previous-count : 0,
                std::memory_order_release, std::memory_order_relaxed)) {}
    }
    [[nodiscard]] size_t outstanding() const { return outstanding_.load(std::memory_order_acquire); }
private:
    std::atomic<size_t> outstanding_{0};
    uint64_t nextPressureProbeMs_ = 0;
};

} // namespace wowee::rendering
