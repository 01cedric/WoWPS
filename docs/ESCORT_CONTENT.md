# Authored escorts

Opt-in guide combat adds saved/replicated guide health and geometry gates.
See [ESCORT_COMBAT.md](ESCORT_COMBAT.md). The remaining
text documents the original route contract, with those extensions taking precedence.

An `escortRoutes` row binds one authored NPC spawn to one quest. Accepting that
quest from that exact spawn starts the route and its `escortStart` event in the
same player transaction. Quest acceptance and abandonment now save before success.

Fields: positive unique `id`, existing `questId` and `spawnId`, `speed` (0.1–7,
default 2.5 yards/s), `followRadius` (5–50, default 25), `failRadius` (at least the
follow radius and at most 150, default 100), `timeoutMs` (1–1800000, default
600000), and 1–64 `points` with server `x/y/z` and `waitMs` (0–60000). At most 32
routes are accepted. Each quest and spawn may belong to one route. The spawn
must be the quest's friendly quest-giving NPC and cannot be a vehicle. Start is
open-world only.

Events `escortStart`, `escortWaypoint`, `escortComplete` and `escortFail` use the
route ID as their source. Waypoint events occur in route order; conditions and
counters can distinguish each visit. Arrival saves the next index before a later
tick can visit again. At most one waypoint is processed per tick. Authored waits
include the last waypoint's wait before completion.

The guide waits outside follow range or while the player is in NPC combat. It
fails on excessive distance, timeout, player death, map/instance/phase change,
travel/vehicle boarding, quest abandonment, or a dead guide. Failure leaves the
quest active, without completion credit; abandon/reaccept restarts it. A failed
failure-script mutation is atomic, logged and not retried; guide ownership is
released regardless. Failed waypoint/completion actions can retry until the
remaining escort timeout, without committing the failed event's credit.

One online owner reserves the guide. Concurrent quest acceptance is refused.
If separately saved/resumed owners conflict, the lowest GUID retains ownership;
the other route fails. Offline owners release the live actor, retaining saved
route progress. On resumption, the actor is reconstructed at the recorded
position. Completion/failure returns the actor to its spawn, immediately; there
is no authored return-home path.

Save40 and LAN96 append a fixed 28-byte route record to owner progress: route ID,
next waypoint, pending wait, remaining online timeout and XYZ position. Saves
1–39 migrate to an empty escort record. Offline time does not advance escorts.
Moving guides are retained by the bounded NPC streaming roster. Existing NPC
snapshots carry their position to guests; guests never submit guide movement.

The route is a kinematic polyline. This package adds no navmesh walking,
obstacle avoidance, ambush spawning, NPC-vs-NPC defense AI, dialogue playback or
retail escort script import. Guides skip ordinary hostile pursuit while under
escort control. They are not a finished combat-escort implementation. Authored
points must be validated against the user's actual geometry on PS4.

The synthetic fixture and `run_local_escort_tests.sh` cover the implemented
behavior; they are not installed quest content. Overall milestone 5 remains open.
