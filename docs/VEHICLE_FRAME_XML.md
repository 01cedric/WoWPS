# Local vehicles in the original FrameXML interface

This bridge exposes the implemented local vehicle profile to the existing
3.3.5 `VehicleMenuBar`. It does not import retail vehicle behavior or infer
`Vehicle.dbc` / `VehicleUIIndicator.dbc` records from local kit identifiers.
The original interface files and textures must be available to the client.

## Supported presentation

- A seated player's current hull is the `vehicle` unit token. Name, level,
  health, maximum health, energy, maximum energy, existence and death queries
  read the local realm snapshot. Its power type is `3, "ENERGY"`.
- `UnitHasVehicleUI("player")` reports a valid current hull/kit/seat, including
  matching map, instance and visible phase. `UnitVehicleSkin("player")` uses
  the original `Mechanical` fallback skin while that interface is available.
- Bonus offset 5 gives the original six always-bonus buttons actions 121–126.
  Seat masks determine which actions exist; slots 127–132 are empty while the
  vehicle page is active. The character's stored action bars are never changed.
- Action queries supply spell identity/icon, usability, power shortage, direct
  weapon range and hull-owned cooldowns. The longer remaining weapon cooldown
  or shared 1-second global cooldown determines the displayed sweep.
  Consumable, stackable, equipped, auto-attack and auto-repeat decorations are
  suppressed for this temporary page. Drag/drop cannot rewrite it.
- `UseAction` calls the existing authoritative vehicle command. Direct weapons
  use the selected target, repairs use the hull, and free-aim projectiles use
  target zero. Projectile firing waits until the shared aim edit is confirmed.
- Action tooltips describe the authored local energy cost, cooldown, damage or
  repair amount and range. They do not substitute player spell scaling or an
  unsupported retail spell description for the vehicle effect.
- Once the original bar and its button widgets are present and the bar is
  visible, the native ability window is suppressed. The native window remains
  the fallback while the original interface is unavailable. Control hints,
  button focus outlines and native ballistic guides remain supplementary.

## Events and controls

The bridge publishes entry/exit events, vehicle-data gain/loss, passenger
changes, bonus-page/action-slot changes, health/energy changes, action
usability/cooldown changes and normalized `VEHICLE_ANGLE_UPDATE` values. It
tracks state changes rather than issuing action updates every rendered frame.
The sixth entry-event argument and vehicle-data indicator argument are zero.

`IsVehicleAimAngleAdjustable()` returns numeric 1 or nil: the original skin
performs arithmetic with that result. `VehicleAimGetNormAngle()` reads the
confirmed pitch. The original pitch click control and held up/down buttons
feed `VehicleAimRequestNormAngle` and the up/down start/stop APIs. These share
the same edit buffer as keyboard, pad and fallback sliders: at most four
submissions per second, one outstanding command, coalesced edits, a two-second
confirmation timeout and reset on rider/hull/seat identity changes. Original
slider placement and ballistic guides follow confirmed snapshot values.

| Input | Action |
| --- | --- |
| Mouse on original vehicle action button | Use that seat's weapon or repair |
| Mouse on original pitch control / pitch up/down | Set / adjust elevation |
| F1–F6 | Use vehicle ability 1–6 |
| Numpad 4 / 6 | Adjust hull-relative yaw |
| Numpad 8 / 2 | Raise / lower elevation |
| V | Request next available seat |
| 4 | Exit vehicle |
| PS4 L1 / R1 | Select an available vehicle ability |
| PS4 R2 + Square | Use selected ability |
| PS4 L2 + L1 / R1 | Adjust yaw |
| PS4 R2 + L1 / R1 | Lower / raise elevation |
| PS4 L2 + Square | Request next available seat |
| PS4 Square without modifiers | Exit vehicle |

The selected ability is outlined on its original button. Open panels retain
their existing pad navigation. Camera sticks and D-pad bindings are unchanged.
The original leave button and `VehicleExit`, `VehicleNextSeat`,
`VehiclePrevSeat` and `UnitSwitchToVehicleSeat` use the existing realm commands.
`CanSwitchVehicleSeat` and `UnitVehicleSeatInfo` read published occupancy;
control types are `Root`, `Child` and `None` for driver, gunner and passenger.

## Boundaries

The seat indicator is intentionally absent: `GetVehicleUIIndicator` returns
nil and zero seats, and no original indicator/seat coordinates are invented.
Passenger ejection is unavailable. There is no adjustable launch-power model,
vehicle-specific skin database, original projectile spell-effect rendering,
or claim of complete retail vehicle parity. Weapon range remains the authored
travel-distance limit for projectiles; the existing projectile authority and
its lifetime, capacity, collision and cancellation rules are unchanged.

The range indicator checks direct-target distance and location. The authority
still decides target legality and line of sight. UI availability alone does
not bypass any authority check. The generic mechanical artwork is original
FrameXML presentation; trajectory guides and live projectile markers are
native presentation.

## Validation entry points

`bash tools/tests/run_local_vehicle_framexml_tests.sh` checks bonus-page
mapping, seat permission changes, persistent hull resources, shared cooldown
selection, repair/stun/death usability, projectile target-zero semantics and
stale hull/map/instance/phase rejection. The existing
`bash tools/tests/run_local_vehicle_aim_ui_tests.sh` covers acknowledgement,
coalescing, rate limits, timeout, identity resets and LAN angle quantization.

These tests and the shared host syntax pass are to be run by the integration
build. This document does not record a successful run. Original FrameXML
rendering, mouse interaction, controller interaction and entry/exit transitions
still require an in-game check with the supported client assets; no PS4
hardware result is claimed here.

## Original interface sources consulted

- [VehicleMenuBar.lua](https://github.com/NexXuZR/3.3.5-Interface-Files/blob/main/VehicleMenuBar.lua): skin arithmetic, six buttons, pitch controls, unit bars and seat events.
- [VehicleMenuBar.xml](https://github.com/NexXuZR/3.3.5-Interface-Files/blob/main/VehicleMenuBar.xml): widget names, action-button inheritance and control callbacks.
- [ActionButton.lua](https://github.com/wowgaming/3.3.5-interface-files/blob/main/ActionButton.lua): bonus-page calculation and action queries.
- [MainMenuBar.lua](https://github.com/wowgaming/3.3.5-interface-files/blob/main/MainMenuBar.lua): vehicle entry/exit presentation transitions.
