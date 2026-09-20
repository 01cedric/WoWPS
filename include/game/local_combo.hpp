#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_melee.hpp"
#include <algorithm>
#include <cmath>
namespace wowee::game {
enum class LocalComboProfile : uint8_t { None, SinisterStrike, Backstab, Claw, Shred, Rake, Eviscerate, Rip, FerociousBite };
inline LocalComboProfile localComboProfile(uint32_t id) {
    // Player rank identities, not name matching or broad family acceptance.
    switch(id) {
    case 1752:case 1757:case 1758:case 1759:case 1760:case 8621:case 11293:case 11294:case 26861:case 26862:case 48637:case 48638:return LocalComboProfile::SinisterStrike;
    case 53:case 2589:case 2590:case 2591:case 8721:case 11279:case 11280:case 11281:case 25300:case 26863:case 48656:case 48657:return LocalComboProfile::Backstab;
    case 1082:case 3029:case 5201:case 9849:case 9850:case 27000:case 48569:case 48570:return LocalComboProfile::Claw;
    case 5221:case 6800:case 8992:case 9829:case 9830:case 27001:case 27002:case 48571:case 48572:return LocalComboProfile::Shred;
    case 1822:case 1823:case 1824:case 9904:case 27003:case 48573:case 48574:return LocalComboProfile::Rake;
    case 2098:case 6760:case 6761:case 6762:case 8623:case 8624:case 11299:case 11300:case 26865:case 31016:case 48667:case 48668:return LocalComboProfile::Eviscerate;
    case 1079:case 9492:case 9493:case 9752:case 9894:case 9896:case 27008:case 49799:case 49800:return LocalComboProfile::Rip;
    case 22568:case 22827:case 22828:case 22829:case 24248:case 31018:case 48576:case 48577:return LocalComboProfile::FerociousBite;
    default:return LocalComboProfile::None;
    }
}
inline void clearLocalCombo(LocalRealmPlayer& p){p.comboPoints=0;p.comboTarget=0;p.comboTargetEpoch=0;p.comboPositionRevision=0;}
inline bool validLocalComboView(const LocalRealmPlayer& p){return p.comboPoints<=5 && (p.comboPoints? p.comboTarget!=0 && (p.classId==4||p.classId==11) && !p.dead : p.comboTarget==0);}
inline bool localComboTargetValid(const LocalRealmPlayer& p,const LocalRealmNpc* n){
    return validLocalComboView(p)&&p.comboPoints&&n&&!n->dead&&n->health&&n->guid==p.comboTarget&&n->combatEpoch==p.comboTargetEpoch&&
        p.health&&!p.flight.active&&!p.transportEntry&&p.mapId==n->mapId&&p.instanceId==n->instanceId&&p.positionRevision==p.comboPositionRevision;
}
inline bool localComboFacingReady(const LocalRealmPlayer& p,const LocalRealmNpc& n,bool behind){
    const double dx=n.x-p.x,dy=n.y-p.y;
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(p.orientation)||!std::isfinite(n.orientation))return false;
    if(dx*dx+dy*dy<0.0001)return !behind;
    // All reviewed attacks face the victim; Backstab and Shred also require its rear half.
    if(dx*std::cos(p.orientation)+dy*std::sin(p.orientation)<0)return false;
    return !behind||dx*std::cos(n.orientation)+dy*std::sin(n.orientation)>0;
}
inline void addLocalCombo(LocalRealmPlayer& p,const LocalRealmNpc& n,uint8_t gain){
    const auto previous=localComboTargetValid(p,&n)?p.comboPoints:0;
    p.comboTarget=n.guid;p.comboTargetEpoch=n.combatEpoch;p.comboPositionRevision=p.positionRevision;
    p.comboPoints=uint8_t(std::min(5u,unsigned(previous)+gain));
}
// The physical combat core owns attributes, AP and equipped-weapon normalization.
inline uint32_t localComboAttackRating(const LocalRealmPlayer& p,const LocalWorldContent& c){return uint32_t(localMeleeStats(p,c).attackPower);}
inline uint32_t localComboAmount(const LocalRealmPlayer& p,const LocalWorldContent& c,const LocalSpellDefinition& s,uint32_t base,uint8_t points,uint32_t extraEnergy,bool periodic,bool sourceComboIncluded=false){
    if(!s.comboProfile)return base;
    const auto profile=LocalComboProfile(s.comboProfile);const double ap=localComboAttackRating(p,c);
    double amount=base;
    if(!periodic&&s.weaponDamage){const auto w=localWeaponAmounts(p,c,false,s.normalizedWeapon,false);amount=(amount+(w.low+w.high+w.magicLow+w.magicHigh)*.5)*s.weaponPercent/100.0;}
    if(!sourceComboIncluded)amount+=(periodic?s.periodicPerCombo:s.directPerCombo)*points;
    if(profile==LocalComboProfile::Rake)amount+=ap*(periodic?0.06:0.01);
    if(periodic&&profile==LocalComboProfile::Rip)amount+=ap*points*0.01;
    if(!periodic&&(profile==LocalComboProfile::Eviscerate||profile==LocalComboProfile::FerociousBite))amount+=ap*points*0.07;
    if(!periodic&&profile==LocalComboProfile::FerociousBite)amount+=extraEnergy*(s.extraEnergyMultiplier+ap/410.0);
    return uint32_t(std::clamp(amount,0.0,1000000.0));
}
}
