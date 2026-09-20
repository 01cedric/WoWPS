#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_combat_reach.hpp"
#include <algorithm>

// P05  target and facing rules the importer had the columns for and the
// authority never read: the source audit D10, D26, D27.
namespace wowee::game {
/// Unit::GetCreatureTypeMask, Unit.h:829-833: 1 << (type - 1), and 0 for a
/// template whose creature_template.type is 0.
inline uint32_t localCreatureTypeMask(uint32_t creatureType) {
    return creatureType>=1&&creatureType<=32?1u<<(creatureType-1):0u;
}
/// SpellInfo::CheckTargetCreatureType, SpellInfo.cpp:1906-1919. The warlock
/// category-1179 clause (Curse of Doom and Exorcism, :1908-1916) answers "true
/// for anything that is not a player", which every target of this realm is; it
/// is written so a future player target cannot silently take the wrong branch.
inline bool localSpellCreatureTypeAllowed(const LocalSpellDefinition& d,uint32_t creatureType,bool targetIsPlayer=false) {
    if(d.spellFamily==5&&d.cooldownCategory==1179)return !targetIsPlayer;
    const auto mask=localCreatureTypeMask(creatureType);
    return !d.targetCreatureType||!mask||(mask&d.targetCreatureType);
}
/// Unit::GetCreatureType for a player, Unit.cpp:11473-11486: the active
/// shapeshift form's SpellShapeshiftForm.creatureType when it is positive, else
/// CREATURE_TYPE_HUMANOID. The client's own column 20 for the 3.3.5a form table
/// is transcribed here rather than imported, because it has no consumer: 0 of
/// the 990 accepted definitions carry a TargetCreatureType on a friendly spell.
/// It exists so the arm cannot answer "allowed" for the wrong reason.
inline uint32_t localFormCreatureType(uint8_t form) {
    switch(form) {
        case 1:case 3:case 4:case 5:case 8:case 14:case 15:case 16:case 26:case 27:case 29:return 1; // beast
        case 2:return 4; // elemental
        case 7:case 10:case 21:case 25:return 6; // undead
        case 22:return 3; // demon
        default:return 7; // humanoid
    }
}
/// FacingCasterFlags & SPELL_FACING_FLAG_INFRONT (SpellDefines.h:136), the gate
/// Spell::CheckRange applies to a player caster at :7360.
inline bool localSpellRequiresFacing(const LocalSpellDefinition& d){return (d.sourceFacingFlags&1u)!=0;}
/// Spell.cpp:7360 in full: a player caster with the flag needs the target in a
/// pi arc, unless it is inside the target's boundary radius floored at
/// MIN_MELEE_REACH (Unit::IsWithinBoundaryRadius, Unit.cpp:820-828).
inline bool localSpellFacingReady(float casterX,float casterY,float orientation,
                                  float targetX,float targetY,float targetZ,float casterZ,
                                  float targetBoundingRadius) {
    if(localHasInArcPi(casterX,casterY,orientation,targetX,targetY))return true;
    const float dx=casterX-targetX,dy=casterY-targetY,dz=casterZ-targetZ;
    return localWithinBoundaryRadius(dx*dx+dy*dy+dz*dz,targetBoundingRadius);
}
/// Unit::IsInCombat for a local creature: the reference's UNIT_FLAG_IN_COMBAT is
/// set while the threat list holds anyone or a victim is selected, which is
/// exactly the two fields this build keeps.
inline bool localNpcInCombat(const LocalRealmNpc& n) {
    if(n.targetGuid)return true;
    for(const auto& e:n.threat)if(e.guid)return true;
    return false;
}
}
