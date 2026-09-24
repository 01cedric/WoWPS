# Original quest admission rules

The schema-2 companion `assets/local_realm/quest_chains.json` contains **950 gates:
947 compiled and 3 explicitly blocked**. Four gates use player conditions and
14 use ordered predecessor rules. Of all gates, 651 contain predicates.
These are admission-rule counts, not playable-quest counts; `gameplayVerified`
remains zero. The binary quest catalog is unchanged.

Regenerate and verify from the repository root:

```sh
python3 -B tools/local_realm/import_quest_chains.py --output assets/local_realm/quest_chains.json --report docs/QUEST_CHAIN_IMPORT_REPORT.json
python3 -B tools/tests/test_quest_chain_import.py
bash tools/tests/run_local_quest_chain_tests.sh
```

The importer checks the catalog's commit and pack checksum, plus fixed SHA-256
hashes of all three SQL inputs from the existing source archives. Companion and
report are deterministic. Source IDs absent from the local catalog remain in
the requirements; historical character state may satisfy them. No missing
requirement is silently dropped. The report's 892 potentially reachable gates
are a positive-dependency over-approximation, not objective or gameplay proof.

## Runtime format

Each quest row contains `id`, `maxLevel`, `alternatives`, `unsupportedReason`,
and optionally `orderedPrevious`. A defined gate replaces the legacy scalar
prerequisite. Other admission checks, including lower level, race/class,
profession and reputation restrictions, continue to apply.

`alternatives` are OR branches of AND predicates. Bounds: 16 alternatives,
16 quest predicates and 16 player predicates per branch. Empty `[[]]` allows
the alternative stage; empty `[]` requires an explicit blocked reason.

| Predicate | Meaning |
|---|---|
| `questId`, `statusMask` | Bits 1=None, 2=Active incomplete, 4=Complete unrewarded, 8=Rewarded |
| `itemId`, `count`, `includeBank`, `negated` | Inventory quantity, optionally plus bank, reaches count |
| `factionId`, `rankMask`, `negated` | Current rank bit matches; bits 0–7 represent Hated through Exalted |
| `spellId`, `negated` | Spell is in the character's known-spell list |

Negation complements only a validated predicate. Equipped items are already
represented in this runtime's inventory and are not counted again. Reputation
uses the replicated standing, including the established rank boundaries.
Item/spell/faction references need not have a locally playable definition:
checking owned or historical character state does not create such definitions.

`orderedPrevious` is a list of at most 16 rules. Each rule contains a `when`
quest predicate and a `require` array of at most 16 quest predicates. The first
matching `when` decides the predecessor stage: all its requirements must hold,
otherwise the gate immediately rejects. Later rules cannot rescue that failure.
No matching rule rejects. The result is ANDed with the alternatives stage.
Schema 1 remains readable, but cannot contain these rules or player predicates.
Unknown fields, invalid types, contradictory player predicates, duplicate
references/alternatives, and oversized expressions are rejected.

## Pinned source behavior

All semantic references use AzerothCore commit
`4e80596cdaa21fa31830522f6f2d7ed8750bfffd`.

| Source | Implemented behavior |
|---|---|
| Positive previous quest / incoming `NextQuestID` | Rewarded predecessor alternatives |
| Negative previous quest | Non-None predecessor, including rewarded for nonrepeatable quests |
| Negative predecessor group | Positive predecessor requires every other member rewarded; negative predecessor requires every other member **None**, preserving the pinned implementation's asymmetric check |
| Positive exclusive group | Other group members must be None |
| Breadcrumb / reward-next chain | Existing forward and reverse exclusion rules retained |
| Conditions 8/9/14/28/47 | Rewarded, incomplete-only, None, complete-only, translated status mask |
| Conditions 2/5/25 | Item count (value3 selects bank), reputation rank mask, known spell |
| ElseGroup / NegativeCondition | AND within a group, OR across groups; exact supported-predicate complement |

`PlayerQuest.cpp:1039–1122` returns immediately after the first matching
predecessor, including group failures. `ObjectMgr.cpp:5782–5804` builds that
list while traversing `QuestMap`; `ObjectMgr.h` defines it as an unordered map.
SQL alone therefore cannot establish a portable order for arbitrary mixed
predecessors. Such order-dependent mixtures remain blocked.

All **14** formerly blocked production groups have positive predecessors from
the same negative group. Every traversal order gives the same decision. They
are encoded in deterministic ID order without claiming that this is upstream
hash-table order. Python tests exhaust all four local statuses and all six
orders of a three-member example. C++ tests separately verify that the runtime
does preserve early rejection for explicitly ordered mixed rules.

Three gates remain blocked because their source prerequisites require recurring
or seasonal history: **3501 → 3502**, **7845 → 7846**, **12435 → 12372**.
Failed quest states, unknown/scripted conditions, reference templates and bounds
violations also fail closed. No recurring-history behavior is invented.

Primary source links:

- [PlayerQuest.cpp](https://github.com/azerothcore/azerothcore-wotlk/blob/4e80596cdaa21fa31830522f6f2d7ed8750bfffd/src/server/game/Entities/Player/PlayerQuest.cpp)
- [ObjectMgr.cpp](https://github.com/azerothcore/azerothcore-wotlk/blob/4e80596cdaa21fa31830522f6f2d7ed8750bfffd/src/server/game/Globals/ObjectMgr.cpp)
- [ObjectMgr.h](https://github.com/azerothcore/azerothcore-wotlk/blob/4e80596cdaa21fa31830522f6f2d7ed8750bfffd/src/server/game/Globals/ObjectMgr.h)
- [ConditionMgr.cpp](https://github.com/azerothcore/azerothcore-wotlk/blob/4e80596cdaa21fa31830522f6f2d7ed8750bfffd/src/server/game/Conditions/ConditionMgr.cpp)
- [QuestDef.h](https://github.com/azerothcore/azerothcore-wotlk/blob/4e80596cdaa21fa31830522f6f2d7ed8750bfffd/src/server/game/Quests/QuestDef.h)
- [SharedDefines.h](https://github.com/azerothcore/azerothcore-wotlk/blob/4e80596cdaa21fa31830522f6f2d7ed8750bfffd/src/server/shared/SharedDefines.h)

Companion bytes already participate in the LAN content fingerprint; this change
needs no additional gameplay hashing patch. Original SmartAI/world-object/event
source capture is documented separately in `ORIGINAL_WORLD_SCRIPT_SOURCES.md`.
Source capture does not establish executable content or console acceptance.
