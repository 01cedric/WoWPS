#pragma once
#include "game/local_gameplay.hpp"
namespace wowee::game {
struct LocalMeleeItem {
    uint32_t id,itemClass,subclass,inventoryType,delay,block,scaling;
    std::array<float,4> damage;std::array<uint8_t,2> school;
    std::array<int32_t,15> stats;
};
struct LocalMeleeStats {
    std::array<int32_t,5> base{},attributes{};
    std::array<int32_t,8> ratings{};std::array<float,8> ratingBonus{};
    float attackPower=0,crit=0,dodge=0,parry=0,block=0,hit=0,haste=0,expertise=0,defense=0,missBonus=0;
    uint32_t blockValue=0;bool offHand=false,sourceStats=false;
    // Player::GetShieldBlockValue() (Player.cpp:5253-5260): worn block value
    // plus strength / 2 minus 10, never negative - computed with or without a
    // shield, because the reference's Shield Slam term reads it either way.
    uint32_t shieldBlockValue=0;
    float offHandCrit=0; // Weapon-constrained talents are evaluated per hand.
    float incomingCritReductionPct=0;
    float talentWeaponHitPct=0; // Aura54: melee and ranged, separate from rating; never spell hit.
    // P06  aura 280, Unit::CalcArmorReducedDamage's bonusPct
    // (Unit.cpp:2239-2267). Battle Stance carries +10% on the form spell itself.
    uint32_t armorPenetrationPct=0;
};
struct LocalSpellCritStats {
    bool sourceStats=false;
    float basePct=0,intellectPct=0,ratingPct=0,talentPct=0,crit=0;
    int32_t itemRating=0,auraRating=0;
};
LocalSpellCritStats localSpellCritStats(const LocalRealmPlayer&,const LocalWorldContent&);
float localSpellCritFromIntellect(const LocalRealmPlayer&,const LocalWorldContent&);
float localSpellCritRatingBonus(const LocalRealmPlayer&,const LocalWorldContent&);
int32_t localRangedCritRating(const LocalRealmPlayer&,const LocalWorldContent&);
float localRangedCritRatingBonus(const LocalRealmPlayer&,const LocalWorldContent&);
float localRangedCritChance(const LocalRealmPlayer&,const LocalWorldContent&);
float localSpellCritChance(const LocalRealmPlayer&,const LocalWorldContent&,uint32_t schoolMask);
float localSpellCritChance(const LocalRealmPlayer&,const LocalWorldContent&,const LocalSpellDefinition&);
float localIncomingCritReductionPct(const LocalRealmPlayer&,const LocalWorldContent&);
uint32_t localNpcCreatureType(uint32_t entry);
bool localNpcIsDemonOrUndead(uint32_t entry);
bool localNpcExperienceTargetEligible(uint32_t entry,uint8_t actorLevel,uint8_t targetLevel);
struct LocalWeaponAmounts {float low=1,high=2,magicLow=0,magicHigh=0,seconds=2,apSeconds=2;bool active=true;};
const LocalMeleeItem* localMeleeItem(uint32_t id);
uint32_t localNpcMeleeFlags(uint32_t entry);
uint32_t localMeleeArmor(const LocalRealmPlayer&,const LocalWorldContent&);
float localMeleeRatingBonus(const LocalRealmPlayer&,const LocalWorldContent&,int rating);
LocalMeleeStats localMeleeStats(const LocalRealmPlayer&,const LocalWorldContent&);
float localWeaponTalentDamageMultiplier(const LocalRealmPlayer&,const LocalWorldContent&,bool offHand=false);
LocalWeaponAmounts localWeaponAmounts(const LocalRealmPlayer&,const LocalWorldContent&,bool offHand=false,bool normalized=false,bool applyDamageModifiers=true);
// AP-based melee specials use the spell effect's percentage, without an added
// weapon roll or weapon-speed normalization (Bloodthirst's source formula).
uint32_t localMeleeSpecialAmount(float attackPower,uint32_t coefficientPct);
// Unit::GetShieldBlockValue(soft_cap, hard_cap) (Unit.h:1181-1194): at or
// above the hard cap the value is the midpoint of the caps; above the soft cap
// the excess counts half; otherwise it is taken as it is.
inline uint32_t localShieldSlamBlockValue(uint32_t value,uint32_t softCap,uint32_t hardCap) {
    if(value>=hardCap)return (softCap+hardCap)/2;
    if(value>softCap)return softCap+(value-softCap)/2;
    return value;
}
// Physical melee spells roll block independently, so a critical can also block
// (Unit::isSpellBlocked, Unit.cpp:3263-3287, minus its two attribute exits,
// which the caller's localSpellPartialBlockApplies tests). The creature's
// block value is Creature::GetShieldBlockValue (Creature.h:158-161),
// level / 2 + strength / 20; this realm's catalog carries no creature strength
// (creature_classlevelstats is not imported), so the term is level / 2 at
// every site (the implementation unified the ranged site's level / 2 + 1).
bool localRollMeleeSpecialBlock(const LocalRealmPlayer&,const LocalRealmNpc&,uint32_t roll);
inline uint32_t localCreatureBlockValue(const LocalRealmNpc& n){return n.level/2;}
float localMeleeSpeed(const LocalRealmPlayer&,const LocalWorldContent&,bool offHand=false);
// Only an active, source-validated child of the currently allocated talent
// contributes. The returned percent is separate from item haste rating.
float localMeleeAuraHastePct(const LocalRealmPlayer&,const LocalWorldContent&);
// Preserve completed swing progress when a period changes. A due swing stays
// due, including the other hand during a same-frame aura mutation.
float localRescaledMeleeTimer(float remaining,float oldPeriod,float newPeriod);
void localRescaleMeleeTimers(LocalRealmPlayer&,float oldMainPeriod,float oldOffPeriod,const LocalWorldContent&);
// The three MeleeSpellHitResult attribute arms a melee-class spell can carry
// (the implementation, P05-1): SPELL_ATTR3_ALWAYS_HIT returns SPELL_MISS_NONE before any
// roll (Unit.cpp:3321-3322; the separate critical roll still runs),
// SPELL_ATTR0_NO_ACTIVE_DEFENSE returns after the miss roll (:3354-3355,
// "cannot be dodged, parried or blocked"), and a full block is rolled last
// only for COMPLETELY_BLOCKED without CU_DIRECT_DAMAGE (:3351, :3475-3483).
// A white swing passes none of them.
struct LocalMeleeSpellRules { bool alwaysHit=false,noActiveDefense=false,fullBlock=false; };
LocalMeleeOutcome localRollPlayerMelee(const LocalRealmPlayer&,const LocalRealmNpc&,const LocalMeleeStats&,bool special,uint32_t roll,uint32_t criticalRoll,bool offHand=false,LocalMeleeSpellRules rules={});
LocalMeleeOutcome localRollNpcMelee(const LocalRealmNpc&,const LocalRealmPlayer&,const LocalMeleeStats&,uint32_t roll);
/// Unit::MeleeSpellHitResult for a creature melee special; roll 0..10000.
LocalMeleeOutcome localRollNpcMeleeSpell(const LocalRealmNpc&,const LocalRealmPlayer&,const LocalMeleeStats&,const LocalSpellDefinition&,uint32_t roll);
/// Unit::isSpellBlocked for a physical creature melee special; roll 0..9999.
bool localNpcMeleeSpellBlocked(const LocalRealmNpc&,const LocalRealmPlayer&,const LocalMeleeStats&,const LocalSpellDefinition&,uint32_t roll);
// Creature attacker against a creature victim (Unit::RollMeleeOutcomeAgainst
// with a non-player attacker): the victim's own avoidance from its creature
// flags and rank, the source crushing rule for a four-level advantage and a
// five percent base critical chance. Glancing is a player-attacker rule and
// therefore never produced here.
struct LocalRealmPet;
LocalMeleeOutcome localRollPetMelee(const LocalRealmPet&,const LocalRealmNpc&,uint32_t roll);
LocalMeleeOutcome localRollNpcAgainstPet(const LocalRealmNpc&,const LocalRealmPet&,uint32_t roll);
// Melee avoidance, and only that: this predicate also gates melee-specific
// rules such as the reduced resource cost of an avoided special, which must not
// start applying to a resisted magic spell.
inline bool localMeleeAvoided(LocalMeleeOutcome x){return x==LocalMeleeOutcome::Miss||x==LocalMeleeOutcome::Dodge||x==LocalMeleeOutcome::Parry;}
// Every outcome that nullifies damage, matching damageNullified in the source's
// DamageInfo. Damage paths use this; melee rules keep localMeleeAvoided.
inline bool localOutcomeNullifiesDamage(LocalMeleeOutcome x){
    return localMeleeAvoided(x)||x==LocalMeleeOutcome::Resist||x==LocalMeleeOutcome::Immune||
           x==LocalMeleeOutcome::Deflect;
}
}
