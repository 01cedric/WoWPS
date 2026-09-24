#pragma once
#include "game/local_forms.hpp"
#include "game/local_arcane.hpp"
#include "game/local_gameplay.hpp"
namespace wowee::game {
inline unsigned localTalentPointsSpent(const LocalRealmPlayer& p){unsigned n=0;for(auto [id,rank]:p.talents)n+=rank;return n;}
inline unsigned localTalentPointsAvailable(const LocalRealmPlayer& p){const unsigned earned=p.level>9?p.level-9:0,spent=localTalentPointsSpent(p);return earned>spent?earned-spent:0;}
inline const LocalSpellDefinition* localTalentSpell(const LocalWorldContent& c,uint32_t id,uint8_t rank){
    if(c.talentIndexReady) {
        const std::array<uint32_t,3> key{id,rank,0};
        const auto found=std::lower_bound(c.talentSpellIndex.begin(),c.talentSpellIndex.end(),key);
        return found!=c.talentSpellIndex.end() && (*found)[0]==id && (*found)[1]==rank?c.spell((*found)[2]):nullptr;
    }
    for(const auto& s:c.spells)if(s.talentId==id && s.talentRank==rank)return &s;return nullptr;
}
inline bool validLocalTalents(const LocalRealmPlayer& p){
    if(p.talents.size()>71 || localTalentPointsSpent(p)>unsigned(p.level>9?p.level-9:0))return false;
    uint32_t previous=0;for(auto [id,rank]:p.talents){if(!id || id<=previous || !rank || rank>5)return false;previous=id;}return true;
}
// Keep saved allocations, but never execute an effect whose direct required
// talent is missing, belongs to another class or has no supported definition.
// This deliberately does not rebuild historical tier/point progression.
inline bool localTalentPrerequisitesReady(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                         const LocalSpellDefinition& talent) {
    if(p.classId<1||p.classId>11)return false;
    for(size_t i=0;i<talent.talentPrerequisites.size();++i)if(const auto required=talent.talentPrerequisites[i]) {
        const auto allocated=std::find_if(p.talents.begin(),p.talents.end(),
            [&](const auto& entry){return entry.first==required;});
        if(allocated==p.talents.end()||allocated->second<unsigned(talent.talentPrerequisiteRanks[i])+1)return false;
        const auto* definition=localTalentSpell(c,required,allocated->second);
        if(!definition||!definition->unsupportedReason.empty()||
           !(definition->allowableClasses&(1u<<(p.classId-1))))return false;
    }
    return true;
}
// Aura 52 is applied to the weapon used by this attack. An allocation stores
// only its current rank; resetting or replacing it therefore needs no cache.
// Callers must establish that the weapon is owned and valid in its hand slot.
// UINT32_MAX denotes an unarmed/form attack: only unrestricted auras match it.
inline uint32_t localTalentWeaponCritPct(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                       uint32_t itemClass,uint32_t subclass,uint32_t inventoryType) {
    if(p.dead || p.classId<1 || p.classId>11 || !validLocalTalents(p))return 0;
    uint32_t percent=0;
    for(auto [id,rank]:p.talents) {
        const auto* s=localTalentSpell(c,id,rank);
        if(!s || !s->passive || !s->unsupportedReason.empty() || !s->passiveMeleeCritPct ||
           !(s->allowableClasses&(1u<<(p.classId-1))) || !localTalentPrerequisitesReady(p,c,*s))continue;
        if(s->requiredItemClass>=0 && uint32_t(s->requiredItemClass)!=itemClass)continue;
        if(s->requiredItemSubclasses && (subclass>=32 || !(s->requiredItemSubclasses&(1u<<subclass))))continue;
        if(s->requiredInventoryTypes && (inventoryType>=32 || !(s->requiredInventoryTypes&(1u<<inventoryType))))continue;
        percent=std::min(100u,percent+uint32_t(s->passiveMeleeCritPct));
    }
    return percent;
}
// Total-stat modifiers apply after racial/base and equipped attributes. Base
// attributes stay available separately for UI and diminishing-return formulas.
/// The aura-137 multiplier the allocated talents contribute, separately from the
/// truncation, so a caller that has another aura-137 source (a form boost, since
/// the implementation) can multiply both and truncate ONCE - which is what the reference does,
/// since GetTotalAuraMultiplierByMiscValue sums every aura before the one
/// UNIT_FIELD_STAT write.
inline float localTalentTotalStatMultiplier(const LocalRealmPlayer& p,const LocalWorldContent& c,size_t stat) {
    if(stat>=5 || p.dead || p.classId<1 || p.classId>11 || !validLocalTalents(p))return 1.f;
    float multiplier=1;
    for(auto [id,rank]:p.talents) {
        const auto* s=localTalentSpell(c,id,rank);
        if(!s || !s->passive || !s->unsupportedReason.empty() || !s->passiveTotalStatPct[stat] ||
           !(s->allowableClasses&(1u<<(p.classId-1))) || !localTalentPrerequisitesReady(p,c,*s))continue;
        multiplier*=1+float(s->passiveTotalStatPct[stat])/100;
    }
    return multiplier;
}
inline int32_t localTalentTotalStat(const LocalRealmPlayer& p,const LocalWorldContent& c,size_t stat,int32_t value) {
    // Keep the source's float multiplier followed by integer truncation,
    // including its boundary behavior (50 * 1.06f becomes 52, not 53).
    return int32_t(std::clamp(float(value)*localTalentTotalStatMultiplier(p,c,stat),0.f,1000000.f));
}
inline uint32_t localTalentSpellCritPct(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    if(p.dead || p.classId<1 || p.classId>11 || !validLocalTalents(p))return 0;
    uint32_t percent=0;
    for(auto [id,rank]:p.talents) {
        const auto* s=localTalentSpell(c,id,rank);
        if(!s || !s->passive || !s->unsupportedReason.empty() || !s->passiveSpellCritPct ||
           !(s->allowableClasses&(1u<<(p.classId-1))) || !localTalentPrerequisitesReady(p,c,*s))continue;
        percent=std::min(100u,percent+uint32_t(s->passiveSpellCritPct));
    }
    return percent;
}
inline uint32_t localTalentBonus(const LocalRealmPlayer& p,const LocalWorldContent& c,bool armor){
    uint64_t value=0;for(auto [id,rank]:p.talents)if(const auto* s=localTalentSpell(c,id,rank);s && s->unsupportedReason.empty())value+=armor?s->passiveArmor:s->passiveHealth;
    return uint32_t(std::min(uint64_t(100000),value));
}
// Only the learned rank contributes. Family identity AND intersection with
// any of the three mask words are required; matching bit numbers in another
// class family cannot grant protection. Modifier 9 percentages add, capped at 100.
inline uint32_t localTalentPushbackReduction(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& cast) {
    if(p.dead || p.classId<1 || p.classId>11 || !cast.spellFamily)return 0;
    uint32_t percent=0;
    for(auto [id,rank]:p.talents) {
        const auto* s=localTalentSpell(c,id,rank);
        if(!s || !s->passive || !s->unsupportedReason.empty() || !s->passivePushbackPct ||
           !(s->allowableClasses&(1u<<(p.classId-1))) || s->spellFamily!=cast.spellFamily)continue;
        bool matches=false;
        for(unsigned k=0;k<3;++k)matches=matches || (s->pushbackSpellMask[k]&cast.spellFamilyFlags[k]);
        if(matches)percent=std::min(100u,percent+uint32_t(s->passivePushbackPct));
    }
    return percent;
}
// Unconditional positive amount/range and negative cast/cost/recovery modifiers.
// Only the current learned rank contributes; reset needs no cache flush.
inline int32_t localTalentCastModifier(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                      const LocalSpellDefinition& cast,uint8_t operation,bool percentage=false) {
    if(p.dead || p.classId<1 || p.classId>11 || !cast.spellFamily || !validLocalTalents(p))return 0;
    int64_t amount=0;
    for(auto [id,rank]:p.talents) {
        const auto* s=localTalentSpell(c,id,rank);
        if(!s || !s->passive || !s->unsupportedReason.empty() ||
           !(s->allowableClasses&(1u<<(p.classId-1))) || s->spellFamily!=cast.spellFamily || !localTalentPrerequisitesReady(p,c,*s))continue;
        for(const auto& mod:s->passiveCastModifiers)if(mod.active && mod.operation==operation && mod.percentage==percentage &&
            // Op 16 (SPELLMOD_RESIST_MISS_CHANCE, Object.cpp:3585-3586) raises the
            // caster's hit chance, so its beneficial direction is positive like
            // the others listed here. 0 accepted producers (audit D12).
            ((operation==1)?mod.amount!=0:
             (operation==0||operation==3||operation==5||operation==7||operation==8||operation==12||operation==16||operation==22||operation==23)?mod.amount>0:mod.amount<0)) {
            bool match=false;
            for(unsigned k=0;k<3;++k)match=match || (mod.mask[k]&cast.spellFamilyFlags[k]);
            if(match)amount+=mod.amount;
        }
    }
    if(operation==0||operation==3||operation==8||operation==12||operation==22||operation==23)
        return int32_t(std::clamp(amount,int64_t(0),percentage?int64_t(1000):int64_t(1000000)));
    if(operation==5||operation==7||operation==16)
        return int32_t(std::clamp(amount,int64_t(0),percentage?int64_t(1000):int64_t(100)));
    if(operation==1)
        return int32_t(std::clamp(amount,percentage?int64_t(-100):int64_t(-600000),percentage?int64_t(1000):int64_t(600000)));
    return int32_t(std::clamp(amount,percentage?int64_t(-100):operation==11?int64_t(-3600000):int64_t(-60000),int64_t(0)));
}
inline uint32_t localSpellDuration(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& cast) {
    if(!cast.durationMs)return 0;
    const auto flat=localTalentCastModifier(p,c,cast,1,false);
    const auto pct=localTalentCastModifier(p,c,cast,1,true);
    const int64_t adjusted=std::max(int64_t(0),int64_t(cast.durationMs)+flat);
    return uint32_t(std::min(int64_t(600000),adjusted*(100+pct)/100));
}
inline uint32_t localSpellGlobalCooldown(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& cast) {
    if(!cast.globalCooldownMs)return 0;
    const auto flat=localTalentCastModifier(p,c,cast,21,false);
    const auto pct=localTalentCastModifier(p,c,cast,21,true);
    const int64_t adjusted=std::max(int64_t(0),int64_t(cast.globalCooldownMs)+flat);
    return uint32_t(std::min(int64_t(60000),adjusted*(100+pct)/100));
}
inline uint32_t localSpellCastTime(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& cast) {
    return uint32_t(std::max(int64_t(0),int64_t(cast.castTimeMs)+localTalentCastModifier(p,c,cast,10)));
}
inline uint32_t localChargedSpellCost(const LocalRealmPlayer&,const LocalWorldContent&,const LocalSpellDefinition&,uint32_t,uint32_t* = nullptr);
inline uint32_t localSpellBaseResourceCost(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& cast) {
    if(cast.resourceType==5)return 0; // Rune timers are separate from numeric resources.
    const uint64_t base=uint64_t(cast.mana)+uint64_t(cast.resourceType==0?localBaseMana(p):p.maxMana)*cast.manaPercent/100;
    const auto percent=100+localTalentCastModifier(p,c,cast,14,true)+localArcaneBlastCostPct(p,c,cast);
    // Source ApplySpellMod(COST) scales the base before adding flat modifiers.
    // DBC rage/runic costs and their flat modifiers use tenths; the local pool
    // stores displayed units. Energy and mana modifiers already use whole units.
    const int32_t unit=(cast.resourceType==1||cast.resourceType==6)?10:1;
    const int64_t raw=int64_t(base)*unit*percent/100+localTalentCastModifier(p,c,cast,14);
    return uint32_t(std::clamp((std::max(int64_t(0),raw)+unit-1)/unit,int64_t(0),int64_t(1000000000)));
}
inline uint32_t localSpellResourceCost(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& cast) {
    return localChargedSpellCost(p,c,cast,localSpellBaseResourceCost(p,c,cast));
}
inline bool learnLocalTalent(LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t id,uint32_t requestedRank,std::string& error){
    auto reject=[&](const std::string& why){error=why;return false;};
    if(requestedRank>4 || !localTalentPointsAvailable(p))return reject("No available talent points or invalid rank");
    const auto* s=localTalentSpell(c,id,uint8_t(requestedRank+1));
    if(!s || p.classId<1 || p.classId>11 || !(s->allowableClasses&(1u<<(p.classId-1))))return reject("Talent does not belong to this class");
    if(!s->unsupportedReason.empty())return reject("Talent unavailable: "+s->unsupportedReason);
    unsigned rank=0,treePoints=0;
    for(auto [other,n]:p.talents){if(other==id)rank=n;if(const auto* v=localTalentSpell(c,other,n);v && v->talentTab==s->talentTab)treePoints+=n;}
    if(rank!=requestedRank || treePoints<unsigned(s->talentRow)*5)return reject("Spend points in earlier tiers first");
    for(size_t i=0;i<3;++i)if(s->talentPrerequisites[i]){
        auto found=std::find_if(p.talents.begin(),p.talents.end(),[&](const auto& t){return t.first==s->talentPrerequisites[i];});
        if(found==p.talents.end() || found->second<s->talentPrerequisiteRanks[i]+1)return reject("Required talent rank is missing");
    }
    auto candidate=p;
    if(const auto* prior=localTalentSpell(c,id,uint8_t(rank)))std::erase(candidate.knownSpells,prior->id);
    if(!s->passive){if(candidate.knownSpells.size()>=LocalGameplay::MaxSpells)return reject("Spellbook is full");candidate.knownSpells.push_back(s->id);}
    auto found=std::find_if(candidate.talents.begin(),candidate.talents.end(),[&](const auto& t){return t.first==id;});
    if(found==candidate.talents.end())candidate.talents.emplace_back(id,uint8_t(rank+1));else found->second=uint8_t(rank+1);
    std::sort(candidate.talents.begin(),candidate.talents.end());p=std::move(candidate);error="Talent learned: "+s->name;return true;
}
}

#include "game/local_clearcasting.hpp"
