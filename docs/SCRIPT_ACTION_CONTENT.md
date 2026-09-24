# Script actions

`scriptActions` is an immutable, ID-sorted content deck. A trigger or timer may
append at most eight distinct `actionIds`; authored order is execution order.
One authority batch is limited to 64 actions and publishes nothing until every
player, actor and capacity check succeeds.

Supported actions are `dialogue`, `spawn`, `despawn`, `move` and `combat`.
Actors use a content-local `actorId` in the low 31-bit range. Dialogue may use
`4294967295` (`kLocalScriptPlayerActor`) to make each scoped player the speaker.
Dialogue text is 1..255 UTF-8 bytes and is retained in a bounded 32-row history.
The latest visible row is exposed as a local status notice with its speaker name.
LAN100 transfers viewer-filtered dialogue in a bounded multipart deck; incomplete,
mixed-context or mixed-generation pages never replace the accepted view.

A spawn names an existing immutable NPC definition, an explicit map/instance
and position, and `lifetimeMs` from 1 through 600000. At most 32 script actors
exist at once. They are transient, use the ordinary NPC combat/spell deck, are
never character-save data, and expire on the authority clock. Expiry clears
player, pet, cast, missile, threat and periodic-effect references before actor
removal. The slot can then be reused.

A move stays on the actor's existing map and instance, remains in the complete
player scope, is limited to 100 yards, and must pass collision line-of-sight.
Longer routes are several authored moves. Area triggers never supply implicit
coordinates; spawn and move positions always come from their action rows.

`LocalGameplay::scriptActionCheckpoint()` and
`restoreScriptActionCheckpoint()` bracket the wider realm save transaction.
They cover the NPC roster, dialogue history, deferred kill facts and allocation
counters, so a save failure after a gameplay command can restore every newly
published or queued action.
World-event code uses `executeScriptActions()` for one all-or-nothing batch.
Action execution does not alter phase bits or game-object state.

NPC death cannot publish half of a reward/script transaction while combat code
still holds NPC references. A bounded 256-row deferred kill queue records the
player, NPC entry, accumulated XP and count. Adjacent identical facts are run-length
combined without reordering separated kills. Capacity and counter overflow are
checked before the fatal hit mutates health. Backpressure therefore leaves the
NPC alive at one health so the kill can be retried; it does not silently lose XP,
quest credit or a script trigger. Pending facts and counters are included in the
script-action checkpoint and in Save44, so a blocked script commit survives
disconnect and realm restart.

Content validation rejects duplicate IDs, unsupported action kinds, missing
references, actor use before spawn, scope mismatches, invalid player targets,
excessive distances, oversized action lists and impossible transient lifetimes.
As elsewhere in `world.json`, unrecognised object members are ignored by the
JSON reader. A failed batch publishes no dialogue, actor, movement or combat
result.

Run the focused regression suite with:

```sh
tools/tests/run_local_script_actions_tests.sh
```

The production `world.json` currently installs no `scriptActions`; the focused
fixture demonstrates the runtime contract and is not original game content.
