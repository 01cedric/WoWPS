#include "core/app_clock.hpp"

#include <chrono>

namespace wowee::core {

namespace {
// Zero in the live client - advanceTestClock is only ever called by the
// headless harness. Added to every reading so a --tick loop can move "now"
// forward without waiting on real time.
double& testClockOffset() {
    static double off = 0.0;
    return off;
}
}  // namespace

double appTimeSeconds() {
    using clock = std::chrono::steady_clock;
    // Fixed on the first call, so every later caller measures from the same
    // point however late it starts asking.
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count()
         + testClockOffset();
}

void advanceTestClock(double seconds) {
    testClockOffset() += seconds;
}

} // namespace wowee::core
