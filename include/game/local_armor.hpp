#pragma once
#include <algorithm>
#include <cstdint>
namespace wowee::game {
// WotLK base armor curve, without penetration or spell-specific scripted overrides.
// Twice the denominator constant keeps the level-60+ half units exact.
inline uint32_t localArmorReducedDamage(uint32_t damage,uint32_t armor,uint8_t attackerLevel) {
    if(!damage||!armor)return damage;
    const uint64_t level=std::clamp(uint32_t(attackerLevel),1u,83u);
    const uint64_t twiceK=800+170*level+(level>59?765*(level-59):0);
    const uint64_t denominator=twiceK+2ULL*armor;
    const uint64_t reduced=(uint64_t(damage)*twiceK+denominator-1)/denominator;
    return uint32_t(std::max(reduced,(uint64_t(damage)+3)/4)); // 75% maximum mitigation.
}
}
