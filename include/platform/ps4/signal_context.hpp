#pragma once

#include <cstddef>
#include <cstdint>

namespace wowee::platform::ps4 {

// PS4 guest signal ABI, verified against shadPS4's guest definitions at:
// https://github.com/shadps4-emu/shadPS4/blob/e67d5676d95545a5cd9c16ed48896ebd45c6cfbf/src/core/libraries/kernel/threads/exception.h
// The register prefix agrees with FreeBSD 9's amd64 mcontext, but PS4 places
// that prefix at ucontext+0x40 and extends mcontext to 0x480 bytes. Neither
// the OpenOrbis musl/Linux ucontext_t nor stock FreeBSD ucontext_t is suitable.
// Only the fixed integer-register prefix is read. No FP state or saved stack
// pointers are dereferenced, and no unwinder or allocator runs in the handler.
namespace signal_context_detail {
struct MachinePrefix {
    uint64_t onStack;
    uint64_t rdi, rsi, rdx, rcx, r8, r9, rax, rbx, rbp;
    uint64_t r10, r11, r12, r13, r14, r15;
    uint32_t trap;
    uint16_t fs, gs;
    uint64_t faultAddress;
    uint32_t flags;
    uint16_t es, ds;
    uint64_t error, rip, cs, rflags, rsp, ss, length;
};

struct ContextPrefix {
    uint8_t signalMaskAndPadding[0x40];
    MachinePrefix machine;
};

static_assert(offsetof(ContextPrefix, machine) == 0x40);
static_assert(offsetof(MachinePrefix, trap) == 0x80);
static_assert(offsetof(MachinePrefix, faultAddress) == 0x88);
static_assert(offsetof(MachinePrefix, rip) == 0xa0);
static_assert(offsetof(MachinePrefix, rsp) == 0xb8);
static_assert(offsetof(MachinePrefix, length) == 0xc8);
static_assert(sizeof(ContextPrefix) == 0x110);

inline uint64_t readLittleEndian64(const unsigned char* bytes) noexcept {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= uint64_t(bytes[i]) << (8 * i);
    return value;
}
} // namespace signal_context_detail

struct InterruptedRegisters {
    uint64_t rip = 0, rsp = 0, rbp = 0;
    uint64_t faultAddress = 0, error = 0;
    uint64_t rax = 0, rbx = 0, rdi = 0, rsi = 0;
    uint64_t rdx = 0, rcx = 0, r8 = 0, r9 = 0;
    uint64_t r10 = 0, r11 = 0, r12 = 0, r13 = 0, r14 = 0, r15 = 0;
    uint32_t trap = 0;
};

constexpr size_t kKernelSignalContextPrefixBytes = 0x110;
constexpr uint64_t kKernelMachineContextBytes = 0x480;

// raw must be the kernel-provided signal context, or a readable test buffer
// of at least availableBytes. An unexpected ABI is reported as unavailable;
// do not guess register offsets or fall back to the host's ucontext_t.
inline bool decodeKernelSignalContext(const void* raw, size_t availableBytes,
                                      InterruptedRegisters& out) noexcept {
    if (!raw || availableBytes < kKernelSignalContextPrefixBytes) return false;
    using namespace signal_context_detail;
    const auto* machine = static_cast<const unsigned char*>(raw) + 0x40;
    const auto read = [machine](size_t offset) noexcept {
        return readLittleEndian64(machine + offset);
    };
    if (read(offsetof(MachinePrefix, length)) != kKernelMachineContextBytes ||
        (read(offsetof(MachinePrefix, cs)) & 3u) != 3u) return false;

    out.rip = read(offsetof(MachinePrefix, rip));
    out.rsp = read(offsetof(MachinePrefix, rsp));
    out.rbp = read(offsetof(MachinePrefix, rbp));
    out.faultAddress = read(offsetof(MachinePrefix, faultAddress));
    out.error = read(offsetof(MachinePrefix, error));
    out.rax = read(offsetof(MachinePrefix, rax));
    out.rbx = read(offsetof(MachinePrefix, rbx));
    out.rdi = read(offsetof(MachinePrefix, rdi));
    out.rsi = read(offsetof(MachinePrefix, rsi));
    out.rdx = read(offsetof(MachinePrefix, rdx));
    out.rcx = read(offsetof(MachinePrefix, rcx));
    out.r8 = read(offsetof(MachinePrefix, r8));
    out.r9 = read(offsetof(MachinePrefix, r9));
    out.r10 = read(offsetof(MachinePrefix, r10));
    out.r11 = read(offsetof(MachinePrefix, r11));
    out.r12 = read(offsetof(MachinePrefix, r12));
    out.r13 = read(offsetof(MachinePrefix, r13));
    out.r14 = read(offsetof(MachinePrefix, r14));
    out.r15 = read(offsetof(MachinePrefix, r15));
    out.trap = static_cast<uint32_t>(read(offsetof(MachinePrefix, trap)));
    return true;
}

} // namespace wowee::platform::ps4
