#include "rendering/m2_blend_mode.hpp"
#include <cassert>
#include <iostream>
using namespace wowee::rendering;
int main() {
    int count = 0;
    for (int mode = 0; mode <= 3; ++mode) {
        for (bool single : {false, true}) {
            const int encoded = m2EncodeAlphaTest(mode, single);
            assert((encoded & 7) == mode);
            assert(((encoded & 8) != 0) == (single && mode != 0));
            ++count;
        }
    }
    // Refresh from authored mode each draw: toggling target sample count or
    // returning to an unmasked effect must clear every previous override.
    int state = m2EncodeAlphaTest(2, true);
    assert(state == 10);
    state = m2EncodeAlphaTest(2, false);
    assert(state == 2);
    state = m2EncodeAlphaTest(0, true);
    assert(state == 0);
    for (unsigned mode = 0; mode <= 6; ++mode) {
        for (bool hasAlpha : {false, true}) {
            const int test = m2BatchNeedsAlphaTest(mode, hasAlpha) ? 1 : 0;
            assert((m2EncodeAlphaTest(test, true) & 7) == test);
            if (m2BlendIsAdditive(mode)) assert(test == 0);
            ++count;
        }
    }
    std::cout << "PASS M2 cutout mode: " << count << " mode/material combinations and sample-state transitions\n";
}
