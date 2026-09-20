#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_pet_spell.hpp"
#include <algorithm>
#include <vector>
namespace wowee::game {
// Pin every non-text source column, including all three effect slots and the
// unused attribute words. A future data profile must be reviewed as a whole.
template<class Tables>
size_t importLocalPetSpells(const Tables& t,std::vector<LocalSpellDefinition>& spells) {
    if(!t.ready)return 0;
    size_t count=0;
    for(const auto& rank:kLocalPetFireboltRanks) {
        const auto row=Tables::lookup(t.spellIndex,rank.id);
        if(row<0||spells.size()>=8192)continue;
        uint32_t hash=2166136261u;
        for(unsigned col=0;col<234;++col)if(col<136||col>=204)
            hash=(hash^t.spells->getUInt32(row,col))*16777619u;
        if(hash!=rank.sourceHash)continue;
        const auto cast=Tables::lookup(t.castIndex,t.spells->getUInt32(row,28));
        const auto range=Tables::lookup(t.rangeIndex,t.spells->getUInt32(row,46));
        if(cast<0||range<0||t.casts->getInt32(cast,2)||
           t.casts->getUInt32(cast,1)!=t.casts->getUInt32(cast,3)||
           t.ranges->getFloat(range,1)||t.ranges->getFloat(range,2)||
           t.ranges->getFloat(range,3)!=30.f||t.ranges->getFloat(range,4)!=30.f)continue;
        if(std::any_of(spells.begin(),spells.end(),[&](const auto& d){return d.id==rank.id;}))continue;
        LocalSpellDefinition d;d.id=rank.id;d.name=t.spells->getString(row,136);
        d.clientSpell=true;d.npcOnly=true;d.allowableClasses=0;
        d.schoolMask=4;d.sourceDamageClass=1;d.spellFamily=5;d.spellFamilyFlags={4096,0,0};
        d.castTimeMs=d.sourceRawCastTimeMs=t.casts->getUInt32(cast,1);
        d.sourceProjectileSpeed=t.spells->getFloat(row,47);d.range=30.f;
        d.resourceType=0;d.mana=t.spells->getUInt32(row,42);
        d.damage=t.spells->getUInt32(row,80)+1;d.damageMax=t.spells->getUInt32(row,80)+t.spells->getUInt32(row,74);
        d.damagePerLevel=t.spells->getFloat(row,77);d.baseLevel=rank.level;d.maxLevel=t.spells->getUInt32(row,37);
        d.spellLevel=rank.level;d.directEffectSlot=0;
        d.iconId=t.spells->getUInt32(row,133);d.visualId=t.spells->getUInt32(row,131);
        d.preventionType=1;d.interruptFlags=15;d.sourceDirectDamage=true;
        spells.push_back(std::move(d));++count;
    }
    return count;
}
}
