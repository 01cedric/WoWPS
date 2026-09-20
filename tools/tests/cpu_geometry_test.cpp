// Exercise the production PS4 allocator with instrumented kernel boundaries.
#include "platform/cpu_geometry.hpp"
#include "audio/decoded_audio_cache.hpp"
#include <memory>
#include <orbis/libkernel.h>
#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {
void require(bool okay, const char* description) {
    if (!okay) { std::fprintf(stderr, "FAIL: %s\n", description); std::abort(); }
}
struct Allocation { size_t bytes; void* mapping; };
std::mutex kernelMutex;
std::unordered_map<off_t, Allocation> allocations;
off_t nextPhysical = 0x4000;
std::atomic<unsigned> allocateCalls{0}, mapCalls{0}, unmapCalls{0}, releaseCalls{0};
bool rejectAllocate = false, rejectMap = false, rejectUnmap = false, rejectRelease = false;
using namespace wowee::platform;
template<class F> void expectBadAlloc(F&& f, const char* description) {
    bool threw = false;
    try { f(); } catch (const std::bad_alloc&) { threw = true; }
    require(threw, description);
}
}
size_t sceKernelGetDirectMemorySize() { return 512 * 1024 * 1024; }
int32_t sceKernelAllocateDirectMemory(off_t begin, off_t end, size_t bytes, size_t align,
                                    int32_t kind, off_t* physical) {
    std::lock_guard<std::mutex> lock(kernelMutex);
    ++allocateCalls;
    require(begin == 0 && end == static_cast<off_t>(sceKernelGetDirectMemorySize()) &&
            align == 16384 && bytes % align == 0 && kind == ORBIS_KERNEL_WB_ONION,
            "direct allocation uses aligned write-back Onion pages");
    if (rejectAllocate) return -1;
    *physical = nextPhysical; nextPhysical += bytes;
    allocations.emplace(*physical, Allocation{bytes, nullptr});
    return 0;
}
int32_t sceKernelMapDirectMemory(void** mapping, size_t bytes, int32_t protection,
                                int32_t flags, off_t physical, size_t align) {
    std::lock_guard<std::mutex> lock(kernelMutex);
    ++mapCalls;
    auto found = allocations.find(physical);
    require(found != allocations.end() && found->second.bytes == bytes &&
            protection == ORBIS_KERNEL_PROT_CPU_RW && flags == 0 && align == 16384,
            "mapping matches its physical allocation and CPU-only protection");
    if (rejectMap) return -1;
    require(posix_memalign(mapping, align, bytes) == 0, "host map allocation succeeds");
    found->second.mapping = *mapping;
    return 0;
}
int32_t sceKernelMunmap(void* mapping, size_t bytes) {
    std::lock_guard<std::mutex> lock(kernelMutex);
    ++unmapCalls;
    for (auto& [physical, allocation] : allocations) {
        if (allocation.mapping != mapping) continue;
        require(allocation.bytes == bytes, "unmap uses original full mapping size");
        if (rejectUnmap) return -1;
        std::free(mapping); allocation.mapping = nullptr; return 0;
    }
    require(false, "unmap only receives a live original mapping"); return -1;
}
int32_t sceKernelReleaseDirectMemory(off_t physical, size_t bytes) {
    std::lock_guard<std::mutex> lock(kernelMutex);
    ++releaseCalls;
    auto found = allocations.find(physical);
    require(found != allocations.end() && found->second.bytes == bytes && !found->second.mapping,
            "physical pages release only after successful unmap");
    if (rejectRelease) return -1;
    allocations.erase(found); return 0;
}
int main() {
    require(cpuGeometryStats().mappedBytes == 0, "allocator starts empty");
    for (size_t bytes : {size_t(0), size_t(1), kCpuGeometryThreshold - 1,
                         kCpuGeometryThreshold, kCpuGeometryThreshold + 1}) {
        auto* p = allocateCpuGeometry(bytes);
        require(p && reinterpret_cast<uintptr_t>(p) % alignof(std::max_align_t) == 0,
                "all returned storage satisfies max_align_t");
        if (bytes) { std::memset(p, 0x5a, bytes); }
        require((cpuGeometryStats().mappedBytes != 0) == (bytes >= kCpuGeometryThreshold),
                "only threshold-sized buffers consume the bounded direct pool");
        freeCpuGeometry(p, bytes);
        require(cpuGeometryStats().mappedBytes == 0, "deallocation restores quota");
    }
    require(allocateCalls == 2 && allocations.empty(), "small buffers use ordinary heap");
    {
        CpuGeometryVector<uint64_t> original(9000, 0x123456789abcdefULL);
        const auto live = cpuGeometryStats().mappedBytes;
        auto* address = original.data();
        CpuGeometryVector<uint64_t> moved(std::move(original));
        CpuGeometryVector<uint64_t> assigned(3000, 7);
        assigned = std::move(moved);
        require(assigned.data() == address && assigned.back() == 0x123456789abcdefULL &&
                cpuGeometryStats().mappedBytes == live, "move assignment transfers storage and releases prior buffer");
        CpuGeometryVector<uint64_t> copied(assigned);
        require(copied == assigned && copied.data() != assigned.data(), "copy has independent owned storage");
    }
    require(cpuGeometryStats().mappedBytes == 0 && allocations.empty(), "vector lifecycle releases all pages");
    {
        CpuGeometryVector<void*> blocks;
        const size_t bytes = 16 * 1024 * 1024;
        for (int i = 0; i < 7; ++i) blocks.push_back(allocateCpuGeometry(bytes));
        const unsigned calls = allocateCalls;
        expectBadAlloc([&] { (void)allocateCpuGeometry(bytes); }, "bounded pool rejects overcommit");
        require(allocateCalls == calls && cpuGeometryStats().mappedBytes <= kCpuGeometryLimit,
                "quota rejection occurs before allocating physical memory");
        for (void* p : blocks) freeCpuGeometry(p, bytes);
    }
    expectBadAlloc([&] { (void)allocateCpuGeometry(SIZE_MAX); }, "oversized byte request rejects without wraparound");
    expectBadAlloc([&] { CpuGeometryAllocator<uint64_t>{}.allocate(SIZE_MAX); }, "element count multiplication rejects overflow");
    std::thread workers[8];
    for (auto& thread : workers) thread = std::thread([] {
        for (unsigned i = 0; i < 150; ++i) {
            CpuGeometryVector<uint64_t> values(3000 + i, i);
            require(values.front() == i && values.back() == i, "concurrent mapped storage remains owned");
        }
    });
    for (auto& thread : workers) thread.join();
    require(cpuGeometryStats().mappedBytes == 0 && allocations.empty(), "concurrent allocations restore quota");
    {
        struct PcmEntry { std::shared_ptr<CpuGeometryVector<uint8_t>> pcmData; };
        wowee::audio::DecodedAudioCache<PcmEntry> cache(16 * 1024 * 1024);
        PcmEntry decoded{std::make_shared<CpuGeometryVector<uint8_t>>(4 * 1024 * 1024, 0x35)};
        cache.insert(1, decoded);
        std::shared_ptr<const CpuGeometryVector<uint8_t>> activeVoice = decoded.pcmData;
        const auto live = cpuGeometryStats().mappedBytes;
        decoded.pcmData.reset();
        cache.clear();
        require(live >= 4 * 1024 * 1024 && cpuGeometryStats().mappedBytes == live,
                "audio cache eviction preserves a live voice's direct PCM allocation");
        require(activeVoice->front() == 0x35 && activeVoice->back() == 0x35,
                "voice reads stable PCM after cache eviction");
        activeVoice.reset();
        require(cpuGeometryStats().mappedBytes == 0 && allocations.empty(),
                "last audio voice releases PCM pages back to the shared direct quota");
    }
    rejectAllocate = true;
    expectBadAlloc([] { (void)allocateCpuGeometry(20000); }, "physical allocation error propagates");
    rejectAllocate = false;
    require(cpuGeometryStats().mappedBytes == 0, "failed physical allocation refunds reservation");
    rejectMap = true;
    expectBadAlloc([] { (void)allocateCpuGeometry(20000); }, "mapping error propagates");
    require(cpuGeometryStats().mappedBytes == 0 && allocations.empty(), "failed map releases physical allocation and refunds reservation");
    rejectMap = false;
    void* p = allocateCpuGeometry(20000);
    const auto charged = cpuGeometryStats().mappedBytes;
    const unsigned releases = releaseCalls;
    rejectUnmap = true;
    freeCpuGeometry(p, 20000);
    require(cpuGeometryStats().mappedBytes == charged && releaseCalls == releases,
            "failed unmap keeps physical pages and quota charged");
    rejectUnmap = false;
    freeCpuGeometry(p, 20000); // Mock failure left the mapping live.
    require(cpuGeometryStats().mappedBytes == 0, "retry after failed unmap can recover allocation");
    p = allocateCpuGeometry(20000);
    rejectRelease = true;
    freeCpuGeometry(p, 20000);
    require(cpuGeometryStats().mappedBytes == charged, "failed physical release remains charged after unmap");
    rejectMap = true;
    expectBadAlloc([] { (void)allocateCpuGeometry(20000); }, "failed map plus failed release propagates");
    require(cpuGeometryStats().mappedBytes == 2 * charged, "map cleanup failure cannot refund unreleased physical quota");
    require(cpuGeometryStats().peakBytes <= kCpuGeometryLimit && cpuGeometryStats().allocationFailures >= 5,
            "diagnostic peak and failed allocation counts remain bounded");
    std::puts("PASS: actual PS4 CPU geometry allocator threshold/alignment, vector ownership, overflow/quota, concurrency, kernel failures");
}
