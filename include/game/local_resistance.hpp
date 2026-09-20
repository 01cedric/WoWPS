#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "game/local_combat_events.hpp"
namespace wowee::game {
// P04 partial spell resistance, transcribed from AzerothCore
// 9c416aaacb5537636abb13c80f55a88947838e33. The binary hit-roll term is
// separately selected by the importer's validated source profile; it never
// applies the partial bucket or the victim-level term.

// Unit::GetResistance(SpellSchoolMask), Unit.cpp:15821-15830: the MINIMUM over
// the schools named by the mask, so a Frostfire spell is resisted by the lower
// of fire and frost. `resistances` is UNIT_FIELD_RESISTANCES order 1..6 (holy,
// fire, nature, frost, shadow, arcane). The reference's loop starts at
// SPELL_SCHOOL_NORMAL, whose slot is armor, and returns uint32(-1) for an
// empty mask; both are behind the CalcAbsorbResist gate below (no NORMAL bit,
// a non-empty mask), so this reduction reads schools 1..6 only and answers
// zero for a mask the gate would never have passed.
inline uint32_t localResistanceForMask(const std::array<uint16_t,6>& resistances,uint32_t schoolMask) {
    int32_t resist=-1;
    for(unsigned school=1;school<7;++school)
        if((schoolMask>>school)&1u && (resist<0||resist>int32_t(resistances[school-1])))resist=int32_t(resistances[school-1]);
    return resist<0?0u:uint32_t(resist);
}

// Unit::CalcAbsorbResist's gate, Unit.cpp:2341: "Magic damage, check for
// resists. Ignore spells that cant be resisted. Xinef: holy resistance exists
// for npcs." A NORMAL-school hit never resists (armor is its mitigation), a
// HOLY hit resists only against a creature, and SPELL_ATTR4_NO_CAST_LOG or the
// binary attribute exempts the spell. The victim in every local call is a
// creature - no player is ever the target of this roll at this baseline.
inline bool localPartialResistApplies(uint32_t schoolMask,bool victimIsCreature,bool noCastLog,bool binary) {
    if(!schoolMask||(schoolMask&1u))return false;
    if((schoolMask&2u)&&!victimIsCreature)return false;
    return !binary&&!noCastLog;
}

// Unit::GetEffectiveResistChance, Unit.cpp:2288-2323, in the static form
// CalcAbsorbResist calls with spellInfo == nullptr: the victim's resistance,
// the level-difference term of +5 per level the victim stands above the
// caster (never negative), and the caster-level constant K - 50 to level 20,
// then +2.5 per level to 60, then 150 + (L-60)(L-67.5), which is 400 at 80 -
// capped at 75 %. The MOD_TARGET_RESISTANCE (123) and spell-penetration terms
// (Unit.cpp:2296-2301) have zero accepted producers in this build and are
// omitted rather than approximated. `casterLevel` 0 means "no caster", in
// which case the victim's own level chooses K and the level term is skipped,
// exactly as the reference's `effectiveCasterLevel` does.
inline float localAverageResist(uint32_t victimResistance,uint8_t casterLevel,uint8_t victimLevel,bool binary=false) {
    float resistance=std::max(float(victimResistance),0.f);
    if(casterLevel&&!binary)
        resistance+=std::max(float(int(victimLevel)-int(casterLevel))*5.f,0.f);
    const float level=float(casterLevel?casterLevel:victimLevel);
    float constant=50.f;
    if(level>60.f)constant=150.f+(level-60.f)*(level-67.5f);
    else if(level>20.f)constant=50.f+(level-20.f)*2.5f;
    return std::min(resistance/(resistance+constant),0.75f);
}

// Object.cpp:3634-3660: after the ordinary miss band, use the SAME 1..10000
// roll for binary resistance. Negative spells only; NORMAL and HOLY exclude
// this term (unlike partial HOLY resistance against creatures).
inline bool localBinaryResistApplies(uint32_t schoolMask,bool negative,bool noCastLog,bool binary) {
    return negative&&binary&&!noCastLog&&schoolMask!=1u&&schoolMask!=2u&&schoolMask!=0u;
}
inline LocalMeleeOutcome localMagicHitOutcome(uint32_t miss,float binaryAverage,uint32_t rollOneTo10000) {
    if(rollOneTo10000<miss)return LocalMeleeOutcome::Miss;
    const uint32_t resist=uint32_t(std::clamp(binaryAverage,0.f,0.75f)*10000.f);
    if(rollOneTo10000<miss+resist)return LocalMeleeOutcome::Resist;
    return LocalMeleeOutcome::Hit;
}

// Unit::CalcAbsorbResist, Unit.cpp:2345-2367: the eleven bucket probabilities
// form a triangle of base 0.4 around the average (so the expected fraction
// resisted is the average itself), the average <= 0.1 case is a three-bucket
// special, the walk is a strict `r >= sum` with the bucket capped at 10, and
// `r` is rand_norm() in [0, 1). Returns the tenths of the hit resisted.
inline unsigned localPartialResistBucket(float averageResist,float roll) {
    float probability[11];
    for(unsigned i=0;i<11;++i) {
        probability[i]=0.5f-2.5f*std::fabs(0.1f*float(i)-averageResist);
        if(probability[i]<0.f)probability[i]=0.f;
    }
    if(averageResist<=0.1f) {
        probability[0]=1.f-7.5f*averageResist;
        probability[1]=5.f*averageResist;
        probability[2]=2.5f*averageResist;
    }
    unsigned i=0;float sum=probability[0];
    while(roll>=sum&&i<10)sum+=probability[++i];
    return i;
}
// Unit.cpp:2367: `float(damage * i / 10)` - integer arithmetic before the
// cast, so a 15-point hit in bucket 1 resists 1, not 1.5. Bucket 10 resists
// the whole hit, which is HITINFO_FULL_RESIST and PROC_HIT_FULL_RESIST
// (Unit.cpp:1975-1979, :144-145); anything below is HITINFO_PARTIAL_RESIST on
// an ordinary landed hit.
inline uint32_t localResistedAmount(uint32_t damage,unsigned bucket) {
    return uint32_t(uint64_t(damage)*std::min(bucket,10u)/10u);
}
}
