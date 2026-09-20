#pragma once
#include <cstdint>
#include <iterator>
namespace wowee::game {
// Client 12340 Spell.dbc + SkillLineAbility skill 188 (Imp), auto-learn type 2.
// The source ranks are 3110..47964, not ordinary player trainer spells.
struct LocalPetFireboltRank { uint32_t id; uint8_t level; uint32_t sourceHash; };
inline constexpr LocalPetFireboltRank kLocalPetFireboltRanks[] = {
    {3110, 1, 3710927459u},
    {7799, 8, 2922158993u},
    {7800, 18, 2779238990u},
    {7801, 28, 3042161252u},
    {7802, 38, 708233091u},
    {11762, 48, 3816452473u},
    {11763, 58, 343980482u},
    {27267, 68, 336012954u},
    {47964, 78, 2464860886u},
};
constexpr uint32_t localPetFireboltSpell(uint32_t entry,uint8_t level) {
    if(entry!=416||!level)return 0;
    uint32_t id=0;for(const auto& rank:kLocalPetFireboltRanks)if(level>=rank.level)id=rank.id;
    return id;
}
constexpr bool localPetFireboltId(uint32_t id) {
    for(const auto& rank:kLocalPetFireboltRanks)if(rank.id==id)return true;return false;
}
}
