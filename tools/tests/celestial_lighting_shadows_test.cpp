#include "rendering/sun_direction.hpp"
#include <cassert>
#include <limits>
#include <cstdio>
using namespace wowee::rendering;
int main() {
    for (int minute=0; minute<1440; ++minute) {
        const glm::vec3 sunRay = sunTravelDirection(float(minute)/1440.0f);
        const glm::vec3 keyRay = outdoorKeyLightTravelDirection(sunRay);
        const glm::vec3 visibleDisc = sunRay.z>0 ? sunRay : -sunRay;
        assert(glm::dot(-keyRay,visibleDisc)>0.99999f);
        assert(keyRay.z<=0.00001f);
        assert(std::abs(glm::length(keyRay)-1.0f)<0.00001f);
        // Scaling a resolved/interpolated direction cannot move the source.
        assert(glm::length(keyRay-outdoorKeyLightTravelDirection(sunRay*5.0f))<0.00001f);
    }
    const auto dawn=sunTravelDirection(6.1f/24.0f);
    assert(std::abs(outdoorKeyLightTravelDirection(dawn).z)<0.05f);
    assert(glm::length(outdoorKeyLightTravelDirection(glm::vec3(0)))>0.99f);
    assert(std::isfinite(outdoorKeyLightTravelDirection(glm::vec3(std::numeric_limits<float>::quiet_NaN())).z));
    puts("PASS 1440-minute sun / White Lady disc-shadow-ray alignment, grazing elevations, normalized and invalid input");
}
