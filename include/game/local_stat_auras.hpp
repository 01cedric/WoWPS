#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_proc_rules.hpp"
#include "game/local_ward_ids.hpp"
namespace wowee::game {
inline uint8_t nextLocalAuraStack(uint32_t oldSpell,uint8_t oldStacks,const LocalSpellDefinition& d) {
    return uint8_t(std::min(unsigned(std::max(uint8_t(1),d.maxAuraStacks)),
        oldSpell==d.id?unsigned(oldStacks)+1:1u));
}
inline uint32_t localStackedAuraAmount(uint32_t amount,uint8_t stacks) {
    return uint32_t(std::min(uint64_t(amount)*stacks,uint64_t(1000000)));
}
inline bool validLocalStatAuras(const LocalRealmPlayer& p) {
    if(p.statAuras.size()>kLocalMaxStatAuras)return false;
    for(size_t i=0;i<p.statAuras.size();++i){const auto& a=p.statAuras[i];
        if(!a.stacks||!a.spellId||!a.remainingMs||a.remainingMs>3600000||a.absorbRemaining>1000000||
           a.procCooldownMs>60000||a.manaRegenRemainder>=5000||a.procAmountSnapshot>1000000||a.buffArmorSnapshot>1000000||
           (a.hasProcAmountSnapshot!=(a.procAmountSnapshot!=0))||
           a.reflectChanceBasisPointsSnapshot>10000||
           (a.reflectChanceBasisPointsSnapshot&&!localWardIdProfile(a.spellId)))return false;
        for(size_t j=0;j<i;++j)if(p.statAuras[j].spellId==a.spellId)return false;
    }return true;
}
inline uint32_t localStatAuraBonus(const LocalRealmPlayer& p,const LocalWorldContent& c,bool armor){
    uint64_t value=0;
    if(p.dead)return 0;
    for(const auto& a:p.statAuras)if(a.remainingMs&&a.mapId==p.mapId&&a.instanceId==p.instanceId)
        if(const auto* d=c.spell(a.spellId);d&&d->unsupportedReason.empty()&&!d->passive)
            value+=localStackedAuraAmount(armor?(d->buffArmor?(a.buffArmorSnapshot?std::min(a.buffArmorSnapshot,uint32_t(std::min(uint64_t(1000000),uint64_t(d->buffArmor)*11))):d->buffArmor):0):d->buffHealth,std::min(a.stacks,d->maxAuraStacks));
    return uint32_t(std::min(value,uint64_t(1000000)));
}
// Damage enters after armor. Ordinary absorbs precede mana-powered absorbs,
// independently of application order. Order within each category stays stable.
inline uint32_t localAbsorbDamage(LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t damage,uint32_t school){
    if(p.dead)return damage;
    for(unsigned category=0;category<2&&damage;++category)for(auto& a:p.statAuras){
        if(!damage)break;
        if(!a.remainingMs||a.mapId!=p.mapId||a.instanceId!=p.instanceId)continue;
        const auto* d=c.spell(a.spellId);
        if(!d||!d->unsupportedReason.empty()||!validLocalProc(*d)||!d->buffAbsorb||!(d->absorbSchoolMask&school)||
           unsigned(d->manaPerAbsorbMilli!=0)!=category)continue;
        // Wards store the caster-scaled absorb amount at application time.
        // Clamping that snapshot to raw DBC base points would erase spell power.
        const auto absorbLimit=d->wardProfile?1000000u:
            localStackedAuraAmount(d->buffAbsorb,std::min(a.stacks,d->maxAuraStacks));
        a.absorbRemaining=std::min(a.absorbRemaining,absorbLimit);
        auto absorbed=std::min(damage,a.absorbRemaining);
        if(d->manaPerAbsorbMilli && absorbed) {
            if(p.resourceType!=LocalResourceType::Mana)continue;
            // The reference truncates mana cost per hit, then proportionally
            // reduces absorption when the available mana cannot pay that cost.
            const auto cost=uint32_t(uint64_t(absorbed)*d->manaPerAbsorbMilli/1000);
            const auto spent=std::min(cost,p.mana);
            absorbed=cost?uint32_t(uint64_t(absorbed)*spent/cost):0;
            p.mana-=spent;
        }
        damage-=absorbed;a.absorbRemaining-=absorbed;
        if(!a.absorbRemaining)a.remainingMs=0;
    }
    std::erase_if(p.statAuras,[](const auto& a){return !a.remainingMs;});
    return damage;
}
}
