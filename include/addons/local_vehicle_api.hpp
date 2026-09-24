#pragma once
#include <algorithm>
#include "game/local_realm.hpp"
#include "game/local_npc_auras.hpp"

namespace wowee::addons {
// A transient bonus page for the original six vehicle buttons. Never write
// this view into the character's persisted action bars.
inline constexpr int kLocalVehicleBonusOffset=5;
inline constexpr int kLocalVehicleFirstAction=121;
inline int localVehicleActionIndex(int action) {
    return action>=kLocalVehicleFirstAction && action<kLocalVehicleFirstAction+12?
        action-kLocalVehicleFirstAction:-1;
}
struct LocalVehicleView {
    game::LocalRealm* realm=nullptr;
    const game::LocalRealmPlayer* player=nullptr;
    const game::LocalRealmNpc* hull=nullptr;
    const game::LocalVehicleKit* kit=nullptr;
    bool pendingCast=false;
    bool active()const {
        return player && hull && kit && player->vehicleGuid==hull->guid &&
            player->vehicleId==hull->vehicleId && player->vehicleSeat<hull->vehicleSeatCount && player->vehicleSeat<8 &&
            player->mapId==hull->mapId && player->instanceId==hull->instanceId &&
            game::localPhaseVisible(player->phaseMask,hull->requiredPhaseMask,hull->excludedPhaseMask);
    }
    const game::LocalVehicleAbility* ability(int index)const {
        if(!active() || index<0 || index>=int(game::kLocalVehicleAbilities))return nullptr;
        const auto& a=kit->abilities[size_t(index)];
        return a.spellId && (a.seatMask&(1u<<player->vehicleSeat))?&a:nullptr;
    }
    bool alive()const{return active() && !player->dead && !player->ghost && player->health &&
        !player->flight.active && !player->transportEntry && !hull->dead && hull->health;}
    bool casting()const {
        return active() && pendingCast;
    }
    bool aimed()const {
        for(int i=0;i<int(game::kLocalVehicleAbilities);++i)if(const auto* a=ability(i);a && a->projectileSpeed>0)return true;
        return false;
    }
    bool usable(int index)const {
        const auto* a=ability(index);
        return a && alive() && !casting() && !game::localNpcStunned(*hull) &&
            (a->powerType==game::LocalVehiclePowerType::None || hull->vehiclePower>=a->powerCost) &&
            (!a->repair || hull->health<hull->maxHealth);
    }
    uint32_t cooldown(int index)const {
        return ability(index)?std::max(hull->vehicleCooldownMs[size_t(index)],hull->vehicleGlobalCooldownMs):0;
    }
    uint32_t cooldownTotal(int index)const {
        const auto* a=ability(index);if(!a)return 0;
        return hull->vehicleCooldownMs[size_t(index)]>=hull->vehicleGlobalCooldownMs?a->cooldownMs:1000;
    }
    uint64_t target(int index,uint64_t selected)const {
        const auto* a=ability(index);return !a || a->projectileSpeed>0?0:a->repair?hull->guid:selected;
    }
};
inline LocalVehicleView localVehicleView(game::LocalRealm* realm) {
    LocalVehicleView v;v.realm=realm;v.player=realm?realm->localPlayer():nullptr;
    if(v.player && v.player->vehicleGuid)for(const auto& n:realm->npcs())
        if(n.guid==v.player->vehicleGuid){v.hull=&n;v.kit=realm->content().vehicleKit(n.vehicleId);break;}
    if(v.active()) {
        const auto casts=realm->vehicleCasts();
        v.pendingCast=std::any_of(casts.begin(),casts.end(),[&](const auto& cast){
            return cast.sourceGuid==v.hull->guid && cast.ownerGuid==v.player->guid;
        });
    }
    return v;
}
} // namespace wowee::addons
