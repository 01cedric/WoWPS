#pragma once

/**
 * local_time.hpp - breaking a time_t into local calendar fields, portably.
 *
 * localtime_r is POSIX and does not exist on Windows, where the spelling is
 * localtime_s with its arguments the other way round. Eight places wrote that
 * #ifdef out by hand and a ninth did not, which is the only reason anyone found
 * out: a test using localtime_r compiled everywhere except Windows and failed
 * CI there with "'localtime_r' was not declared in this scope".
 *
 * Neither plain localtime() nor a shared static is used, because both hand back
 * a pointer into a buffer the next caller overwrites. This returns the struct
 * by value, which is what every one of the call sites wanted anyway: each
 * declared its own std::tm and filled it in.
 *
 * Header-only, so a test can use it without linking anything.
 */

#include <ctime>

namespace wowee {
namespace core {

/// Local calendar fields for a point in time.
inline std::tm localTime(std::time_t when) {
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &when);
#else
    localtime_r(&when, &out);
#endif
    return out;
}

}  // namespace core
}  // namespace wowee
