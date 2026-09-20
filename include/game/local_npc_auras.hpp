#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_diminishing.hpp"
namespace wowee::game {
inline bool validLocalNpcStormstrikeAuras(const LocalRealmNpc& n,const LocalWorldContent& c) {
    if(n.stormstrikeAuras.size()>kLocalMaxNpcStormstrikeAuras||(n.dead&&!n.stormstrikeAuras.empty()))return false;
    for(size_t i=0;i<n.stormstrikeAuras.size();++i) {
        const auto& a=n.stormstrikeAuras[i];const auto* d=c.spell(a.spellId);
        if(!d||!d->unsupportedReason.empty()||d->id!=17364||d->stormstrikeProfile!=1||d->durationMs!=12000||
           !a.casterGuid||!a.remainingMs||a.remainingMs>12000||!a.charges||a.charges>4)return false;
        for(size_t j=0;j<i;++j)if(n.stormstrikeAuras[j].casterGuid==a.casterGuid)return false;
    }
    return true;
}
// Piecewise integration preserves partial-tick expiry: a weaker aura takes
// over only after the stronger one ends. Reductions never add or multiply.
inline float localNpcPursuitDistance(const LocalRealmNpc& n,uint32_t elapsedMs) {
    uint32_t at=0;uint64_t weighted=0;
    while(at<elapsedMs) {
        uint32_t next=elapsedMs;uint8_t strongest=0;
        for(const auto& a:n.snares)if(a.remainingMs>at) {
            strongest=std::max(strongest,a.percent);next=std::min(next,a.remainingMs);
        }
        weighted+=uint64_t(next-at)*(100-std::min(uint8_t(99),strongest));at=next;
    }
    return float(weighted)*0.00004f; // Existing NPC base pursuit is 4 units/s.
}
inline bool validLocalNpcSnares(const LocalRealmNpc& n,const LocalWorldContent& c) {
    if(n.snares.size()>kLocalMaxNpcSnares || ((n.dead||n.transportEntry)&&!n.snares.empty()))return false;
    for(size_t i=0;i<n.snares.size();++i) {
        const auto& a=n.snares[i];const auto* d=c.spell(a.spellId);
        if(!d||!d->unsupportedReason.empty()||!d->snarePercent||d->snarePercent>99||
           a.percent!=d->snarePercent||!a.casterGuid||!a.remainingMs||
           a.remainingMs>d->durationMs||d->durationMs>600000)return false;
        for(size_t j=0;j<i;++j)if(n.snares[j].casterGuid==a.casterGuid&&
            n.snares[j].spellId==a.spellId)return false;
    }
    return true;
}

// P04. The reference's stun implies root (Unit::SetStunned clears the unit's
// movement before anything else), so one predicate answers both the pursuit and
// the swing question. Silence suppresses only the cast tick.
inline bool localNpcStunned(const LocalRealmNpc& n) {
    for(const auto& a:n.controls)
        if(a.remainingMs&&a.kind==uint8_t(LocalNpcControlKind::Stun))return true;
    return false;
}
// Silence alone. Unit::SetStunned sets UNIT_FLAG_STUNNED and nothing else, and
// UNIT_FLAG_SILENCED has exactly one writer in the reference,
// AuraEffect::HandleAuraModSilence - so a stunned creature must not be
// replicated as silenced. The cast gate reads `stunned || (silenced && ...)`
// and therefore loses nothing by this narrowing.
inline bool localNpcSilenced(const LocalRealmNpc& n) {
    for(const auto& a:n.controls)
        if(a.remainingMs&&a.kind==uint8_t(LocalNpcControlKind::Silence))return true;
    return false;
}
inline bool validLocalNpcControls(const LocalRealmNpc& n,const LocalWorldContent& c) {
    if(n.controls.size()>kLocalMaxNpcControls || ((n.dead||n.transportEntry)&&!n.controls.empty()))return false;
    for(size_t i=0;i<n.controls.size();++i) {
        const auto& a=n.controls[i];const auto* d=c.spell(a.spellId);
        if(!d||!d->unsupportedReason.empty()||!d->controlProfile||
           a.kind!=uint8_t(d->controlProfile==2?LocalNpcControlKind::Silence:LocalNpcControlKind::Stun)||
           !a.casterGuid||!a.remainingMs||a.remainingMs>d->durationMs||d->durationMs>600000)return false;
        // One application per caster per spell, exactly as a snare. A second
        // rank replaces rather than stacks; the cast path resolves that.
        for(size_t j=0;j<i;++j)if(n.controls[j].casterGuid==a.casterGuid&&
            n.controls[j].spellId==a.spellId)return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// P04 creature template immunity. AzerothCore 9c416aaacb5537636abb13c80f55a88947838e33.
// The container the reference walks is Unit::m_spellImmune, eight multimaps
// keyed by the granting spell (Unit.h:2092-2093); a creature template fills the
// IMMUNITY_SCHOOL and IMMUNITY_MECHANIC maps from its creature_immunities set
// with the placeholder id UINT32_MAX (Creature.cpp:2246-2283). This build has
// no aura-granted immunity on either side, so the two definition masks ARE the
// container, and the predicates below are the reference's read of it.
// ---------------------------------------------------------------------------
// Which effect slots of `d` a creature template strips, as a bit per slot.
// Creature::IsImmunedToSpellEffect (Creature.cpp:2318-2328): the EFFECT-level
// mechanic against the template's mechanic set, tested before anything else
// and therefore not bypassed by SPELL_ATTR0_NO_IMMUNITIES (that bit is read
// only in Unit::IsImmunedToSpellEffect, :9822, which runs after). The
// mechanical-creature heal arm (:2324-2325) is not written: no player heal can
// target a creature here. Unit::IsImmunedToSpellEffect's own arms - IMMUNITY_EFFECT,
// IMMUNITY_STATE, aura 267 - read the set's Effects and Auras lists and
// aura-granted state, and the source audit section 6.5
// measured both lists to intersect no admitted spell, so they are not carried.
// Spell.cpp:2413-2416 (AddUnitTarget) and :3101-3118 (DoSpellHitOnUnit) drop
// the stripped slots from what the cast applies; the rest lands, and no immune
// line is shown for a partially stripped cast.
inline uint8_t localNpcStrippedEffects(const LocalNpcDefinition& def,const LocalSpellDefinition& d) {
    uint8_t stripped=0;
    for(unsigned k=0;k<3;++k) {
        if(!(d.effectMask&(1u<<k)))continue;
        const auto mechanic=d.effectMechanic[k];
        if(mechanic&&mechanic<64&&((def.immuneMechanicsMask>>mechanic)&1u))stripped|=uint8_t(1u<<k);
    }
    return stripped;
}
// Unit::IgnoresSchoolImmunityFromFriendlyCaster (Unit.cpp:9571-9581): template
// immunity, recognisable by its placeholder id, never applies against a
// friendly caster - a player may heal a nature-immune friendly creature. Every
// caller here has already passed canAttack, so the caster is hostile and the
// clause is a constant; it is written on the caller as the reference writes
// it, so the shape survives a friendly-target producer.
inline bool localNpcSchoolImmune(const LocalNpcDefinition& def,uint32_t schoolMask,bool casterFriendly) {
    if(!schoolMask||casterFriendly)return false;
    return (uint32_t(def.immuneSchoolMask)&schoolMask)==schoolMask;
}
// Creature::IsImmunedToSpell (Creature.cpp:2286-2316) followed by
// Unit::IsImmunedToSpell (Unit.cpp:9733-9799), reduced to the template
// population, in the reference's order:
//   1. SPELL_ATTR0_CU_BYPASS_MECHANIC_IMMUNITY (a spell_custom_attr bit on two
//      raid ids, neither admitted) - not carried, so never true.
//   2. the SPELL-level Mechanic (column 3) against the template's mechanic set
//      (:2298-2299) - ahead of the NO_IMMUNITIES read, so not bypassed by it.
//   3. immune to every IsEffect() slot (:2305-2313) through
//      localNpcStrippedEffects.
//   4. Unit::IsImmunedToSpell: IMMUNITY_ID / IMMUNITY_ALLOW_ID are script-only
//      (:9739-9749; no producer); NO_IMMUNITIES answers not immune (:9751);
//      IMMUNITY_DISPEL reads DispelTypeMask, zero on all 456 sets (:9754); the
//      spell mechanic and all-effects arms repeat 2 and 3; then, unless
//      SPELL_ATTR2_NO_SCHOOL_IMMUNITIES, HasSchoolImmunityForMask (:9583-9608)
//      - the template's school mask must COVER the spell's whole mask.
// WorldObject::SpellHitResult (Object.cpp:3746-3770) asks this before the hit
// roll, and Spell::DoSpellHitOnUnit reads the diminishing record only for a
// target that was not immune (Spell.cpp:3195), which is why the caller must
// run this before any diminishing read. Immunity is decided at hit, never at
// cast (there is no SPELL_FAILED_IMMUNE site), so the cost is always paid.
inline bool localNpcImmuneToSpell(const LocalNpcDefinition& def,const LocalSpellDefinition& d,bool casterFriendly) {
    if(d.mechanic&&d.mechanic<64&&((def.immuneMechanicsMask>>d.mechanic)&1u))return true;
    if(d.effectMask&&localNpcStrippedEffects(def,d)==d.effectMask)return true;
    if(d.sourceNoImmunities)return false;
    if(!d.sourceNoSchoolImmunities&&localNpcSchoolImmune(def,d.schoolMask,casterFriendly))return true;
    return false;
}
// Unit::IsImmunedToDamage(caster, spellInfo) (Unit.cpp:9629-9669), the form
// every periodic damage tick asks first (SpellAuraEffects.cpp:6287:
// SendTickImmune, the aura kept): NO_IMMUNITIES or NO_SCHOOL_IMMUNITIES
// answers not immune, then the template school mask must cover the spell's.
// IMMUNITY_DAMAGE is aura-granted only and has no producer here.
inline bool localNpcImmuneToDamage(const LocalNpcDefinition& def,const LocalSpellDefinition& d,bool casterFriendly) {
    if(d.sourceNoImmunities||d.sourceNoSchoolImmunities)return false;
    return localNpcSchoolImmune(def,d.schoolMask,casterFriendly);
}

// ---------------------------------------------------------------------------
// P04 dispel: Unit::GetDispellableAuraList (Unit.cpp:5612-5673) over the four
// creature containers, which hold player-cast harmful auras and nothing else.
// The candidate list is built from visible, non-passive auras whose
// (1 << Dispel) meets the mask; for DISPEL_MAGIC only, an aura is skipped when
// its sign equals the caster's friendliness to the target (:5650-5655) - so an
// OFFENSIVE magic dispel removes helpful auras only. Curse, disease, poison and
// enrage have no sign test; MECHANIC_BANISH yields only to a NO_IMMUNITIES
// dispel (:5659-5662). The count pushed per aura is stacks, or charges under
// SPELL_ATTR7_DISPEL_REMOVES_CHARGES - it decides how many draws an aura
// survives, never which aura is drawn. Every aura a creature holds in this
// build is harmful, so the list is empty for the one admitted shape (an
// offensive magic dispel) by the reference's own rule; the draw loop of
// SpellEffects.cpp:2744-2795 therefore has no candidate to remove and is not
// transcribed. The suite asserts the count is zero for every accepted
// definition against fully populated containers.
inline uint32_t localDispelMask(uint8_t dispelType) {
    // SpellInfo::GetDispelMask (SpellInfo.cpp:1975-1986): DISPEL_ALL (7) is the
    // four-bit magic|curse|disease|poison mask, never stealth or enrage.
    if(dispelType==7)return (1u<<1)|(1u<<2)|(1u<<3)|(1u<<4);
    return 1u<<dispelType;
}
inline size_t localNpcDispellableAuraCount(const LocalRealmNpc& n,const LocalWorldContent& c,
                                           uint32_t dispelMask,bool casterFriendlyToTarget,bool dispellerNoImmunities) {
    size_t count=0;
    // `positive` is the reference's "target is friendly to the caster"; a
    // creature-held aura is harmful (IsPositive false), so the magic arm skips
    // it exactly when the dispeller is hostile.
    const auto candidate=[&](uint32_t spellId,uint32_t stacks) {
        const auto* d=c.spell(spellId);if(!d||d->passive||!stacks)return;
        if(!(localDispelMask(d->dispelType)&dispelMask))return;
        constexpr bool auraPositive=false;
        if(d->dispelType==1&&auraPositive==casterFriendlyToTarget)return;
        if(d->mechanic==kLocalMechanicBanish&&!dispellerNoImmunities)return;
        ++count;
    };
    for(const auto& a:n.controls)if(a.remainingMs)candidate(a.spellId,1);
    for(const auto& a:n.snares)if(a.remainingMs)candidate(a.spellId,1);
    for(const auto& a:n.damageAuras)if(a.remainingMs)candidate(a.spellId,a.stacks);
    for(const auto& a:n.stormstrikeAuras)if(a.remainingMs)candidate(a.spellId,a.charges);
    return count;
}

inline bool validLocalNpcDamageAuras(const LocalRealmNpc& n,const LocalWorldContent& c) {
    if(n.damageAuras.size()>kLocalMaxNpcDamageAuras||(n.dead&&!n.damageAuras.empty()))return false;
    for(size_t i=0;i<n.damageAuras.size();++i) {
        const auto& a=n.damageAuras[i];const auto* d=c.spell(a.spellId);
        if(!d||!d->unsupportedReason.empty()||!d->periodicDamage||!a.casterGuid||!a.remainingMs||
           a.durationMs!=d->durationMs||a.remainingMs>a.durationMs||a.durationMs>600000||!a.stacks||a.stacks>d->maxAuraStacks)return false;
        for(size_t j=0;j<i;++j)if(n.damageAuras[j].casterGuid==a.casterGuid&&n.damageAuras[j].spellId==a.spellId)return false;
    }
    return true;
}
}
