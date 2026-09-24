# Authored combat escorts — The local runtime

An existing `escortRoutes` row can opt into guide combat. This extends the local
authored route runtime; it does not import retail escort scripts or encounters.

```json
{
  "id": 900,
  "questId": 1,
  "spawnId": 1,
  "combat": true,
  "combatChaseRadius": 10,
  "speed": 4,
  "followRadius": 15,
  "failRadius": 30,
  "timeoutMs": 30000,
  "points": [{"x": 8, "waitMs": 500}, {"x": 12}]
}
```

`combat` defaults to false. `combatChaseRadius` is allowed only when combat is
enabled. Its default is the smaller of 10 yards and the follow radius; accepted
values are 1–30 yards and cannot exceed the route's follow radius. A combat guide
must have a nonzero authored NPC `damage` amount. Existing limits still apply:
32 routes, one route per quest and guide spawn, 64 waypoints, and open-world
friendly quest-giving guides. Active guides share the bounded NPC roster.

## Authority and damage

An aggressive enemy can acquire the active guide using its normal aggro radius
and the escort owner's faction/phase eligibility. It chooses between nearby
players and eligible guides; it does not automatically abandon existing threat.
The guide defends itself and its owner against already-engaged attackable NPCs.
It does not proactively attack neutral bystanders or defend unrelated players.

Both sides are actual NPC actors. Guide attacks use the guide GUID, level and
authored damage; incoming hits reduce guide health and use its authored armor.
The escort owner's health, armor, talents and resources are not substituted.
Supported damage is a direct physical melee hit every two seconds, reduced by
the existing armor rule and checked against creature melee reach. There are no
critical/avoidance rolls, NPC spell casts or healing spells targeting the guide
in this escort combat path. Stuns prevent guide movement and swings.

The guide owns the threat produced by its attacks. Existing first-tag, party,
quest and experience rules allocate supported rewards to the player; combat
history retains the guide's real actor identity. Existing NPC snapshots expose
guide health, position, death and attack target to LAN guests. Guide observations
are not reattributed to the player's combat-text caster identity.

## Movement, pauses and collision

During combat, guide pursuit is bounded around the point where its current
defensive engagement began. Enemies outside that radius are not chased. Ordinary
enemy leash rules remain in force. The guide pauses its authored route and
waypoint wait while it has a combat target, is under attack, is stunned, or its
owner is in NPC combat. Owner follow distance also pauses route progress. The
overall online timeout continues during these pauses.

When the engagement ends, the guide continues toward the same unconsumed
waypoint from its current position. Guide movement updates the saved route
position; restarting does not replay a completed waypoint. Combat routes process
movement, waits and completion after NPC combat and area phase changes, so a
guide that dies in that tick cannot also earn route-completion credit.

Installed collision geometry gates route movement, defensive pursuit and enemy
pursuit of a guide. Movement checks a low ray at 0.25 yards and a body-height ray
using the existing fixed collision height. Melee and aggressive guide acquisition
also require a clear body-height ray. These checks include installed M2 geometry.
A 0.01-yard extension at both ends of each nonzero query includes contacts exactly
on a step boundary; it does not move the actor or alter the general collision API.
A blocked step leaves the actor in place and consumes no waypoint or wait edge.

These are segment obstruction gates, not a swept capsule, terrain-height solver,
ground support check or navmesh search. Without a collision pack covering the
map, the existing collision service permits movement and attacks. A map entry
alone does not prove complete geometry coverage. Authored routes still need
validation against actual client assets and console movement.

## Persistence and lifecycle

Save41 adds `guideHealth` as one unsigned 32-bit field after the escort progress
XYZ fields. The complete escort progress record is now 32 bytes. The same field
is included in LAN99 owner progress. Saves through version 40 have no guide
health field; legacy zero health is initialized once from the authored guide
definition. New active routes capture the guide's current health.

Guide health is synchronized after damage and restored with the route on resume.
Reconnecting to an existing saved route does not refill the guide. An offline
owner releases the shared live actor; a living released guide returns home with
full health for subsequent reservations, while the offline player's saved route
retains its own health and progress. A reconnect restores that saved health.
There is no additional guide health regeneration during an active escort.

Guide death immediately emits creature death/kill observations, fails the escort
once, clears ownership and leaves a non-lootable corpse for the normal authored
NPC respawn timer. The quest remains active unless authored script effects change
it; abandon/reaccept starts a new route. Guide respawn does not resume a failed
escort. Living release on completion, abandonment or other failure returns the
guide home and removes its threat references from enemies.

Owner death, ghost state, travel, vehicle boarding, map/instance/phase mismatch,
quest removal, excessive separation, timeout and conflicting reservations retain
the existing failure rules. A failed failure-script mutation is logged and does
not retain control of the actor. Waypoint and completion script failures preserve
their unconsumed edge for a later retry. Quest accept/abandon still use the realm's
save-before-success transaction.

## Verification and remaining scope

`tools/tests/run_local_escort_combat_tests.sh` builds the production runtime and
codecs with the synthetic `fixtures/escort_combat_world.json`. Its checks cover
NPC acquisition and retaliation, guide caster identity and player reward credit,
wait/completion ordering, HP persistence and legacy migration, death and respawn,
geometry gates, phase cleanup, bounded pursuit and content validation. See the
release validation report for actual executed test results.

Original navmesh paths, terrain-aware walking, authored ambush spawning, dialogue
playback, retail script import, NPC spell encounters and healing the guide remain
outside this package. The synthetic fixture is not installed retail quest content.
Overall milestone 5 is not completed by this combat escort runtime alone.
