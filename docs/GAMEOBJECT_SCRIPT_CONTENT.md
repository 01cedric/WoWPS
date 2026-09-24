# Authored GameObject interactions

For shared doors, chest/resource rewards, persistence and LAN replication,
see [SHARED_OBJECTS.md](SHARED_OBJECTS.md). The historical personal-object scope
below remains supported; shared kinds now use a distinct state ledger. All uses
also check installed collision line of sight.

This package adds placed, static open-world objects to the local realm's authored
`world.json`. It connects their use to the existing per-character script, quest,
phase and timer system. It does not import the original server GameObject scripts.

## Content contract

`gameObjects` accepts at most 512 rows, sorted by unique positive `id` on load.
Each row needs positive `entry` and `displayId` and a nonempty `name` (96 bytes
maximum). The display must resolve in the user's installed client assets.
Positions use server coordinates, as NPC spawns and mailboxes do.

| Field | Default / allowed values |
|---|---|
| `mapId` | 0; 0–10000 |
| `x`, `y` | 0; -100000 to 100000 |
| `z` | 0; -20000 to 20000 |
| `orientation` | 0 radians; -100 to 100 |
| `scale` | 1; 0.1–10 |
| `useRadius` | 5 yards; 0.1–5, three-dimensional and inclusive |
| `requiredPhaseMask`, `excludedPhaseMask` | 0; existing phase visibility semantics, masks cannot overlap |
| `requiredScriptId`, `requiredValue` | 0; optional exact saved-state gate, state ID must have an authored writer |
| `requiredQuestId` | 0; optional quest gate requiring an active, incomplete quest |

Each object needs at least one `scriptTriggers` row with `trigger: "objectUse"`
and `sourceId` equal to the object's **spawn ID**, not its entry/display ID.
The event supports the same state mutation, phase change and timer operations
as other script events. Existing `type: "script"` objectives observe those
states, with authored English text in the original quest UI. No new objective
encoding or save layout is needed.

Use `requiredQuestId` to prevent credit before accepting or after completing a
quest. For a one-shot object, gate on a zero state and set that state to one in
its use event. For a cooldown, set a gate state and schedule an authored timer
that clears it. Without a gate, each new accepted command can execute the event
again. Design that behavior explicitly; the engine does not infer retail reset
or respawn rules. The synthetic fixture demonstrates quest acceptance resets,
a one-shot gate, quest completion, a timer and abandon cleanup.

## Authority and persistence

- Commands carry both spawn ID and a deterministic object GUID. The host checks
  their agreement and uses its own immutable content and player position.
- Use requires a living, non-ghost player in the open world, within radius, in
  the correct phase and with the authored gates satisfied. Vehicle occupancy,
  flight, transport travel, casting, an attack target or active NPC combat refuse use.
- At least one event row must match the current state. All object rows and their
  resulting script-quest completion actions are staged together. An overflow or
  invalid transition refuses the use without partial state, timer or quest credit.
- Realm commands save the resulting progress before success. A failed atomic
  disk write restores the previous player state. Timers continue to use simulated
  online time, following the script timer rules.
- LAN95 adds `UseGameObject` to the reliable command stream. Duplicate command
  IDs cannot execute twice; state/timers/quests use existing owner-progress
  replication. Both peers need compatible content fingerprints. Save39 remains
  unchanged and retains its legacy readers.
- The effect belongs to the interacting character. This is not shared door,
  chest inventory, resource-node depletion or encounter state.

## Presentation and controls

Objects within 120 horizontal and vertical yards use the existing GameObject
model spawner. Map, instance, distance and phase changes retire their render
instances. State/quest gates control use, not visibility; a used object remains
visible unless its phase becomes hidden. Appearance comes from installed MPQs.

The contextual Square action and keyboard 4 use the nearest eligible object;
Square preserves the selected hostile target's combat action. Objects precede
mailboxes and friendly NPC interactions. Vehicle exit and corpse recovery keep
priority on contextual Square. The hint names the nearby object in English.

## Limits and acceptance

No dungeon objects, shared world-state replication, door animations, loot tables,
object collision/line-of-sight enforcement, channelled use or retail content
conversion is added. Distance-only interaction can reach through a wall. The
256-row scan is bounded but has not been benchmarked on PS4. The example entry
and display IDs are synthetic test values, not a verified visual demo.

Run `bash tools/tests/run_local_gameobject_tests.sh`. This covers production
gameplay, actual loopback UDP traffic, persistence, rollback and malformed data.
A host syntax check does not verify model appearance, collision, pad behavior or
PS4 execution. Those still need the SDK and console acceptance.
