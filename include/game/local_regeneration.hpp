#pragma once
#include "game/local_forms.hpp"
#include "game/local_regeneration_rates.hpp"
#include <algorithm>
#include <cmath>
namespace wowee::game {
inline void addLocalResourceCredit(uint32_t& amount,uint32_t capacity,uint32_t& remainder,uint64_t milliCredit){
    if(amount>=capacity){amount=capacity;remainder=0;return;}
    milliCredit+=remainder;
    amount=uint32_t(std::min(uint64_t(capacity),uint64_t(amount)+milliCredit/1000));
    remainder=amount==capacity?0:uint32_t(milliCredit%1000);
}
inline uint64_t localRegenMilliRate(double rate){
    return std::isfinite(rate)&&rate>0?uint64_t(std::llround(std::min(rate,1000000.0)*1000)):0;
}
inline void discardLocalFullRegenerationCredit(LocalRealmPlayer& p){
    if(p.mana>=p.maxMana)p.resourceRegenRemainder=0;
    const bool hiddenMana=p.classId==11&&p.formSpellId&&p.resourceType!=LocalResourceType::Mana;
    if(hiddenMana&&p.druidMana>=p.druidManaCapacity)p.druidManaRemainder=0;
    // The fine carry belongs to caster mana even while energy/rage is shown.
    // Item use, healing and pool recalculation can fill it between regen calls.
    const bool fullMana=hiddenMana?p.druidMana>=p.druidManaCapacity:
        p.resourceType==LocalResourceType::Mana&&p.mana>=p.maxMana;
    if(fullMana){
        p.manaRegenSubMilli=0;
        for(auto& aura:p.statAuras)aura.manaRegenRemainder=0;
    }
}
inline bool advanceLocalRegeneration(LocalRealmPlayer& p,uint32_t elapsedMs,bool combat,const LocalRegenerationRates& rates){
    const auto oldMana=p.mana,oldHealth=p.health,oldDelay=p.manaRegenDelayMs,oldRemainder=p.resourceRegenRemainder;
    const auto oldHidden=p.druidMana,oldHiddenRemainder=p.druidManaRemainder;
    if(p.dead||!p.health){
        p.manaRegenDelayMs=p.resourceRegenRemainder=p.druidManaRemainder=0;
        p.regenerationTickMs=p.manaRegenSubMilli=0;
        for(auto& aura:p.statAuras)aura.manaRegenRemainder=0;
        return oldDelay||oldRemainder||oldHiddenRemainder;
    }
    const bool hiddenMana=p.classId==11&&p.formSpellId&&p.resourceType!=LocalResourceType::Mana;
    const auto delayed=std::min(elapsedMs,p.manaRegenDelayMs);p.manaRegenDelayMs-=delayed;
    if(p.resourceType==LocalResourceType::Mana||hiddenMana){
        auto& amount=hiddenMana?p.druidMana:p.mana;
        auto& remainder=hiddenMana?p.druidManaRemainder:p.resourceRegenRemainder;
        // Rates have millipoint/second precision; retain sub-millipoints across
        // frames and form switches. Save24/25 thousandths keep their old units.
        const uint64_t microCredit=uint64_t(delayed)*localRegenMilliRate(rates.manaInterruptedPerSecond)+
            uint64_t(elapsedMs-delayed)*localRegenMilliRate(rates.manaPerSecond)+p.manaRegenSubMilli;
        p.manaRegenSubMilli=uint32_t(microCredit%1000);
        addLocalResourceCredit(amount,localManaCapacity(p),remainder,microCredit/1000);
        if(amount==localManaCapacity(p))p.manaRegenSubMilli=0;
    }else{p.manaRegenDelayMs=0;p.manaRegenSubMilli=0;}
    if(p.resourceType==LocalResourceType::Energy)
        addLocalResourceCredit(p.mana,p.maxMana,p.resourceRegenRemainder,uint64_t(elapsedMs)*10);
    else if(p.resourceType!=LocalResourceType::Mana)p.resourceRegenRemainder=0;
    const uint64_t tickMs=uint64_t(p.regenerationTickMs%2000)+elapsedMs;
    const auto ticks=tickMs/2000;p.regenerationTickMs=uint32_t(tickMs%2000);
    if(ticks){
        const double healthRate=rates.healthItemPerSecond+(combat?0:rates.healthSpiritPerSecond);
        const uint64_t healthTick=std::isfinite(healthRate)?uint64_t(std::clamp(healthRate*2,0.0,1000000.0)):0;
        p.health=uint32_t(std::min(uint64_t(p.maxHealth),uint64_t(p.health)+ticks*healthTick));
        if(!combat&&(p.resourceType==LocalResourceType::Rage||p.resourceType==LocalResourceType::RunicPower))
            p.mana-=uint32_t(std::min(uint64_t(p.mana),ticks*(p.resourceType==LocalResourceType::Rage?2:3)));
    }
    discardLocalFullRegenerationCredit(p);
    return oldHidden!=p.druidMana||oldHiddenRemainder!=p.druidManaRemainder||p.mana!=oldMana||p.health!=oldHealth||p.manaRegenDelayMs!=oldDelay||p.resourceRegenRemainder!=oldRemainder;
}
}
