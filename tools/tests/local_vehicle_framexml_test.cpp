#include "addons/local_vehicle_api.hpp"
#include <cassert>
#include <iostream>

using namespace wowee;
int main() {
    game::LocalRealmPlayer rider;
    game::LocalRealmNpc hull;
    game::LocalVehicleKit kit;
    rider.guid=7;rider.vehicleGuid=99;rider.vehicleId=15;rider.health=100;
    hull.guid=99;hull.vehicleId=15;hull.vehicleSeatCount=3;
    hull.health=500;hull.maxHealth=1000;hull.vehiclePower=30;
    kit.id=15;kit.maxPower=100;
    kit.abilities[0].spellId=100;kit.abilities[0].seatMask=1;
    kit.abilities[0].powerCost=20;kit.abilities[0].cooldownMs=5000;
    kit.abilities[0].damage=50;
    kit.abilities[1].spellId=101;kit.abilities[1].seatMask=2;
    kit.abilities[1].projectileSpeed=30;kit.abilities[1].damage=80;
    kit.abilities[2].spellId=102;kit.abilities[2].seatMask=3;kit.abilities[2].repair=100;
    addons::LocalVehicleView view{nullptr,&rider,&hull,&kit};
    // The original bonus-page calculation must expose six weapon slots and
    // shadow the remaining slots without aliasing the persisted player page.
    const int page=6+addons::kLocalVehicleBonusOffset;
    assert(addons::localVehicleActionIndex((page-1)*12+1)==0);
    assert(addons::localVehicleActionIndex((page-1)*12+6)==5);
    assert(addons::localVehicleActionIndex(120)==-1);
    assert(addons::localVehicleActionIndex(132)==11);
    assert(addons::localVehicleActionIndex(133)==-1);
    assert(view.active() && view.usable(0) && !view.aimed());
    assert(!view.ability(-1) && !view.ability(6) && !view.ability(11));
    assert(!view.ability(1) && view.target(0,456)==456 && view.target(2,456)==99);
    // Hull cooldown and energy survive a driver/gunner handoff. The stronger
    // of the authored weapon cooldown and the shared GCD owns the sweep.
    hull.vehicleCooldownMs[0]=3200;hull.vehicleGlobalCooldownMs=700;
    assert(view.cooldown(0)==3200 && view.cooldownTotal(0)==5000);
    hull.vehicleCooldownMs[0]=200;
    assert(view.cooldown(0)==700 && view.cooldownTotal(0)==1000);
    hull.vehiclePower=19;assert(!view.usable(0));hull.vehiclePower=30;
    hull.health=hull.maxHealth;assert(!view.usable(2));hull.health=500;
    hull.controls.emplace_back();hull.controls.back().kind=uint8_t(game::LocalNpcControlKind::Stun);
    hull.controls.back().remainingMs=500;
    assert(!view.usable(0));hull.controls.clear();assert(view.usable(0));
    rider.vehicleSeat=1;
    assert(!view.ability(0) && view.ability(1) && view.aimed() && view.target(1,456)==0);
    assert(hull.vehicleCooldownMs[0]==200 && hull.vehiclePower==30);
    rider.vehicleSeat=2;assert(!view.ability(0) && !view.ability(1) && !view.aimed());
    rider.vehicleSeat=3;assert(!view.active());rider.vehicleSeat=0;
    // Stale snapshots must never expose another hull, instance, or phase as
    // the active action source, even before the next presentation event.
    rider.vehicleGuid=98;assert(!view.active());rider.vehicleGuid=99;
    rider.vehicleId=16;assert(!view.active());rider.vehicleId=15;
    rider.mapId=1;assert(!view.active());rider.mapId=0;
    rider.instanceId=1;assert(!view.active());rider.instanceId=0;
    hull.requiredPhaseMask=2;rider.phaseMask=1;assert(!view.active());
    rider.phaseMask=2;assert(view.active());
    rider.dead=true;assert(!view.usable(0));rider.dead=false;
    rider.ghost=true;assert(!view.usable(0));rider.ghost=false;
    rider.health=0;assert(!view.usable(0));rider.health=100;
    hull.dead=true;assert(!view.usable(0));hull.dead=false;
    rider.vehicleGuid=0;assert(!view.active());
    std::cout << "PASS vehicle FrameXML view: bonus page, seat permissions, hull resources, target semantics, stale snapshot rejection\n";
}
