#include "rendering/gpu_timestamp_validation.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    using wowee::rendering::gpuTimestampDeltaMs;
    double ms = -7.0;
    assert(gpuTimestampDeltaMs(0, 0, 64, 1.0, ms) && ms == 0.0);
    assert(gpuTimestampDeltaMs(0, 250000, 32, 2.0, ms) && ms == 0.5);
    assert(gpuTimestampDeltaMs(0xfffffffe, 3, 32, 1.0, ms) && ms == 0.000005);
    assert(gpuTimestampDeltaMs(UINT64_MAX - 1, 3, 64, 1.0, ms) && ms == 0.000005);
    assert(gpuTimestampDeltaMs(0x1234fffe, 0x67890003, 16, 1.0, ms) && ms == 0.000005);
    ms = -7.0;
    assert(!gpuTimestampDeltaMs(100, 99, 32, 1.0, ms));
    assert(!gpuTimestampDeltaMs(0, 0x80000000, 32, 1.0, ms));
    assert(!gpuTimestampDeltaMs(0, 1, 0, 1.0, ms));
    assert(!gpuTimestampDeltaMs(0, 1, 65, 1.0, ms));
    assert(!gpuTimestampDeltaMs(0, 1, 64, 0.0, ms));
    assert(!gpuTimestampDeltaMs(0, 1, 64, -1.0, ms));
    assert(!gpuTimestampDeltaMs(0, 1, 64, std::numeric_limits<double>::quiet_NaN(), ms));
    assert(!gpuTimestampDeltaMs(0, 1, 64, std::numeric_limits<double>::infinity(), ms));
    assert(!gpuTimestampDeltaMs(0, 1000000001, 64, 1.0, ms));
    assert(!gpuTimestampDeltaMs(0, 1000, 64, std::numeric_limits<double>::max(), ms));
    assert(ms == -7.0); // A rejection cannot leak a fabricated duration.
    std::cout << "PASS: production timestamp conversion, zero, 32/64-bit wrap, advertised width, invalid clocks, stale/backwards intervals\n";
}
