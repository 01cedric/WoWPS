#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_area_aura.hpp"
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
inline size_t localOwnerAuraCount(const LocalRealmPlayer& p){
    return p.statAuras.size()+p.healingAuras.size()+localOwnerAreaAuraCount(p);
}
template<class Content>
LocalOwnerAuraView localOwnerAuraAt(const LocalRealmPlayer& p,const Content& c,size_t i){
    const size_t timed=p.statAuras.size()+p.healingAuras.size();
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
inline bool validLocalHealingAuraViews(const LocalRealmPlayer& p){
    if(p.healingAuras.size()>kLocalMaxHealingAuraViews)return false;
    for(size_t i=0;i<p.healingAuras.size();++i){const auto& a=p.healingAuras[i];
        if(!a.stacks||!a.spellId||!a.casterGuid||!a.remainingMs||a.remainingMs>a.durationMs||a.durationMs>600000)return false;
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
