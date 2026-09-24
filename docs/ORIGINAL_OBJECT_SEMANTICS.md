# Original GameObject semantics

The reviewed compiler (`tools/local_realm/compile_reviewed_original_content.py`)
installs an original placement only when every behaviour it has in AzerothCore
is represented locally. Inputs are pinned: `original_script_sources.json.gz`,
`lock_rows.json` (from the user's WotLK `Lock.dbc`, SHA-256 recorded) and
`pool_totals.json` (from the pinned full `pool_gameobject`/`pool_pool` SQL).

| Original | Local kind | Semantics |
|---|---|---|
| Type 5 GENERIC, addon faction / NOT_SELECTABLE / NODESPAWN only | `decorative` | Presentation; never usable. |
| Type 8 SPELL_FOCUS without scripts | `decorative` | Presentation. The local crafting rules do not yet require a focus. |
| Type 7 CHAIR | `chair` | `data0` slots on the line orthogonal to the facing, `size` apart; nearest slot not occupied within 0.1 yd; stand state 4 + `data1`. Applied by the sitting client; movement stands up. |
| Type 3 CHEST, Lock type OPEN/TREASURE/OPEN_KNEELING/… | `chest` | Loot template roll, consumable (depleted until respawn) or persistent. |
| Type 3 CHEST, Lock HERBALISM/MINING | `resource` | Skill 182/186 at the lock's value; mining needs a TotemCategory-165 pick. Gathering skill-up chance 100/75/25/0 % at +0/+25/+50/+100. |

**Loot** follows `LootTemplate::Process`: ungrouped rows roll their chance
independently; each group yields at most one row — explicitly chanced rows
against one 0–100 roll, otherwise a uniform equal-chance member; counts are
uniform in `[min,max]`; `QuestRequired` rows reach only a player whose active
quest still needs the item. A full bag rejects the use atomically.

**GO_FLAG_INTERACT_COND** chests are usable only while a quest still needs one
of their quest-loot items (`GameObject::ActivateToQuest`).

**Pools**: each spawn belongs to at most one pool; `maxActive` members are
spawned (status 0/1/2), the rest are dormant (status 3). When a depleted member
respawns, the pool picks uniformly among it and all dormant members. A pool is
installed only as a whole. When the regional capture holds only part of the
original pool, `maxActive = max(1, round(max_limit × captured / total))` keeps
AzerothCore's expected density; the report marks such pools `regionClipped`.

**Events**: one positive membership in a supported holiday, or a fixed
`game_event` interval (`start < now < end` and `(now-start) mod occurence <
length`, local time). Arena Tournament has `start == end` and is never active.

Blocked rows keep explicit reasons in `docs/REVIEWED_ORIGINAL_RUNTIME_REPORT.json`.
