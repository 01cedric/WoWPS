#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_npc_spell_profiles.hpp"
#include <algorithm>
#include <vector>

namespace wowee::game {
template<class Tables>
bool decodeLocalNpcSpell(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    if(d.id!=5401&&d.id!=11985)return false;
    const auto fail=[&](){d.unsupportedReason="Unreviewed NPC spell source profile";return false;};
    if(!t.ready||!t.spells||!t.casts||!t.ranges||row>=t.spells->getRecordCount()||t.spells->getUInt32(row,0)!=d.id)return fail();
    struct Column {uint16_t column;uint32_t lightning,fireball;};
    static constexpr Column columns[]={
#include "game/local_npc_spell_columns_generated.inc"
    };
    for(const auto& col:columns)if(t.spells->getUInt32(row,col.column)!=(d.id==5401?col.lightning:col.fireball))return fail();
    const bool lightning=d.id==5401;
    const auto cast=Tables::lookup(t.castIndex,lightning?5:14);
    const auto range=Tables::lookup(t.rangeIndex,lightning?4:5);
    const uint32_t castMs=lightning?2000:3000;
    const float maxRange=lightning?30.f:40.f;
    if(cast<0||range<0||t.casts->getUInt32(cast,1)!=castMs||t.casts->getInt32(cast,2)||t.casts->getUInt32(cast,3)!=castMs||
       t.ranges->getFloat(range,1)!=0||t.ranges->getFloat(range,2)!=0||t.ranges->getFloat(range,3)!=maxRange||
       t.ranges->getFloat(range,4)!=maxRange||t.ranges->getUInt32(range,5))return fail();
    d.clientSpell=true;d.npcOnly=true;d.allowableClasses=0;d.triggeredOnly=false;
    d.name=t.spells->getString(row,136);if(d.name.empty()||d.name.size()>96)d.name=lightning?"Lightning Bolt":"Fireball";
    d.iconId=t.spells->getUInt32(row,133);d.visualId=t.spells->getUInt32(row,131);
    d.schoolMask=lightning?8:4;d.sourceDamageClass=1;d.sourceRawCastTimeMs=d.castTimeMs=castMs;
    d.sourceNotAProc=false;d.sourceCantCrit=false;d.sourceCantReflect=false;d.sourceAlwaysHit=false;
    d.sourceProjectileSpeed=t.spells->getFloat(row,47);
    d.resourceType=0;d.mana=0;d.manaPercent=0;d.range=maxRange;d.minRange=0;
    d.damage=lightning?8:64;d.damageMax=lightning?12:86;
    d.damagePerLevel=t.spells->getFloat(row,77);d.baseLevel=lightning?5:20;d.maxLevel=0;
    d.directEffectSlot=0;d.cooldownCategory=lightning?2:0;d.cooldownMs=0;d.categoryCooldownMs=0;
    d.globalCooldownMs=lightning?0:1500;d.interruptFlags=lightning?9:15;
    // SPELL_PREVENTION_TYPE belongs to the spell being prevented, so a silence
    // on the caster can only suppress this cast if the column travels with it
    // (Spell::CheckCasterAuras, Spell.cpp:7167). This is a separate decode path
    // from decodeClientSpell, which is why it needs its own read; the generated
    // column table already pins column 214 to 1 for both rows.
    d.preventionType=uint8_t(t.spells->getUInt32(row,214));
    d.unsupportedReason.clear();return true;
}

template<class Tables>
size_t importLocalNpcSpells(const Tables& t,std::vector<LocalSpellDefinition>& spells) {
    if(!t.ready)return 0;
    size_t retained=0;
    for(uint32_t id:{5401u,11985u}) {
        // Never replace an ordinary class spell with an NPC-only definition.
        if(std::any_of(spells.begin(),spells.end(),[&](const auto& d){return d.id==id;}))continue;
        const auto row=Tables::lookup(t.spellIndex,id);
        if(row<0||spells.size()>=8192)continue;
        LocalSpellDefinition d;d.id=id;
        if(decodeLocalNpcSpell(t,uint32_t(row),d)){spells.push_back(std::move(d));++retained;}
    }
    return retained;
}
}
