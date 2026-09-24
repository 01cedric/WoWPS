#pragma once
#include "game/local_gameplay.hpp"
namespace wowee::game {
inline bool localCombatWithNpc(const LocalRealmPlayer& p,const LocalRealmNpc& n) {
    if(p.dead||!p.health||n.dead||!n.health||p.mapId!=n.mapId||p.instanceId!=n.instanceId)return false;
    if(p.vehicleGuid && (n.targetGuid==p.vehicleGuid ||
       (n.playerThreat.viewerGuid==p.guid && n.viewerVehicleCombat) ||
       std::any_of(n.threat.begin(),n.threat.end(),[&](const auto& e){return e.guid==p.vehicleGuid && e.amount;})))return true;
    if(n.targetGuid==p.guid||p.attackTarget==n.guid)return true;
    if(n.playerThreat.viewerGuid==p.guid)return n.playerThreat.present;
    return std::any_of(n.threat.begin(),n.threat.end(),[&](const auto& e){return e.guid==p.guid&&e.amount;});
}
inline bool localCombatActive(const LocalRealmPlayer& p,const std::vector<LocalRealmNpc>& npcs) {
    return std::any_of(npcs.begin(),npcs.end(),[&](const auto& n){return localCombatWithNpc(p,n);});
}
}
