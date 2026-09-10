#pragma once
#include "game/local_gameplay.hpp"
namespace wowee::game {
inline bool validLocalStatAuras(const LocalRealmPlayer& p) {
    if(p.statAuras.size()>kLocalMaxStatAuras)return false;
    for(size_t i=0;i<p.statAuras.size();++i){const auto& a=p.statAuras[i];
        if(!a.spellId||!a.remainingMs||a.remainingMs>3600000||a.absorbRemaining>1000000)return false;
        for(size_t j=0;j<i;++j)if(p.statAuras[j].spellId==a.spellId)return false;
    }return true;
}
inline uint32_t localStatAuraBonus(const LocalRealmPlayer& p,const LocalWorldContent& c,bool armor){
    uint64_t value=0;
    if(p.dead)return 0;
    for(const auto& a:p.statAuras)if(a.remainingMs&&a.mapId==p.mapId&&a.instanceId==p.instanceId)
        if(const auto* d=c.spell(a.spellId);d&&d->unsupportedReason.empty()&&!d->passive)
            value+=armor?d->buffArmor:d->buffHealth;
    return uint32_t(std::min(value,uint64_t(1000000)));
}
// Damage enters after armor. Distinct eligible shields consume in application order.
inline uint32_t localAbsorbDamage(LocalRealmPlayer& p,const LocalWorldContent& c,uint32_t damage,uint32_t school){
    if(p.dead)return damage;
    for(auto& a:p.statAuras){
        if(!damage)break;
        if(!a.remainingMs||a.mapId!=p.mapId||a.instanceId!=p.instanceId)continue;
        const auto* d=c.spell(a.spellId);
        if(!d||!d->unsupportedReason.empty()||!d->buffAbsorb||!(d->absorbSchoolMask&school))continue;
        a.absorbRemaining=std::min(a.absorbRemaining,d->buffAbsorb);
        const auto absorbed=std::min(damage,a.absorbRemaining);damage-=absorbed;a.absorbRemaining-=absorbed;
        if(!a.absorbRemaining)a.remainingMs=0;
    }
    std::erase_if(p.statAuras,[](const auto& a){return !a.remainingMs;});
    return damage;
}
}
