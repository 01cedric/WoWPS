#include "rendering/cpu_phase_window.hpp"
#include <cassert>
#include <cstdio>

int main() {
    wowee::rendering::CpuPhaseWindow<4> profile;
    assert(profile.meanUs(0) == 0);
    for (uint32_t window = 0; window < 8; ++window) {
        for (uint32_t i = 1; i <= 120; ++i) {
            const bool publish = profile.add({i, 0, 37000, 10000000000ULL});
            assert(publish == (i == 120));
        }
        assert(profile.samples == 120);
        assert(profile.meanUs(0) == 60);
        assert(profile.maxUs[0] == 120);
        assert(profile.meanUs(1) == 0 && profile.maxUs[1] == 0);
        assert(profile.meanUs(2) == 37000 && profile.maxUs[2] == 37000);
        assert(profile.sumUs[3] == 1200000000000ULL);
        assert(profile.meanUs(3) == 10000000000ULL);
        profile.reset();
        assert(profile.samples == 0);
        for (unsigned phase = 0; phase < 4; ++phase) {
            assert(profile.sumUs[phase] == 0 && profile.maxUs[phase] == 0);
            assert(profile.meanUs(phase) == 0);
        }
    }
    std::puts("CPU phase windows: 8 x 120 samples, means/maxima/reset/64-bit totals PASS");
}
