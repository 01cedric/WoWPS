#pragma once
#include "game/local_gameplay.hpp"
namespace wowee::game {
inline size_t localOwnerAuraCount(const LocalRealmPlayer& p){return p.statAuras.size()+p.healingAuras.size();}
template<class Content>
LocalHealingAuraView localOwnerAuraAt(const LocalRealmPlayer& p,const Content& c,size_t i){
    if(i>=p.statAuras.size())return p.healingAuras.at(i-p.statAuras.size());
    const auto& a=p.statAuras[i];const auto* d=c.spell(a.spellId);
    return {a.spellId,a.remainingMs,d?d->durationMs:a.remainingMs,a.casterGuid?a.casterGuid:p.guid};
}
inline bool validLocalHealingAuraViews(const LocalRealmPlayer& p){
    if(p.healingAuras.size()>kLocalMaxHealingAuraViews)return false;
    for(size_t i=0;i<p.healingAuras.size();++i){const auto& a=p.healingAuras[i];
        if(!a.spellId||!a.casterGuid||!a.remainingMs||a.remainingMs>a.durationMs||a.durationMs>600000)return false;
        for(size_t j=0;j<i;++j)if(p.healingAuras[j].spellId==a.spellId&&p.healingAuras[j].casterGuid==a.casterGuid)return false;
    }return true;
}
// Shared by the original UI command and focused host tests.
inline uint64_t localSpellCommandTarget(const LocalSpellDefinition& s,const LocalRealmPlayer& self,
        uint64_t selected,const std::vector<LocalRealmPlayer>& players){
    if(s.damage||s.periodicDamage)return selected;
    const bool friendly=((s.heal||s.periodicHeal)&&!s.healingSelfOnly)||
        ((s.buffHealth||s.buffArmor||s.buffAbsorb)&&!s.buffSelfOnly);
    if(friendly&&std::any_of(players.begin(),players.end(),[&](const auto& p){return p.guid==selected;}))return selected;
    return self.guid;
}
}
