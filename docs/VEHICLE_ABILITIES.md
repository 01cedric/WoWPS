# Authored vehicle abilities

This is a bounded local vehicle weapon runtime, independent of player spells,
talents, mana and equipment. It does not import retail vehicle spell effects.
Supported kits connect to the original VehicleMenuBar APIs; the native
bar remains a fallback. See [VEHICLE_FRAME_XML.md](VEHICLE_FRAME_XML.md) for the
shared action view, controls, events and remaining visual acceptance.

## Content

`world.json` accepts up to 64 `vehicleKits`. A kit's `id` matches an authored
spawn's `vehicleId`; multiple spawns may share an immutable kit but have separate
live resources. Existing vehicles without a kit retain their previous behavior.
Every ability seat must exist on every spawn using that kit.

```json
"vehicleKits": [{
  "id": 700,
  "maxPower": 100,
  "regenPerSecond": 10,
  "abilities": [
    {"slot": 1, "spellId": 9001, "seatMask": 3, "powerType": 1,
     "powerCost": 30, "cooldownMs": 2000, "castTimeMs": 1000,
     "damage": 90, "schoolMask": 4, "areaRadius": 5, "range": 30},
    {"slot": 2, "spellId": 9002, "seatMask": 1, "powerCost": 20,
     "cooldownMs": 3000, "repair": 60}
  ]
}]
```

Slots are 1–6 in content and F1–F6 in the UI; command slots are zero-based.
Each kit must have at least one ability. Spell IDs and slots must be unique
within the kit. `spellId` must resolve to authored spell metadata already in the
world; it supplies the name/event identity, **not** the effect implementation.
Kit amounts, costs and cooldowns are explicit authored rules. Referencing a
retail spell ID does not implement that retail spell.

`seatMask` uses bit 0 for seat 0, bit 1 for seat 1, etc. The example allows both
first seats to fire, and only the first seat to repair. Each ability has exactly
one nonzero `damage` or `repair` amount (maximum 1,000,000). Without
`projectileSpeed`, damage is a direct hit. Physical damage is reduced by armor;
an authored single elemental school bypasses armor. Optional bounded cast time
and area damage are described in [VEHICLE_EFFECTS.md](VEHICLE_EFFECTS.md).
There is no hit/crit roll or player scaling. Repair heals only the current
vehicle; a full-health vehicle rejects repair without spending power.

Power is bounded at 1,000,000; regeneration at 10,000 units/second. An ability's
cost cannot exceed the pool. Cooldowns range from 100 to 60,000 ms. All accepted
abilities also start a fixed shared 1,000 ms global cooldown. Regeneration and
cooldowns tick once per vehicle, with fractional time preserved. Boarding,
changing seats and changing gunners do not refill energy or clear cooldowns.

Projectile profiles, aim and flight are described in [VEHICLE_PROJECTILES.md](VEHICLE_PROJECTILES.md).

## Authority and combat

The host checks the exact current vehicle GUID, life state, seat permission,
map/instance/phase, energy and cooldowns. Direct targeted damage requires a living attackable NPC
outside transports/vehicles, a finite 3D distance within the authored range
(maximum 60 yards) and the existing collision-pack line-of-sight gate. Without
collision coverage that gate permits the shot, as for existing local spells.
Its origin currently uses the existing fixed-height ray, not a model muzzle.
All validation precedes resource spending. A cast-time ability commits its
resource and cooldown only after successful completion. PvP and arbitrary
player targets are not supported.

The vehicle GUID owns damage and threat. The first engaging player's tag and
existing party/quest/XP rules own rewards. Kill events keep the creature actor's
identity; player resources and proc identities are not substituted. Occupants
receive vehicle hit/repair observations through the existing combat text path.

Enemies pursue and melee the hull, including aggressive acquisition before the
first shot. The vehicle's armor applies; this bounded retaliation path has no
NPC spell casting, avoidance/crit rolls, vehicle immunities or occupant splash
damage. Destroying the hull clears its seats through the existing forced-exit
path, produces death observations and schedules its ordinary NPC respawn. A
respawn gets a fresh pool; an empty hull under attack stays in the actor roster.

## Controls and replication

Keyboard F1–F6 activates the corresponding allowed slot against the selected
enemy for direct weapons; free-flight weapons fire along confirmed aim with no
target, and repair automatically names the hull. Keyboard V or the native button
changes seat. Keyboard 4 or the Exit button exits. The PS4 source bindings use
L1/R1 to select a permitted ability, R2+Square to use it and plain Square to exit.
Open original UI panels retain their navigation. Hardware input and visual
layout still require console acceptance.

The ability resource block adds 32 bytes per NPC: power, global cooldown and six ability cooldowns.
The decoder validates these against the installed kit. Owner progress now puts
vehicle occupancy before combat views, so a view may name only the currently
occupied hull in addition to the player. Wrong sessions, replayed command IDs,
gaps, truncation and malformed power/cooldown values remain rejected. This was introduced in LAN97; both peers now
must use LAN100 and matching content. Since LAN98 the eight seat angles pack into
24 bytes and carries a per-viewer vehicle combat flag. The existing two-NPC packet budget still
fits its 1,400-byte ceiling.

The separate bounded cast deck carries progress without increasing the already
full NPC packet. Save41 adds unrelated object/escort state. Vehicle actors,
power, cooldowns, aim, casts and projectiles are transient world state. Character quest rewards use the existing save path; restart restores
exit recovery rather than a live seat or hull.

## Scope and verification

`tools/tests/run_local_vehicle_ability_tests.sh` builds and runs the production
runtime/codec test against `fixtures/vehicle_ability_world.json`. The fixture is
synthetic and is not installed as retail content. The production `world.json`
still has no vehicle kits. Existing `.wvhc` editor catalogs are not automatically
converted into these local kits.

Still required for full vehicle support: original spell effect import, the
verified original FrameXML vehicle presentation, full retail effect simulation, proper
muzzle/seat/bone attachments, physics/collision movement, vehicle-specific
resources and immunities, NPC spell retaliation, original encounter scripts
and console validation. Overall milestone 5 remains incomplete.
