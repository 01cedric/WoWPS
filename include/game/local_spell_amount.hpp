#pragma once
#include "game/local_talents.hpp"
namespace wowee::game {
// Pinned Player::ApplySpellMod: amount percentages multiply for DAMAGE/DOT,
// but add for ALL_EFFECTS/EFFECT1/EFFECT2/EFFECT3. Flat adds after scaling.
// Keep float intermediates, matching source ApplyEffectModifiers, and truncate
// only when a concrete local integer amount is produced.
inline float localSpellAmountModifier(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                      const LocalSpellDefinition& cast,float base,uint8_t operation) {
    base=std::clamp(base,0.f,1000000.f);
    if(p.dead || p.classId<1 || p.classId>11 || !cast.spellFamily || !validLocalTalents(p))return base;
    if(operation!=0&&operation!=3&&operation!=8&&operation!=12&&operation!=22&&operation!=23)return base;
    float multiplier=1.f;
    int64_t flat=0;
    for(auto [id,rank]:p.talents) {
        const auto* talent=localTalentSpell(c,id,rank);
        if(!talent || !talent->passive || !talent->unsupportedReason.empty() ||
           !(talent->allowableClasses&(1u<<(p.classId-1))) || talent->spellFamily!=cast.spellFamily ||
           !localTalentPrerequisitesReady(p,c,*talent))continue;
        for(const auto& mod:talent->passiveCastModifiers) {
            if(!mod.active || mod.operation!=operation || mod.amount<=0)continue;
            bool match=false;
            for(unsigned k=0;k<3;++k)match=match || (mod.mask[k]&cast.spellFamilyFlags[k]);
            if(!match)continue;
            if(!mod.percentage)flat+=mod.amount;
            else if(base!=0.f) {
                if(operation==0||operation==22)multiplier*=float(100+int64_t(mod.amount))/100.f;
                else multiplier+=float(mod.amount)/100.f;
            }
        }
    }
    return std::clamp(base*multiplier+float(flat),0.f,1000000.f);
}
// DBC points-per-combo enter before effect modifiers (SpellEffectInfo::CalcValue).
// Weapon and class-specific AP bonuses follow, then the DAMAGE/DOT stage. Slot 255 denotes absent or combined effects, never EFFECT1 by default.
// Generic source admission keeps slot-specific talents away from aggregates.
inline uint32_t localSpellEffectAmountAfterTalents(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                                  const LocalSpellDefinition& d,uint32_t base,bool periodic,uint8_t comboPoints=0) {
    const float sourceAmount=std::clamp(float(base)+(periodic?d.periodicPerCombo:d.directPerCombo)*std::min(uint8_t(5),comboPoints),0.f,1000000.f);
    const auto slot=periodic?d.periodicEffectSlot:d.directEffectSlot;
    if(slot>2)return uint32_t(sourceAmount);
    const auto all=localSpellAmountModifier(p,c,d,sourceAmount,8);
    constexpr uint8_t operations[]={3,12,23};
    return uint32_t(localSpellAmountModifier(p,c,d,all,operations[slot]));
}
// Direct damage/healing uses operation 0; periodic damage/healing uses 22.
// Periodic callers snapshot this per-stack amount when the aura is applied.
inline uint32_t localSpellAmountAfterTalents(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                            const LocalSpellDefinition& d,uint32_t base,bool periodic) {
    return uint32_t(localSpellAmountModifier(p,c,d,float(base),periodic?22:0));
}
}
