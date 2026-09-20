#pragma once
#include "game/local_gameplay.hpp"
#include <algorithm>
namespace wowee::game {
// The class-agnostic part of the critical eligibility of a direct spell effect
// (the implementation moved the damage-class test out to the callers): a client row that is
// not passive, not SPELL_ATTR2_CANT_CRIT (SharedDefines.h:473), carries a
// school inside the seven-bit mask and a direct damage or heal, and is not a
// combo or melee-special profile (those roll their critical inside the melee
// roll). Unit::SpellDoneCritChance (Unit.cpp:8819-8907) then picks the chance
// by DmgClass: the spell critical percentage by school for MAGIC, the melee
// critical for MELEE, none for NONE.
inline bool localDirectSpellCritEligible(const LocalSpellDefinition& d) {
    return d.clientSpell&&!d.passive&&!d.sourceCantCrit&&
        d.schoolMask&&!(d.schoolMask&~127u)&&(d.damage||d.heal)&&
        !d.comboProfile&&!d.meleeSpecialProfile;
}
// The magic arm: DmgClass 1 reads PLAYER_SPELL_CRIT_PERCENTAGE1 + school,
// which is 0 for the physical school (Unit.cpp:8838-8843), so a physical
// magic-class spell is never eligible here.
inline bool localDirectMagicCritEligible(const LocalSpellDefinition& d) {
    return localDirectSpellCritEligible(d)&&d.sourceDamageClass==1&&!(d.schoolMask&1);
}
// Unit::SpellCriticalDamageBonus (Unit.cpp:9117-9131) with no
// MOD_CRIT_DAMAGE_BONUS / CRIT_PERCENT_VERSUS / SPELLMOD_CRIT_DAMAGE_BONUS
// producer accepted: +100 % for a MELEE or RANGED damage class, +50 % for
// everything else. Unit.cpp:1539-1541 writes the same 2x inline for the
// melee/ranged branch of CalculateSpellDamageTaken, and the periodic tick
// (SpellAuraEffects.cpp:6368, :6467) goes through the same function.
inline uint32_t localMagicCriticalAmount(uint32_t amount) {
    return uint32_t(std::min(uint64_t(1000000),uint64_t(amount)+amount/2));
}
inline uint32_t localMeleeCriticalAmount(uint32_t amount) {
    return uint32_t(std::min(uint64_t(1000000),uint64_t(amount)*2));
}
inline uint32_t localSpellCriticalAmount(const LocalSpellDefinition* d,uint32_t amount) {
    return d&&d->clientSpell&&(d->sourceDamageClass==2||d->sourceDamageClass==3)?localMeleeCriticalAmount(amount):localMagicCriticalAmount(amount);
}
// The the implementation dispatch of a hostile-targeted DmgClass 2 definition with no
// combo / melee-special / Stormstrike profile through the melee-class roll
// (WorldObject::SpellHitResult, Object.cpp:3719-3723: MELEE and RANGED go to
// MeleeSpellHitResult). The 39 accepted such spells took an unconditional Hit
// previously (the source audit section 2.3, D6/D7).
inline bool localMeleeClassSpellRoll(const LocalSpellDefinition& d) {
    return d.clientSpell&&d.sourceDamageClass==2&&!d.comboProfile&&!d.meleeSpecialProfile&&!d.stormstrikeProfile&&
        !d.passive&&!d.triggeredOnly&&(d.damage||d.periodicDamage||d.snarePercent||d.controlProfile)&&!d.heal&&!d.periodicHeal;
}
// Unit::MeleeSpellHitResult :3351 - a FULL block is rolled only for
// SPELL_ATTR3_COMPLETELY_BLOCKED without the computed CU_DIRECT_DAMAGE. Every
// accepted COMPLETELY_BLOCKED row (the five Rake ranks) carries a school
// damage effect, so this is false on all 990 at the pin; it is written at its
// real gate so the data decides.
inline bool localSpellFullyBlockable(const LocalSpellDefinition& d) {
    return d.sourceCompletelyBlocked&&!d.sourceDirectDamage;
}
// Unit::isSpellBlocked (Unit.cpp:3263-3267) and the CalculateSpellDamageTaken
// gate that calls it (:1524-1531): a physical MELEE or RANGED class spell that
// is neither NO_ACTIVE_DEFENSE nor ALWAYS_HIT rolls an independent partial
// block. The ranged shot has its own site; this is the cast site's predicate.
inline bool localSpellPartialBlockApplies(const LocalSpellDefinition& d) {
    return d.clientSpell&&(d.sourceDamageClass==2||d.sourceDamageClass==3)&&(d.schoolMask&1)&&
        !d.sourceNoActiveDefense&&!d.sourceAlwaysHit;
}
}
