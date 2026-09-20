#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_talents.hpp"
namespace wowee::game {
inline uint32_t localSpellRecoveryDuration(const LocalRealmPlayer& p,const LocalWorldContent& c,
                                         const LocalSpellDefinition& d,bool category) {
    const auto base=category?d.categoryCooldownMs:d.cooldownMs;
    if(!base || (category&&d.noCategoryCooldownMods))return base;
    const auto flat=localTalentCastModifier(p,c,d,11,false);
    const auto percent=localTalentCastModifier(p,c,d,11,true);
    const int64_t reduced=std::max(int64_t(0),int64_t(base)+flat);
    return uint32_t(std::min(int64_t(3600000),reduced*(100+percent)/100));
}
inline bool validLocalCategoryCooldowns(const LocalRealmPlayer& p) {
    if(p.categoryCooldowns.size()>kLocalMaxCategoryCooldowns ||
       (p.migrateLegacyCooldowns&&!p.categoryCooldowns.empty()))return false;
    for(size_t i=0;i<p.categoryCooldowns.size();++i) {
        const auto& a=p.categoryCooldowns[i];
        if(!a.category||a.category>100000||a.family>1000||a.remainingMs>3600000)return false;
        for(size_t j=0;j<i;++j)if(p.categoryCooldowns[j].category==a.category&&p.categoryCooldowns[j].family==a.family)return false;
    }
    return true;
}
inline uint32_t localSpellCooldownRemaining(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& d) {
    uint32_t result=0;
    for(const auto& a:p.cooldowns) {
        if(a.spellId==d.id)result=std::max(result,a.remainingMs);
        if(p.migrateLegacyCooldowns&&d.cooldownCategory)if(const auto* old=c.spell(a.spellId))
            if(old->cooldownCategory==d.cooldownCategory&&old->spellFamily==d.spellFamily)
                result=std::max(result,a.remainingMs);
    }
    if(d.cooldownCategory)for(const auto& a:p.categoryCooldowns)
        if(a.category==d.cooldownCategory&&a.family==d.spellFamily)result=std::max(result,a.remainingMs);
    return result;
}
inline bool migrateLocalCategoryCooldowns(LocalRealmPlayer& p,const LocalWorldContent& c) {
    if(!p.migrateLegacyCooldowns||!c.talentIndexReady)return false;
    std::vector<LocalCategoryCooldown> migrated;migrated.reserve(kLocalMaxCategoryCooldowns);
    for(const auto& a:p.cooldowns)if(a.remainingMs)if(const auto* d=c.spell(a.spellId);d&&d->cooldownCategory&&d->categoryCooldownMs) {
        auto found=std::find_if(migrated.begin(),migrated.end(),[&](const auto& x){return x.category==d->cooldownCategory&&x.family==d->spellFamily;});
        if(found!=migrated.end())found->remainingMs=std::max(found->remainingMs,a.remainingMs);
        else {if(migrated.size()>=kLocalMaxCategoryCooldowns)return false;migrated.push_back({d->cooldownCategory,d->spellFamily,a.remainingMs});}
    }
    // Old saves recorded max(spell,category) as one timer. Preserve that
    // remaining interval conservatively; never restart the full base duration.
    p.categoryCooldowns=std::move(migrated);p.migrateLegacyCooldowns=false;return true;
}
}
