#pragma once
#include "game/local_gameplay.hpp"
#include <cmath>
namespace wowee::game {
inline LocalNpcThreatView localThreatView(const LocalRealmNpc& n,const LocalRealmPlayer& p) {
    LocalNpcThreatView view;view.viewerGuid=p.guid;
    if(n.dead||p.dead||p.mapId!=n.mapId||p.instanceId!=n.instanceId)return view;
    if(n.playerThreat.viewerGuid==p.guid)return n.playerThreat;
    uint64_t tank=0,highest=0;
    for(const auto& e:n.threat) {
        if(e.guid==p.guid)view.amount=e.amount;
        if(e.guid==n.targetGuid)tank=e.amount;
        highest=std::max(highest,e.amount);
    }
    if(!view.amount||!tank){view.amount=0;return view;}
    view.present=true;
    view.rawBasisPoints=uint32_t(std::min(uint64_t(1000000),view.amount*10000/tank));
    if(n.targetGuid==p.guid){view.status=highest>tank?2:3;view.scaledBasisPoints=10000;}
    else {
        const double dx=double(n.x)-p.x,dy=double(n.y)-p.y,dz=double(n.z)-p.z;
        const uint64_t threshold=dx*dx+dy*dy+dz*dz<=4.5*4.5?110:130;
        view.status=view.amount>=tank?1:0;
        view.scaledBasisPoints=uint16_t(std::min(uint64_t(10000),view.amount*1000000/(tank*threshold)));
    }
    return view;
}
}
