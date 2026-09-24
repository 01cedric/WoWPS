#pragma once
#include "game/local_gameplay.hpp"
#include "game/local_line_of_sight.hpp"

namespace wowee::game {
struct LocalEscortCombatState { float anchorX=0,anchorY=0,anchorZ=0; };

// The shared collision ray deliberately excludes both endpoints. Movement
// needs boundary contacts: otherwise a step ending exactly on a thin wall can
// start its next step on that wall and pass through it. Extend only this local
// query, leaving the actor position and the source-referenced ray API unchanged.
inline bool localEscortClosedSegmentClear(const LocalCollisionData& collision,uint32_t map,
    float ax,float ay,float az,float bx,float by,float bz) {
    const float dx=bx-ax,dy=by-ay,dz=bz-az;
    const float length=std::sqrt(dx*dx+dy*dy+dz*dz);
    if(!std::isfinite(length))return false;
    if(length<=1e-6f)return true;
    const float pad=.01f/length;
    return collision.isInLineOfSight(map,ax-dx*pad,ay-dy*pad,az-dz*pad,
                                     bx+dx*pad,by+dy*pad,bz+dz*pad,false);
}

// Authored movement remains a polyline, not a ground/navmesh solver. Installed
// geometry blocks both a low and a body-height ray before a position changes.
inline bool localEscortMotionAllowed(const LocalCollisionData& collision,uint32_t map,
    float ax,float ay,float az,float bx,float by,float bz) {
    if(!std::isfinite(bx)||!std::isfinite(by)||!std::isfinite(bz)||
       std::abs(bx)>100000||std::abs(by)>100000||std::abs(bz)>20000)return false;
    return localEscortClosedSegmentClear(collision,map,ax,ay,az+.25f,bx,by,bz+.25f) &&
        localEscortClosedSegmentClear(collision,map,ax,ay,az+kLocalCollisionHeight,bx,by,bz+kLocalCollisionHeight);
}
inline bool localEscortMeleeVisible(const LocalCollisionData& collision,const LocalRealmNpc& a,const LocalRealmNpc& b) {
    return a.mapId==b.mapId && a.instanceId==b.instanceId &&
        localEscortClosedSegmentClear(collision,a.mapId,a.x,a.y,a.z+kLocalCollisionHeight,b.x,b.y,b.z+kLocalCollisionHeight);
}
} // namespace wowee::game
