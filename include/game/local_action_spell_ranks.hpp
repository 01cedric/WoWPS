#pragma once
#include "game/local_gameplay.hpp"
#include "game/protocol_constants.hpp"
#include <algorithm>
namespace wowee::game {
// Only source successor links identify ranks; display names are not identities.
// A missing link or malformed cycle never selects an unrelated replacement.
inline uint32_t localActionSpellRank(const LocalWorldContent& c,const std::vector<uint32_t>& known,uint32_t original) {
    if(original==SPELL_ID_ATTACK||original==SPELL_ID_HEARTHSTONE||!c.spell(original))return original;
    if(std::find(known.begin(),known.end(),original)!=known.end())return original;
    uint32_t current=original,replacement=0;
    for(size_t depth=0;depth<2048;++depth) {
        const auto* d=c.spell(current);
        if(!d||!d->supercededBySpell)return replacement;
        current=d->supercededBySpell;if(current==original)return 0;
        if(std::find(known.begin(),known.end(),current)!=known.end())replacement=current;
    }
    return 0;
}
}
