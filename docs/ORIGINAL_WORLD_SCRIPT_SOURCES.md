# Original world script source checkpoint

The project now includes the original SQL inputs that were absent from the
earlier milestone-5 source archives: SmartAI actions, GameObject placements,
templates and loot, waypoint tables, calendar schedules, event memberships,
object pools, creature addon state and dialogue rows. All inputs come from
AzerothCore revision `4e80596cdaa21fa31830522f6f2d7ed8750bfffd`.

This checkpoint imports **source data**, with explicit execution blockers.
It does **not** mark raw SQL rows as playable local content. No extracted
SmartAI, GameObject, escort or calendar row is registered in `world.json`.
The new authored ScriptAction ABI and simulation-time scheduler are separate
runtime capabilities; their presence does not imply automatic SmartAI parity.

## Files and scope

| File | Purpose |
| --- | --- |
| `tools/local_realm/world_script_source_sql.tar.gz` | 27 complete newly acquired SQL base tables |
| `tools/local_realm/world_script_source_manifest.json` | Pinned URLs, sizes and SHA256 hashes for all 36 required tables |
| `assets/local_realm/original_script_sources.json.gz` | Lossless selected SQL row dictionaries and provenance |
| `docs/ORIGINAL_SCRIPT_IMPORT_REPORT.json` | Every selected action, object, path and event with its blocker status |
| `tools/local_realm/fetch_world_script_sources.py` | Reproduce the new SQL archive, verifying the manifest |
| `tools/local_realm/import_world_script_sources.py` | Reproduce the scoped companion and report without executing SQL |

Nine inputs are reused from `world_source_sql.tar.gz` and
`vendor_source_sql.tar.gz`; their exact table bytes are now pinned in the new
manifest too. SQL licenses remain governed by `LICENSE-AC-GPL-2.0.txt`.

The scope combines the current **950 adapted catalog quests** and their
creature actors with the existing Northshire/Goldshire region from
`world.json` provenance. It preserves the full source group for a selected
SmartAI actor, including rows concerning quests outside that adapted catalog.
It follows called timed action lists, source-local links, object loot
references, pool parents and event prerequisites. It retains the complete
condition table so shared condition references are not silently lost.

The generated checkpoint contains **43,880 rows across 36 tables**, including:

| Source content | Selected count |
| --- | ---: |
| SmartAI rows, including called timed lists | 2,568 |
| Direct creature SmartAI rows for Northshire/Goldshire actors | 65 |
| Original GameObject placements in the region | 944 |
| Original GameObject templates | 246 |
| GameObject loot rows | 156 |
| GameObject event memberships | 656 |
| Relevant calendar events | 15 |
| Waypoint movement points | 6,168 |
| SmartAI escort points | 204 |
| Creature dialogue rows | 1,061 |

These counts are extraction coverage, not completed gameplay. The report has
`runtimeInstalled: false`, all runtime import counts zero and
`gameplayVerified: 0`.

## Reproduction

From the project root, the normal offline reproduction uses the three source
archives already included:

```sh
python3 tools/local_realm/import_world_script_sources.py
python3 tools/tests/test_world_script_sources.py
```

To reacquire the newly added full SQL tables from their exact primary URLs:

```sh
python3 tools/local_realm/fetch_world_script_sources.py
```

To rebuild that archive from a previously downloaded directory:

```sh
python3 tools/local_realm/fetch_world_script_sources.py --source-dir /path/to/sql
```

The importer also accepts `--sql-dir /path/to/sql`, containing all 36 pinned
base dumps. A missing table, altered byte sequence, wrong source revision,
duplicate archive member or malformed base dump stops the run. Inputs are
parsed as data; no MySQL server, SQL execution or shell interpolation is used.
The tools preserve source numeric values, strings, NULLs, signed event
memberships and all selected row fields. Output ordering and gzip/tar metadata
are deterministic for the same inputs and Python/zlib toolchain.

## What remains blocked

- Original SmartAI requires its event lifecycle, RNG, source-local phase,
  target selection, invoker, timed-list, spell and action semantics. A SmartAI
  phase is not a local player's world phase mask. Whole source groups remain
  blocked; one apparently simple row is not activated while its linked context
  is missing.
- GameObjects may depend on event calendars, pools, Lock.dbc, conditional or
  probabilistic loot, quest-count limits, addon flags/gold/artkits, rotations,
  script names and type-specific data. Such rows are preserved, not converted
  to an always-active deterministic chest or generic door.
- Original calendar timestamps and holiday-derived dates are not converted
  into arbitrary elapsed-simulation delays. The runtime scheduler currently
  executes authored simulation-time events only. Holiday dates require matching
  Holidays.dbc behavior; absolute dates also require a deliberate source-timezone
  policy. Original world-event progress machines are a further requirement.
- A movement path is not automatically a quest escort. Path ownership,
  movement modes, point actions, combat and quest lifecycle still need a
  reviewed conversion.
- Original C++ `ScriptName` implementations and arbitrary spell/target
  dependency chains are outside the extracted SQL closure. The report names
  required C++ scripts and exact Lock/Holidays/SmartAI-cast spell record IDs,
  and lists unresolved paths or references if present.

For example, Marshal McBride's original quest-54 acceptance script and William
Pestle's quest-112 reward script are now present as source. Those quests are
absent from the current adapted 950-quest catalog, so their scripts cannot be
claimed as installed quest behavior. The companion preserves those source
quest definitions and reports the admission/runtime gap.

## Validation

The focused suite passed **20 tests**, covering exact source hash checks,
missing-input and duplicate-member rejection, real upstream identity rules,
nested/cyclic timed-list and loot-reference closure, explicit missing links,
calendar/quest/lock/pool blockers, geographic scope, deterministic output and
transactional companion/report publication.
The full pinned-source import completed successfully. It did not execute game
content or render an original scene, and is not a console acceptance test.

## Pinned primary sources

- [World SQL directory](https://github.com/azerothcore/azerothcore-wotlk/tree/4e80596cdaa21fa31830522f6f2d7ed8750bfffd/data/sql/base/db_world)
- [SmartScriptMgr.h](https://github.com/azerothcore/azerothcore-wotlk/blob/4e80596cdaa21fa31830522f6f2d7ed8750bfffd/src/server/game/AI/SmartScripts/SmartScriptMgr.h)
- [GameObjectData.h](https://github.com/azerothcore/azerothcore-wotlk/blob/4e80596cdaa21fa31830522f6f2d7ed8750bfffd/src/server/game/Entities/GameObject/GameObjectData.h)

The manifest supplies the exact raw SQL URL for each table. The earlier
quest-chain documentation's statement that these source tables were missing
describes the pre-import checkpoint; source availability has improved, while
the execution boundaries above remain explicit.
