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
    d.npcScales=d.npcCostScales=(t.spells->getUInt32(row,4)&0x80000u)!=0&&t.spells->getUInt32(row,39)>0;
    d.npcTargetShape=0;d.npcPositive=false;
    d.unsupportedReason.clear();return true;
}

/// Generic NPC-only spells named by the generated SmartAI script family. The
/// ordinary client decoder reads every column and, for a creature caster,
/// decodeCreatureEffects admits the shapes the generator admits (one enemy,
/// the caster, areas, cones, chains; damage, weapon, leech, heal, interrupt,
/// knockback and the aura kinds listed there). A cost is admitted only in mana
/// (POWER_MANA), which the creature pays from its own pool.
template<class Tables>
bool decodeLocalNpcGenericSpell(const Tables& t,uint32_t row,LocalSpellDefinition& d) {
    d.clientSpell=true;
    if(!decodeClientSpell(t,row,d,true) || !d.unsupportedReason.empty())return false;
    const auto u=[&](uint32_t c){return t.spells->getUInt32(row,c);};
    if(u(4)&0x40u){d.unsupportedReason="Passive spell";return false;}
    if((d.mana||d.manaPercent)&&(u(41)!=0||(u(5)&0x2u))){d.unsupportedReason="Unsupported NPC spell power";return false;}
    if(d.sourceDamageClass>3||d.snarePercent||d.controlProfile||d.dispelProfile||d.buffArmor||d.buffHealth||d.buffAbsorb||d.formId||d.chainTargets>1){
        d.unsupportedReason="Unsupported NPC spell shape";return false;
    }
    // 2.39: the player-side Arcane Explosion profile (areaRadius) of a mage
    // creature's rank; the creature area lives in npcAreaRadius.
    d.areaRadius=0;
    d.npcOnly=true;d.allowableClasses=0;d.triggeredOnly=false;d.resourceType=0;
    // Player-side cost modifiers never apply to a creature caster; the flat and
    // percent columns are read raw and scaled by localNpcSpellManaCost.
    d.mana=u(42);d.manaPercent=u(204);
    d.preventionType=uint8_t(u(214));
    d.npcScales=d.npcCostScales=(u(4)&0x80000u)!=0&&u(39)>0;
    d.npcCastableWhileDead=(u(4)&0x100000u)!=0;
    return true;
}

template<class Tables>
size_t importLocalNpcSpells(const Tables& t,std::vector<LocalSpellDefinition>& spells) {
    if(!t.ready)return 0;
    std::vector<uint32_t> ids;
    for(const auto& profile:kLocalNpcSpellProfiles)if(profile.spellId())ids.push_back(profile.spellId());
    for(const auto& profile:kLocalNpcSmartLists)if(profile.spellId())ids.push_back(profile.spellId());
    std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    size_t retained=0;
    // Spells triggered by an installed spell (SPELL_EFFECT_TRIGGER_SPELL,
    // SPELL_AURA_PERIODIC_TRIGGER_SPELL) are decoded after it; they may not
    // trigger further.
    std::vector<uint32_t> triggered;
    const auto decodeOne=[&](uint32_t id,bool isTriggered) {
        // Never replace an ordinary class spell with an NPC-only definition.
        if(std::any_of(spells.begin(),spells.end(),[&](const auto& d){return d.id==id;}))return;
        const auto row=Tables::lookup(t.spellIndex,id);
        if(row<0||spells.size()>=8192)return;
        LocalSpellDefinition d;d.id=id;
        // The two originally reviewed rows keep their column-pinned decoder.
        bool ok=false;
        if(id==5401||id==11985)ok=decodeLocalNpcSpell(t,uint32_t(row),d);
        else if constexpr(requires{t.durations;t.durationIndex;})ok=decodeLocalNpcGenericSpell(t,uint32_t(row),d);
        if(ok&&isTriggered&&(d.npcTriggerSpellId||d.npcPeriodicTriggerSpellId)){d.unsupportedReason="A triggered creature spell may not trigger further";ok=false;}
        if(ok&&isTriggered&&d.npcProcSpellId){d.unsupportedReason="A triggered creature spell may not trigger further";ok=false;}
        if(ok) {
            if(d.npcTriggerSpellId)triggered.push_back(d.npcTriggerSpellId);
            if(d.npcPeriodicTriggerSpellId)triggered.push_back(d.npcPeriodicTriggerSpellId);
            if(d.npcProcSpellId)triggered.push_back(d.npcProcSpellId);
            spells.push_back(std::move(d));++retained;
        }
    };
    for(uint32_t id:ids)decodeOne(id,false);
    std::sort(triggered.begin(),triggered.end());triggered.erase(std::unique(triggered.begin(),triggered.end()),triggered.end());
    for(uint32_t id:triggered)decodeOne(id,true);
    return retained;
}
}
