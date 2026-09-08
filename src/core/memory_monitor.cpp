#include "core/memory_monitor.hpp"
#include "core/logger.hpp"
#include <fstream>
#include <string>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#elif defined(WOWEE_PS4)
#include "platform/ps4/cpu_memory.hpp"
#else
#include <sys/sysinfo.h>
#endif

namespace wowee {
namespace core {

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(WOWEE_PS4)
namespace {
size_t readMemAvailableBytesFromProc() {
    std::ifstream meminfo("/proc/meminfo");
    if (!meminfo.is_open()) return 0;

    std::string line;
    while (std::getline(meminfo, line)) {
        // /proc/meminfo format: "MemAvailable:  123456789 kB"
        static constexpr size_t kFieldPrefixLen = 13; // strlen("MemAvailable:")
        if (line.rfind("MemAvailable:", 0) != 0) continue;
        std::istringstream iss(line.substr(kFieldPrefixLen));
        size_t kb = 0;
        iss >> kb;
        if (kb > 0) return kb * 1024ull;
        break;
    }
    return 0;
}
} // namespace
#endif // !_WIN32 && !__APPLE__

MemoryMonitor& MemoryMonitor::getInstance() {
    static MemoryMonitor instance;
    return instance;
}

void MemoryMonitor::initialize() {
#if !defined(WOWEE_PS4)
    constexpr size_t kOneGB = 1024ull * 1024 * 1024;
    // Fallback if OS API unavailable - 16 GB is a safe conservative estimate
    // that prevents over-aggressive asset caching on unknown hardware.
    constexpr size_t kFallbackRAM = 16 * kOneGB;
#endif

#if defined(WOWEE_PS4)
    totalRAM_ = platform::ps4::kCpuMemoryBudgetLimit;
    const auto available = platform::ps4::queryAvailableCpuMemory();
    if (available.measured) {
        LOG_INFO("PS4 CPU memory: flexible available=", available.bytes / (1024 * 1024),
                 " MiB, CPU budget ceiling=", totalRAM_ / (1024 * 1024),
                 " MiB, shared cache ceiling=32 MiB; GPU direct memory excluded");
    } else {
        LOG_WARNING("PS4 CPU memory query unavailable: status=", available.status,
                    "; automatic cache allowance=0, pressure throttling unavailable");
    }
#elif defined(_WIN32)
    ULONGLONG totalKB = 0;
    if (GetPhysicallyInstalledSystemMemory(&totalKB)) {
        totalRAM_ = static_cast<size_t>(totalKB) * 1024ull;
        LOG_INFO("System RAM detected: ", totalRAM_ / kOneGB, " GB");
    } else {
        totalRAM_ = kFallbackRAM;
        LOG_WARNING("Could not detect system RAM, assuming 16GB");
    }
#elif defined(__APPLE__)
    int64_t physmem = 0;
    size_t len = sizeof(physmem);
    if (sysctlbyname("hw.memsize", &physmem, &len, nullptr, 0) == 0) {
        totalRAM_ = static_cast<size_t>(physmem);
        LOG_INFO("System RAM detected: ", totalRAM_ / kOneGB, " GB");
    } else {
        totalRAM_ = kFallbackRAM;
        LOG_WARNING("Could not detect system RAM, assuming 16GB");
    }
#else
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        totalRAM_ = static_cast<size_t>(info.totalram) * info.mem_unit;
        LOG_INFO("System RAM detected: ", totalRAM_ / kOneGB, " GB");
    } else {
        totalRAM_ = kFallbackRAM;
        LOG_WARNING("Could not detect system RAM, assuming 16GB");
    }
#endif
}

size_t MemoryMonitor::getAvailableRAM() const {
#ifdef _WIN32
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status)) {
        return static_cast<size_t>(status.ullAvailPhys);
    }
    return totalRAM_ / 2;
#elif defined(__APPLE__)
    // hw.usermem is a 32-bit kernel sysctl on macOS: on systems with ≥16 GB RAM
    // the value overflows signed int32, truncating to ~2 GB and causing false
    // severe-pressure reports.  Use the Mach VM statistics API instead, which
    // is the same source that Activity Monitor and `vm_stat` use.
    {
        mach_port_t host_port = mach_host_self();
        vm_size_t page_size = 0;
        host_page_size(host_port, &page_size);
        if (page_size > 0) {
            mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
            vm_statistics64_data_t vm_stat;
            if (host_statistics64(host_port, HOST_VM_INFO64,
                                  reinterpret_cast<host_info64_t>(&vm_stat),
                                  &count) == KERN_SUCCESS) {
                // free + inactive pages are reclaimable on demand
                return (static_cast<size_t>(vm_stat.free_count) +
                        static_cast<size_t>(vm_stat.inactive_count)) *
                       static_cast<size_t>(page_size);
            }
        }
    }
    return totalRAM_ / 2;
#elif defined(WOWEE_PS4)
    // Zero also conservatively disables automatic cache allowance if telemetry
    // is unavailable. Never substitute half of the GPU direct-memory pool.
    return platform::ps4::queryAvailableCpuMemory().bytes;
#else
    // Best source on Linux for reclaimable memory headroom.
    if (size_t memAvailable = readMemAvailableBytesFromProc(); memAvailable > 0) {
        return memAvailable;
    }

    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        // Fallback approximation if /proc/meminfo is unavailable.
        size_t freeBytes = static_cast<size_t>(info.freeram) * info.mem_unit;
        size_t bufferBytes = static_cast<size_t>(info.bufferram) * info.mem_unit;
        size_t available = freeBytes + bufferBytes;
        return (totalRAM_ > 0 && available > totalRAM_) ? totalRAM_ : available;
    }
    return totalRAM_ / 2;  // Fallback: assume 50% available
#endif
}

size_t MemoryMonitor::getRecommendedCacheBudget() const {
    size_t available = getAvailableRAM();
#if defined(WOWEE_PS4)
    const size_t budget = available / 4;
    return budget < platform::ps4::kCpuCacheBudgetLimit
               ? budget : platform::ps4::kCpuCacheBudgetLimit;
#else
    // Use 50% of available RAM for caches, hard-capped at 16 GB.
    static constexpr size_t kHardCapBytes = 16ull * 1024 * 1024 * 1024;  // 16 GB
    size_t budget = available * 50 / 100;
    return budget < kHardCapBytes ? budget : kHardCapBytes;
#endif
}

bool MemoryMonitor::isMemoryPressure() const {
#if defined(WOWEE_PS4)
    const auto available = platform::ps4::queryAvailableCpuMemory();
    // A failed query is not proof of exhaustion. In that case cache sizing is
    // conservative, but terrain workers must not stall forever awaiting a
    // measurement which this firmware cannot provide.
    return available.measured && available.bytes < platform::ps4::kCpuPressureHeadroom;
#else
    size_t available = getAvailableRAM();
    // Memory pressure if < 10% RAM available
    return available < (totalRAM_ * 10 / 100);
#endif
}

bool MemoryMonitor::isSevereMemoryPressure() const {
#if defined(WOWEE_PS4)
    const auto available = platform::ps4::queryAvailableCpuMemory();
    return available.measured && available.bytes < platform::ps4::kCpuSeverePressureHeadroom;
#else
    size_t available = getAvailableRAM();
    // Severe pressure if < 15% RAM available - background workers should
    // pause entirely to avoid OOM-killing other applications.
    return available < (totalRAM_ * 15 / 100);
#endif
}

} // namespace core
} // namespace wowee
