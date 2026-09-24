#pragma once

#include <cmath>
#include <cstdint>

namespace wowee::game {

// The bounded local vehicle runtime exposes the only two power contracts it
// actually simulates.  Energy spends the hull's replicated pool; None is for
// authored buttons which have no resource cost.  Numeric values are local
// content schema values, not Spell.dbc Powers enum values.
enum class LocalVehiclePowerType : uint8_t { None = 0, Energy = 1 };

inline constexpr uint8_t kLocalVehiclePhysicalSchool = 1;
inline constexpr uint8_t kLocalVehicleSchoolMask = 0x7f;

inline bool validLocalVehicleSchool(uint8_t school) {
    return school && !(school & (school - 1)) && !(school & ~kLocalVehicleSchoolMask);
}

inline bool localVehicleAreaContains(float cx, float cy, float cz,
                                     float x, float y, float z, float radius) {
    if (!std::isfinite(cx + cy + cz + x + y + z + radius) || radius < 0) return false;
    const double dx=double(x)-cx,dy=double(y)-cy,dz=double(z)-cz;
    return dx*dx+dy*dy+dz*dz <= double(radius)*radius;
}

} // namespace wowee::game
