#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_area_aura.hpp"
#include <cmath>
namespace wowee::game {
// The aura flag byte this build already speaks, as its own packet reader
// decodes it (src/game/world_packets_world.cpp:236-260): 0x01/0x02/0x04 name
// the effect indices the application carries, 0x08 NOT_CASTER, 0x20 DURATION,
// 0x80 harmful. The reference builds the same byte per application in
// AuraApplication::BuildUpdatePacket. Only the effect bits vary per owner row;
// every owner aura is a positive one with its caster written out.
inline constexpr uint8_t kLocalOwnerAuraEffectMask=0x07;
inline constexpr uint8_t kLocalOwnerAuraFlags=0x18;
/// One owner-facing aura row, in the shape the client's buff cache wants.
struct LocalOwnerAuraView {
    uint32_t spellId=0,remainingMs=0,durationMs=0;
    uint64_t casterGuid=0;
    uint8_t stacks=1,flags=0;
    bool operator==(const LocalOwnerAuraView&) const = default;
};
/// Derived area aura applications the owner presents. The authority already
/// bounds them; clamping here keeps a malformed snapshot from growing the
/// client's buff cache past kLocalMaxAreaAuraApplications.
inline size_t localOwnerAreaAuraCount(const LocalRealmPlayer& p){
    return std::min(p.areaAuras.size(),kLocalMaxAreaAuraApplications);
}
/// Stat auras, then healing views, then area aura applications. Area auras are
/// appended last on purpose: GetPlayerBuff hands the interface this index back
/// and asks four more questions with it (src/addons/lua_spell_api.cpp:368-394),
/// so an area aura arriving or going must not renumber the timed buffs.
inline size_t localOwnerHarmfulAuraCount(const LocalRealmPlayer& p){
    return std::min(p.harmfulAuras.size(),kLocalMaxHealingAuraViews);
}
inline size_t localOwnerAuraCount(const LocalRealmPlayer& p){
    return p.statAuras.size()+p.healingAuras.size()+localOwnerAreaAuraCount(p)+localOwnerHarmfulAuraCount(p);
}
/// Harmful rows: NOT_CASTER | NEGATIVE with the effect bits (0x88|0x07).
inline constexpr uint8_t kLocalOwnerHarmfulAuraFlags=0x88;
template<class Content>
LocalOwnerAuraView localOwnerAuraAt(const LocalRealmPlayer& p,const Content& c,size_t i){
    const size_t timed=p.statAuras.size()+p.healingAuras.size();
    // Creature debuffs come after area auras so buff indices never renumber.
    if(i>=timed+localOwnerAreaAuraCount(p)){
        const auto& h=p.harmfulAuras.at(i-timed-localOwnerAreaAuraCount(p));
        return {h.spellId,h.remainingMs,h.durationMs,h.casterGuid,h.stacks,
            uint8_t(kLocalOwnerHarmfulAuraFlags|kLocalOwnerAuraEffectMask)};
    }
    if(i>=timed){
        // P03/D2: one derived raid area aura application, drawn as an ordinary
        // buff. The reference sends one aura row per application, and the
        // emitter's own application is one of them - UnitAura::FillTargetMap
        // applies the aura to its caster unconditionally - so it is presented
        // like any other and not special-cased away.
        const auto& a=p.areaAuras.at(i-timed);
        // No timer, ever. SpellDuration.dbc row 21 is -1: the emitter holds no
        // lease, LocalSpellDefinition::durationMs is 0 and indefiniteDuration
        // carries the source fact. Both duration fields therefore stay 0, which
        // every reader here already means by "permanent": UnitAura answers
        // duration 0 and expirationTime 0 (src/addons/lua_spell_api.cpp:
        // 283-292), GetPlayerBuff answers untilCancelled=1 (:384) and the HUD
        // draws no sweep (src/ui/game_screen_hud.cpp:1260). Nothing counts down
        // from an invented value. -1 reads the same to those three but is this
        // client's mount/form marker (src/game/entity_controller.cpp:606,
        // src/ui/chat/game_state_adapter.cpp:88), and a self-emitted indefinite
        // aura tagged that way is mistaken for the mount aura.
        //
        // The caster shown is the emitter, never the recipient: the reference
        // keys an application on Aura::GetCasterGUID, and the recipient's own
        // emitter is just the case where the two GUIDs are equal.
        //
        // effective==false is kept visible. The reference keeps the application
        // and empties its effect mask (SpellAuras.cpp:621-625), which on the
        // wire is this flag byte losing bits 0..2; that is carried verbatim
        // below. The original UI cannot express the difference: GetPlayerBuff
        // and its four follow-ups, and UnitAura, expose name, icon, count,
        // duration and caster and never the effect mask, so a stripped
        // application is drawn exactly like a live one. Nothing is faked to
        // suggest otherwise - blanking the icon or zeroing the count would
        // claim a distinction the reference does not make.
        return {a.spellId,0,0,a.emitterGuid,1,
                uint8_t(kLocalOwnerAuraFlags|(a.effectMask&kLocalOwnerAuraEffectMask))};
    }
    if(i>=p.statAuras.size()){const auto& h=p.healingAuras.at(i-p.statAuras.size());
        return {h.spellId,h.remainingMs,h.durationMs,h.casterGuid,h.stacks,
            uint8_t(kLocalOwnerAuraFlags|kLocalOwnerAuraEffectMask)};}
    const auto& a=p.statAuras[i];const auto* d=c.spell(a.spellId);
    return {a.spellId,a.remainingMs,d?d->durationMs:a.remainingMs,a.casterGuid?a.casterGuid:p.guid,
        uint8_t(d&&d->proc.charges?a.procCharges:a.stacks),
        uint8_t(kLocalOwnerAuraFlags|kLocalOwnerAuraEffectMask)};
}
inline bool validLocalHarmfulAuraViews(const LocalRealmPlayer& p){
    if(p.harmfulAuras.size()>kLocalMaxHealingAuraViews)return false;
    for(size_t i=0;i<p.harmfulAuras.size();++i){const auto& a=p.harmfulAuras[i];
        if(!a.spellId||!a.casterGuid||!a.stacks||a.remainingMs>a.durationMs||!a.durationMs)return false;
        // LAN104 amounts: a slow below 100 % and an armor reduction, never a bonus.
        // LAN104 amounts: a slow below 100 %; LAN108 admits armor bonuses too.
        if(a.slowPercent>=100||a.armorModifier>100000||a.armorModifier<-100000||a.armorPercent<-99||a.controlKind>5)return false;
        // LAN107 amounts: bounded modifiers, a school for the scoped ones.
        if(a.attackPower>100000||a.attackPower<-100000||a.damageDoneFlat>100000||a.damageDoneFlat<-100000||
           a.damageTakenFlat>100000||a.damageTakenFlat<-100000||a.damageDonePct<-99||a.damageDonePct>1000||
           a.damageTakenPct<-99||a.damageTakenPct>1000||a.healingPct<-100||a.healingPct>0||a.hastePct<-100||a.hastePct>1000||
           a.schoolMask>127||((a.damageDoneFlat||a.damageDonePct||a.damageTakenFlat||a.damageTakenPct)&&!a.schoolMask))return false;
        if(a.breakOnDamage&&!a.controlKind)return false;
        // LAN108 amounts: cast speed, hit chance, avoidance, a scoped resistance.
        if(a.castSpeedPct<-99||a.castSpeedPct>1000||a.resistance>100000||a.resistance<-100000||a.resistanceSchool>127||
           (a.resistance&&!a.resistanceSchool)||(a.resistanceSchool&&(a.resistanceSchool&1))||
           a.hitChancePct<-100||a.hitChancePct>100||a.dodgePct<-100||a.dodgePct>100||a.parryPct<-100||a.parryPct>100||a.blockPct<-100||a.blockPct>100)return false;
        for(size_t j=0;j<i;++j)if(p.harmfulAuras[j].spellId==a.spellId&&p.harmfulAuras[j].casterGuid==a.casterGuid)return false;
    }
    if(p.schoolLockouts.size()>kLocalMaxSchoolLockouts)return false;
    for(const auto& l:p.schoolLockouts)if(!l.schoolMask||l.schoolMask>127||!l.remainingMs||l.remainingMs>600000)return false;
    if(!std::isfinite(p.knockbackCos)||!std::isfinite(p.knockbackSin)||!std::isfinite(p.knockbackSpeedXY)||!std::isfinite(p.knockbackSpeedZ)||
       std::abs(p.knockbackCos)>1.001f||std::abs(p.knockbackSin)>1.001f||p.knockbackSpeedXY<0||p.knockbackSpeedXY>1000||p.knockbackSpeedZ<0||p.knockbackSpeedZ>1000)return false;
    return true;
}
/// Creature controls on a character, from its harmful views (host and guest
/// alike): bit 1 = stunned (UNIT_STATE_STUNNED), bit 2 = rooted, bit 4 =
/// fleeing (UNIT_STATE_FLEEING), bit 8 = confused, bit 16 = silenced.
inline uint8_t localPlayerControl(const LocalRealmPlayer& p){
    uint8_t mask=0;
    for(const auto& a:p.harmfulAuras)switch(a.controlKind){case 1:mask|=1;break;case 2:mask|=2;break;case 3:mask|=4;break;case 4:mask|=8;break;case 5:mask|=16;break;default:break;}
    return mask;
}
/// Whether a creature control keeps the character from moving on its own
/// (stun, root, fear, confuse all take the client's control away).
inline bool localPlayerMovementHeld(const LocalRealmPlayer& p){ return (localPlayerControl(p)&0xf)!=0; }
/// The strongest scoped modifiers of the creature views on a character.
struct LocalPlayerViewModifiers { int32_t attackPower=0,damageDoneFlat=0,damageTakenFlat=0; int32_t damageDonePct=0,damageTakenPct=0,healingPct=0,hastePct=0;
    // LAN108: MOD_CASTING_SPEED_NOT_STACK (the strongest, Unit::ApplyCastTimePercentMod
    // of a NOT_STACK aura), MOD_HIT_CHANCE, MOD_DODGE/PARRY/BLOCK_PERCENT, a
    // scoped MOD_RESISTANCE and MOD_DISARM.
    int32_t castSpeedPct=0,hitChancePct=0,dodgePct=0,parryPct=0,blockPct=0,resistance=0; bool disarmed=false; };
inline LocalPlayerViewModifiers localPlayerViewModifiers(const LocalRealmPlayer& p,uint8_t schoolMask){
    LocalPlayerViewModifiers m;
    // Unit::GetTotalAuraModifier sums flat amounts; the percentage ones multiply
    // (GetTotalAuraMultiplier), which the callers do with the summed percent.
    for(const auto& a:p.harmfulAuras){
        m.attackPower+=a.attackPower;m.healingPct+=a.healingPct;m.hastePct+=a.hastePct;
        m.hitChancePct+=a.hitChancePct;m.dodgePct+=a.dodgePct;m.parryPct+=a.parryPct;m.blockPct+=a.blockPct;
        if(a.castSpeedPct<m.castSpeedPct||(a.castSpeedPct>0&&a.castSpeedPct>m.castSpeedPct&&m.castSpeedPct>=0))m.castSpeedPct=a.castSpeedPct;
        if(a.disarmed)m.disarmed=true;
        if(a.schoolMask&schoolMask){m.damageDoneFlat+=a.damageDoneFlat;m.damageDonePct+=a.damageDonePct;m.damageTakenFlat+=a.damageTakenFlat;m.damageTakenPct+=a.damageTakenPct;}
        if(a.resistanceSchool&schoolMask)m.resistance+=a.resistance;
    }
    return m;
}
/// Unit::CanUseAttackType: a disarmed character swings without its weapon.
inline bool localPlayerDisarmed(const LocalRealmPlayer& p){ for(const auto& a:p.harmfulAuras)if(a.disarmed)return true; return false; }
/// Unit::ApplyCastTimePercentMod's NOT_STACK term on a cast time: a negative
/// amount lengthens the cast (100 + |amount|) %, a positive one shortens it.
inline uint32_t localPlayerCastTimeModified(const LocalRealmPlayer& p,uint32_t castTimeMs){
    const auto pct=localPlayerViewModifiers(p,0).castSpeedPct;
    if(!pct||!castTimeMs)return castTimeMs;
    const float factor=pct<0?(100.f+float(-pct))/100.f:100.f/(100.f+float(pct));
    return uint32_t(std::clamp<int64_t>(int64_t(float(castTimeMs)*factor),1,3600000));
}
/// Unit::SpellHealingBonusTaken's MOD_HEALING_PCT from creature views.
inline uint32_t localPlayerHealingTaken(const LocalRealmPlayer& p,uint32_t amount){
    const auto pct=localPlayerViewModifiers(p,0).healingPct;
    if(!pct)return amount;
    return uint32_t(std::max<int64_t>(0,int64_t(amount)*(100+std::max(-100,pct))/100));
}
/// Unit::IsSpellProhibited: a school lockout refuses a cast of that school.
inline bool localPlayerSchoolLocked(const LocalRealmPlayer& p,uint32_t schoolMask){
    for(const auto& l:p.schoolLockouts)if((l.schoolMask&schoolMask)&&l.remainingMs)return true;
    return false;
}
inline bool validLocalHealingAuraViews(const LocalRealmPlayer& p){
    if(p.healingAuras.size()>kLocalMaxHealingAuraViews)return false;
    for(size_t i=0;i<p.healingAuras.size();++i){const auto& a=p.healingAuras[i];
        if(!a.stacks||!a.spellId||!a.casterGuid||!a.remainingMs||a.remainingMs>a.durationMs||a.durationMs>3600000)return false;
        for(size_t j=0;j<i;++j)if(p.healingAuras[j].spellId==a.spellId&&p.healingAuras[j].casterGuid==a.casterGuid)return false;
    }return true;
}
// Shared by the original UI command and focused host tests.
inline uint64_t localSpellCommandTarget(const LocalSpellDefinition& s,const LocalRealmPlayer& self,
        uint64_t selected,const std::vector<LocalRealmPlayer>& players){
    if(s.damage||s.periodicDamage||s.snarePercent)return selected;
    const bool friendly=((s.heal||s.periodicHeal)&&!s.healingSelfOnly)||
        ((s.buffHealth||s.buffArmor||s.buffAbsorb||s.proc.effect!=LocalProcEffect::None)&&!s.buffSelfOnly);
    if(friendly&&std::any_of(players.begin(),players.end(),[&](const auto& p){return p.guid==selected;}))return selected;
    return self.guid;
}
}
