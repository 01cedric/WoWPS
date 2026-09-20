#pragma once
#include "game/local_gameplay.hpp"
namespace wowee::game {
struct LocalResourcePools {
    uint32_t baseHealth=0,baseMana=0,health=1,mana=0;
    bool sourceValues=false;
};
uint32_t localClassBaseMana(const LocalRealmPlayer&);
LocalResourcePools localResourcePools(const LocalRealmPlayer&,const LocalWorldContent&);
// Base pools are separate from maximum pools: Intellect must not raise a
// spell's percentage-of-base-mana cost.
constexpr uint64_t localPrimaryPoolBonus(uint32_t value,uint32_t beyondTwenty){
    return std::min(20u,value)+uint64_t(value>20?value-20:0)*beyondTwenty;
}
}
