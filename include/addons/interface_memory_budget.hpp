#pragma once
#include <cstddef>
namespace wowee::addons {
// Flexible-page telemetry is conservative: malloc may retain reusable pages.
// Leave a reserve for the existing world and the next individual XML/Lua file.
inline constexpr std::size_t kInterfaceStartHeadroom = 64ull * 1024 * 1024;
inline constexpr std::size_t kInterfaceContinueHeadroom = 24ull * 1024 * 1024;
inline constexpr bool interfaceMemoryAvailable(std::size_t freeBytes, bool measured, bool starting) {
    return measured && freeBytes >= (starting ? kInterfaceStartHeadroom : kInterfaceContinueHeadroom);
}
}
