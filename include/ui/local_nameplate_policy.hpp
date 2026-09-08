#pragma once
#include <cmath>
namespace wowee::ui {
constexpr float LocalNpcNameDistance = 22.0f;
constexpr float LocalTargetNameDistance = 35.0f;
constexpr float LocalPlayerNameDistance = 50.0f;
constexpr unsigned LocalNpcPlateLimit = 12;
inline bool localNpcPlateVisible(float distance, bool selected, bool dead, bool lootable) {
    return std::isfinite(distance) && distance >= 0 && (!dead || lootable || selected) &&
           distance <= (selected ? LocalTargetNameDistance : LocalNpcNameDistance);
}
}
