#pragma once
#include "game/local_gameplay.hpp"
namespace wowee::game {
struct LocalRegenerationRates {
    double healthSpiritPerSecond=0,healthItemPerSecond=0;
    double manaSpiritCoefficient=0,manaPerSecond=0,manaInterruptedPerSecond=0;
    bool sourceValues=false;
};
// Base spirit + direct equipped stats + supported unconditional learned-rank
// mana-regeneration talents. Shared by host ticks, hidden druid mana and UI.
// Timed aura credit is applied separately over each aura's actual remaining
// lifetime, never counted again here. manaSpiritCoefficient excludes fixed MP5.
LocalRegenerationRates localRegenerationRates(const LocalRealmPlayer&,const LocalWorldContent&);
uint32_t localRegenerationAuraManaPer5(const LocalRealmPlayer&,const LocalWorldContent&);
}
