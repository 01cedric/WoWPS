# Authored vehicle casts and area effects

The local vehicle profile supports a bounded set of effects without treating a
retail spell ID as an implementation. The immutable kit owns every amount,
school, radius, cast time and power rule. The spell row supplies only the name
and combat-event identity.

## Ability fields

| Field | Accepted values | Default |
|---|---:|---:|
| `castTimeMs` | 0–10,000 ms | 0 |
| `areaRadius` | 0–40 yards | 0 |
| `schoolMask` | one bit: 1, 2, 4, 8, 16, 32 or 64 | 1 physical |
| `powerType` | 0 none, 1 vehicle energy | 1 |
| `interruptOnMove` | Boolean | true |

`powerType: 0` requires `powerCost: 0`. Energy abilities use the existing
shared hull pool. This profile does not invent additional mana, rage or rune
pools. Repairs remain single-hull physical healing and therefore reject an
area radius or a nonphysical school.

Physical damage passes through the existing vehicle-level armor calculation.
Holy, fire, nature, frost, shadow and arcane damage bypass armor and retain the
authored school on combat events. The vehicle profile has no resistance, hit,
critical, absorption or player-stat scaling model.

Direct area damage is centered on the selected victim. Projectile area damage
is centered on the first authoritative collision point. It affects living,
attackable non-vehicle NPCs in the same map, instance and visible phase. The
primary target is included once; other targets are resolved in GUID order.
Players, pets, vehicles and transport passengers are excluded.

## Cast lifecycle

One hull can own one pending vehicle cast. Up to 16 casts can exist across a
realm. The host validates the target, line of sight, range, seat, resource and
cooldown before starting, but it reserves neither power nor cooldown. At
completion the host repeats those checks and performs one atomic commit. A
failed or interrupted cast spends nothing and starts no cooldown.

The cast is canceled when its owner disconnects, dies, becomes a ghost, leaves
the hull, changes seat, map, instance, phase or position revision; when the
hull dies, respawns, changes incarnation or becomes stunned; when a direct
target dies or changes incarnation; and on explicit Cancel Cast or Stop Attack.
Movement or rotation also cancels an ability whose `interruptOnMove` is true.
An ability with false can complete while the hull moves. Projectile casts
snapshot confirmed yaw and pitch when they start.

The public cast view contains source, owner, target, spell, remaining and total
milliseconds, map, instance, phase, slot and seat. Authority lifecycle and aim
snapshots are not sent. LAN100 publishes the complete bounded list atomically;
stale, malformed, duplicate or content-mismatched lists cannot replace the last
accepted view. Casts are transient and never enter the character save.

`tools/tests/run_local_vehicle_effects_tests.sh` exercises cast completion and
cancellation, resources, direct and projectile areas, schools, aim snapshots,
movement policy and strict profile/view validation with synthetic content.
The fixture is not installed game content.

