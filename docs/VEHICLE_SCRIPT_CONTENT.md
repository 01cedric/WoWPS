# Local vehicles and script-backed quests

This implements a bounded local/LAN ground-vehicle foundation. It does not import
original vehicle encounters or enable the separate retail vehicle action bar.

## Authored vehicles

An ordinary `world.json` NPC spawn can declare `vehicleId`, `vehicleSeatCount`
(1–8), and `vehicleControllerSeat` (zero based). `vehicleId` is a script identity,
not an NPC entry or a promise that Vehicle.dbc has been imported. Required and
excluded phase masks apply to entry and continued occupancy.

Square on a friendly vehicle requests the driving seat, or the first free passenger
seat. Square while aboard requests exit; the original leave-vehicle UI also routes
to the local authority. The host checks range, life/combat state, phase and seat
occupancy. Normal character actions require leaving the vehicle first.

Only the controller seat may submit movement. A simulation-time budget permits
7 yards per second with at most 3.5 yards of accumulated allowance. More packets
do not create more allowance. Passengers follow the authority actor. Ground
collision still comes from the existing character movement path; this does not
introduce server VMAP validation or a vehicle-sized collision hull.

All seats currently use the actor origin. Vehicle-specific seat offsets,
animations, weapons, flying vehicles, vehicle abilities and encounter content are
not implemented by this checkpoint. No shipped NPC is relabeled as a vehicle just
to make a feature appear complete.

## Scripts and quest objectives

`vehicleEnter` and `vehicleExit` script triggers use `sourceId = vehicleId`.
Multiple rows for an event are staged together, including state, phase and timer
changes. Explicit exit is rejected atomically if its script fails. Forced exit
(death, invalid phase/actor, disconnect) releases the seat even on script failure;
an exit-recovery marker retains the pending cleanup for the next load.

A quest objective can use:

```json
{"type":"script", "entry":500, "count":2, "text":"Complete the scripted event"}
```

`entry` names an authored script-state ID. Positive state values award bounded,
monotonic objective credit while the quest is in the log. Resetting temporary
script state does not remove credit already earned. A completion trigger still
runs only on the transition to Complete. An author should reset the event state
on `questAccept` when starting a fresh attempt.

`questAbandon` triggers use the quest ID. They can cancel its timers and reset
event state before the quest is removed; failure keeps the quest and script state
unchanged. The production loader validates vehicle, quest, state and timer
references. `tools/tests/fixtures/vehicle_script_world.json` is a synthetic example
for the regression suite, not added retail game content.

## Save and network boundaries

Save38 reads versions 1–37. A save records the script-visible ID needed for exit
cleanup, never a live seat or NPC handle. Initialization applies the exit script
and leaves the character unseated at the saved position. LAN93 separately carries
current seat identity/permission in owner progress and public player snapshots,
and seat metadata in NPC snapshots. Every LAN peer must use matching code/content.

Tests exercise production gameplay, save/progress codecs, host position handlers
and stale snapshot ordering. They do not certify original MPQ presentation,
two-console networking, vehicle geometry or PS4 performance.

## Persistent escorts and seats

Spawns may add `vehicleSeatOffsets`, at most eight rows with `seat`, `x`, `y`
and `z`. Offsets are bounded to +/-20 yards and rotate with the actor's server
yaw. Seat indices must be unique and exist. The controller seat must remain at
the zero offset because current driver movement uses the ground origin.

`SwitchVehicleSeat` (LAN96) checks the current actor GUID, life, phase, map,
instance, target seat and occupancy on the host. It changes driver authority,
bumps the position revision and clears the player's movement allowance. A seat
switch does not repeat enter/exit scripts. Switching seats cannot grant a fresh
movement budget. Keyboard V cycles free seats; `VehicleNextSeat()` and
`VehiclePrevSeat()` route original Lua calls to the same authority. The local
`UnitVehicleSeatInfo` reader exposes the roster; ejection stays unsupported.

These are static authored seat offsets, not M2 bone attachments or retail seat
animations. The existing vehicle action bar remains disabled: vehicle spell
execution, energy, aim/projectiles and the corresponding original UI are still
open. Existing D-pad camera controls are retained.

## Vehicle abilities

Authored weapon kits, native ability controls and shared transient resources are
now implemented as described in [VEHICLE_ABILITIES.md](VEHICLE_ABILITIES.md).
LAN97 replaces LAN96; Save40 is unchanged. Earlier limitations above describe
the historical checkpoints; original vehicle effects/UI and physics remain open.
