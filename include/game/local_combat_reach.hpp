#pragma once
#include "game/local_gameplay.hpp"
#include <algorithm>
#include <cmath>

// Combat reach: the term every range test in the reference adds and this build
// added nowhere previously (P05-5; the source audit
// section 5.1, divergences D21, D23, D24, D25).
//
//   ObjectDefines.h:44-47  DEFAULT_WORLD_OBJECT_SIZE 0.388999998569489,
//                          DEFAULT_COMBAT_REACH 1.5, MIN_MELEE_REACH 2.0,
//                          NOMINAL_MELEE_RANGE 5.0
//   Player.h:1104          a player's UNIT_FIELD_COMBATREACH is
//                          scale * DEFAULT_COMBAT_REACH, and no local player
//                          ever has a scale but 1.
//   Creature.cpp:3536-3550 a creature's is creature_model_info.CombatReach for
//                          its display id, or DEFAULT_WORLD_OBJECT_SIZE when
//                          that row is missing or the column is not positive,
//                          times the model row's DisplayScale. The catalog
//                          stores the product; see LocalNpcDefinition.
//   Unit.cpp:766-780       IsWithinCombatRange: dist^2 < (range + reachA + reachB)^2
//   Unit.cpp:782-797       IsWithinMeleeRange:  dist^2 < (dist + GetMeleeRange + leeway)^2
//   Unit.cpp:799-803       GetMeleeRange: max(reachA + reachB + 4/3, NOMINAL_MELEE_RANGE)
//   Unit.cpp:805-818       IsWithinRange: dist^2 <= range^2
//   Unit.cpp:820-828       IsWithinBoundaryRadius: dist <= max(target bounding
//                          radius, MIN_MELEE_REACH) - the facing check's escape.
//
// GetLeewayBonusRange (Unit.cpp:794) is a movement-speed PvP allowance and is
// zero here for the reason the audit's section 8.3 gives; every site below
// therefore takes the constant branch.
namespace wowee::game {
inline constexpr float kLocalWorldObjectSize = 0.388999998569489f;
inline constexpr float kLocalDefaultCombatReach = 1.5f;
inline constexpr float kLocalMinMeleeReach = 2.0f;
inline constexpr float kLocalNominalMeleeRange = 5.0f;

/// UNIT_FIELD_COMBATREACH of a catalog creature. A catalog built previously
/// carries no value, and the reference's own default for a creature whose model
/// info is missing stands in - never the player's 1.5.
inline float localCreatureCombatReach(const LocalNpcDefinition* d) {
    if(!d||!std::isfinite(d->combatReach)||d->combatReach<=0)return kLocalWorldObjectSize;
    return std::min(d->combatReach,1000.0f);
}
/// UNIT_FIELD_BOUNDINGRADIUS of a catalog creature; zero without a row.
inline float localCreatureBoundingRadius(const LocalNpcDefinition* d) {
    if(!d||!std::isfinite(d->boundingRadius)||d->boundingRadius<=0)return 0;
    return std::min(d->boundingRadius,1000.0f);
}
/// Unit::GetMeleeRange, Unit.cpp:799-803.
inline float localMeleeRange(float reachA,float reachB) {
    return std::max(reachA+reachB+4.0f/3.0f,kLocalNominalMeleeRange);
}
/// Unit::IsWithinMeleeRange(obj, dist), Unit.cpp:782-797, with the zero leeway
/// of a realm without PvP movement. `squared` is the centre-to-centre squared
/// distance the caller already has.
inline bool localWithinMeleeRange(float squared,float reachA,float reachB,float dist=0) {
    const float maximum=std::max(0.0f,dist)+localMeleeRange(reachA,reachB);
    return std::isfinite(squared)&&squared<maximum*maximum;
}
/// Unit::IsWithinCombatRange, Unit.cpp:766-780.
inline bool localWithinCombatRange(float squared,float range,float reachA,float reachB) {
    const float maximum=range+reachA+reachB;
    return std::isfinite(squared)&&maximum>0&&squared<maximum*maximum;
}
/// Unit::IsWithinBoundaryRadius, Unit.cpp:820-828: the target's bounding radius
/// floored at MIN_MELEE_REACH, compared with <=.
inline bool localWithinBoundaryRadius(float squared,float targetBoundingRadius) {
    const float radius=std::max(targetBoundingRadius,kLocalMinMeleeReach);
    return std::isfinite(squared)&&squared<=radius*radius;
}
/// Position::HasInArc(M_PI, obj) reduced to its half-plane test, Position.cpp:148-181:
/// the angle between the actor's orientation and the direction to the target is
/// compared with +-pi/2 inclusive, which is exactly `dot >= 0`. A target at the
/// actor's own position is always in arc (`obj == this`, :150-152).
inline bool localHasInArcPi(float x,float y,float orientation,float targetX,float targetY) {
    const double dx=double(targetX)-double(x),dy=double(targetY)-double(y);
    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(orientation))return false;
    if(dx*dx+dy*dy<1e-8)return true;
    return dx*std::cos(double(orientation))+dy*std::sin(double(orientation))>=0;
}
}
