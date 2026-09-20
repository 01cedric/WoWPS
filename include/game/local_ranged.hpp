#pragma once
#include "game/local_melee.hpp"
#include "game/local_forms.hpp"
#include "game/local_talents.hpp"
#include "game/local_progression_modifiers.hpp"
#include "game/local_reactive_talents.hpp"
#include "game/local_spell_equipment.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace wowee::game {
namespace local_ranged_detail {
struct Item {uint32_t id;int32_t attackPower,hit,haste,spellHit;};
inline constexpr Item items[]={
#include "game/local_ranged_items_generated.inc"
};
struct Ammo {uint32_t id,subclass;float dps;uint32_t level;};
inline constexpr Ammo ammo[]={
#include "game/local_ranged_ammo_generated.inc"
};
struct Ratio {uint8_t clazz,level;float hit,haste,spellHit;};
inline constexpr Ratio ratios[]={
#include "game/local_ranged_ratios_generated.inc"
};
template<class T,size_t N> const T* find(const T(&rows)[N],uint32_t id) {
    const auto it=std::lower_bound(std::begin(rows),std::end(rows),id,[](const T& a,uint32_t b){return a.id<b;});
    return it!=std::end(rows)&&it->id==id?it:nullptr;
}
inline const LocalMeleeItem* worn(const LocalRealmPlayer& p,const LocalWorldContent& c,size_t slot) {
    const auto id=p.equipment[slot];const auto* d=c.item(id);const auto* item=localMeleeItem(id);
    if(!d||!item||item->scaling||!localEquipmentFits(d->inventoryType,d->slot,slot))return nullptr;
    uint64_t copies=0;for(const auto& s:p.inventory)if(s.itemId==id)copies+=s.count;
    if(uint64_t(std::count(p.equipment.begin(),p.equipment.begin()+slot+1,id))>copies)return nullptr;
    return item;
}
}
/// Unit::m_modSpellHitChance, StatSystem.cpp:885-886: the total of
/// SPELL_AURA_MOD_SPELL_HIT_CHANCE (55) and GetRatingBonusValue(CR_HIT_SPELL).
/// The rating half is the same `spellHit` item column and the same class/level
/// `Ratio` row the wand shot has summed since the implementation (2,520 of 5,650 items carry
/// one; 800 ratio rows); previously the cast roll read neither, and
/// WorldObject::MagicSpellHitResult adds it to every magic hit
/// (Object.cpp:3606-3615). Aura 55 has zero accepted producers (13 castable
/// rows, none imported; audit D12), so the aura half is a documented zero
/// rather than an invented container.
inline float localSpellHitRatingPercent(const LocalRealmPlayer& p,const LocalWorldContent& c) {
    if(p.classId<1||p.classId>11||!p.level||p.level>80)return 0;
    const uint32_t key=uint32_t(p.classId)*100+p.level;
    const auto ratio=std::lower_bound(std::begin(local_ranged_detail::ratios),std::end(local_ranged_detail::ratios),key,
        [](const auto& a,uint32_t b){return uint32_t(a.clazz)*100+a.level<b;});
    if(ratio==std::end(local_ranged_detail::ratios)||uint32_t(ratio->clazz)*100+ratio->level!=key)return 0;
    int64_t spellHitRating=0;
    for(size_t slot=0;slot<p.equipment.size();++slot)if(const auto* item=local_ranged_detail::worn(p,c,slot))
        if(const auto* bonus=local_ranged_detail::find(local_ranged_detail::items,item->id))spellHitRating+=bonus->spellHit;
    if(spellHitRating<=0)return 0;
    return std::clamp(float(spellHitRating)*ratio->spellHit,0.f,100.f);
}
struct LocalRangedAmounts {
    bool active=false;
    uint32_t ammoId=0,schoolMask=1,basePeriodMs=0,periodMs=0;
    float low=0,high=0,hit=0,crit=0;
};
inline bool localRangedAutoSpell(const LocalSpellDefinition& d) {
    return d.clientSpell&&((d.id==75&&d.rangedAutoProfile==1)||(d.id==5019&&d.rangedAutoProfile==2));
}
inline LocalRangedAmounts localRangedAmounts(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& d) {
    LocalRangedAmounts a;
    if(!localRangedAutoSpell(d)||p.classId<1||p.classId>11||
       (d.allowableClasses&&!(d.allowableClasses&(1u<<(p.classId-1))))||p.dead||!p.health||p.flight.active||p.transportEntry||p.mountSpellId||
       !localSpellEquipmentReady(p,c,d)||!localFormEnvironmentReady(p,d))return a;
    const auto* form=localActiveForm(p);
    if(form&&(form->form==1||form->form==5||form->form==8||form->form==16))return a;
    const auto* weapon=local_ranged_detail::worn(p,c,17);
    if(!weapon||weapon->itemClass!=2||!weapon->delay||weapon->delay>60000||weapon->school[0]>6||
       weapon->damage[0]>weapon->damage[1]||weapon->damage[2]||weapon->damage[3])return a;
    const bool wand=d.rangedAutoProfile==2;
    if(wand?weapon->subclass!=19:(weapon->subclass!=2&&weapon->subclass!=3&&weapon->subclass!=18))return a;
    float ammoDps=0;
    if(!wand) {
        const uint32_t subclass=weapon->subclass==3?3:2;
        // Local inventory selection: first compatible owned stack. Damage and
        // consumption always use that exact source projectile, never free ammo.
        for(const auto& s:p.inventory)if(s.count&&c.item(s.itemId)) {
            const auto* ammo=local_ranged_detail::find(local_ranged_detail::ammo,s.itemId);
            if(ammo&&ammo->subclass==subclass&&p.level>=ammo->level){a.ammoId=ammo->id;ammoDps=ammo->dps;break;}
        }
        if(!a.ammoId)return a;
    }
    const auto stats=localMeleeStats(p,c);if(!stats.sourceStats||p.level>80||!p.level)return a;
    const uint32_t key=p.classId*100+p.level;
    const auto ratio=std::lower_bound(std::begin(local_ranged_detail::ratios),std::end(local_ranged_detail::ratios),key,
        [](const auto& a,uint32_t b){return uint32_t(a.clazz)*100+a.level<b;});
    if(ratio==std::end(local_ranged_detail::ratios)||uint32_t(ratio->clazz)*100+ratio->level!=key)return a;
    int64_t itemAp=0,hitRating=0,hasteRating=0,spellHitRating=0;
    for(size_t slot=0;slot<p.equipment.size();++slot)if(const auto* item=local_ranged_detail::worn(p,c,slot))
        if(const auto* bonus=local_ranged_detail::find(local_ranged_detail::items,item->id)){
            itemAp+=bonus->attackPower;hitRating+=bonus->hit;hasteRating+=bonus->haste;spellHitRating+=bonus->spellHit;
        }
    const auto level=uint32_t(p.level);
    const float baseAp=float(stats.attributes[1])-10.f+float(p.classId==3?2*level:(p.classId==1||p.classId==4)?level:0);
    const auto ap=std::clamp(double(baseAp)+itemAp,0.0,1000000.0);
    a.basePeriodMs=weapon->delay;
    const float haste=std::clamp(float(hasteRating)*ratio->haste,0.f,1000.f);
    a.periodMs=uint32_t(std::clamp(weapon->delay/(1.f+haste/100.f),1.f,60000.f));
    a.schoolMask=wand?1u<<weapon->school[0]:d.schoolMask;
    const float seconds=weapon->delay/1000.f;
    const float bonus=float(ap/14)*seconds+ammoDps*seconds;
    a.low=weapon->damage[0]+bonus;a.high=weapon->damage[1]+bonus;
    if(a.schoolMask&1) {
        const float factor=localTalentPhysicalDamageMultiplier(p,c)*localTimedDamageMultiplier(p,c,false)*
            localTalentWeaponDamageMultiplier(p,c,weapon->itemClass,weapon->subclass,weapon->inventoryType);
        a.low*=factor;a.high*=factor;
    }
    a.hit=wand?std::clamp(float(spellHitRating)*ratio->spellHit,0.f,100.f):
        std::clamp(float(hitRating)*ratio->hit+stats.talentWeaponHitPct,0.f,100.f);
    a.crit=d.sourceCantCrit?0:wand?localSpellCritChance(p,c,a.schoolMask):localRangedCritChance(p,c);
    a.active=true;return a;
}
inline void localStopRangedAuto(LocalRealmPlayer& p) {
    p.rangedAutoSpellId=0;p.rangedTarget=0;p.rangedWeapon=0;
    // Preserve ranged recovery, as StopAttack does not reset GetAttackTime.
}
inline bool localRangedPositionChanged(const LocalRealmPlayer& p) {
    const auto dx=p.x-p.rangedOriginX,dy=p.y-p.rangedOriginY,dz=p.z-p.rangedOriginZ;
    return dx*dx+dy*dy+dz*dz>.0001f;
}
inline void localRangedRememberPosition(LocalRealmPlayer& p) {
    p.rangedOriginX=p.x;p.rangedOriginY=p.y;p.rangedOriginZ=p.z;p.rangedOriginRevision=p.positionRevision;
}
inline LocalMeleeOutcome localRollRanged(const LocalRealmPlayer& p,const LocalRealmNpc& n,
                                      const LocalRangedAmounts& a,uint32_t missRoll,uint32_t critRoll,bool wand=false) {
    const int diff=(int(n.level)-int(p.level))*5;
    const int levelDiff=int(n.level)-int(p.level);
    const float miss=wand?std::clamp((levelDiff<3?4.f+levelDiff:6.f+(levelDiff-2)*11.f)-a.hit,0.f,99.f):
        std::clamp(5.f+(diff>10?1.f+(diff-10)*.4f:diff*.1f)-a.hit,0.f,60.f);
    if((wand?missRoll+1:missRoll)<miss*100)return LocalMeleeOutcome::Miss;
    const float crit=std::clamp(a.crit-(wand?0.f:diff*.04f),0.f,100.f);
    return critRoll<crit*100?LocalMeleeOutcome::Critical:LocalMeleeOutcome::Hit;
}
}
