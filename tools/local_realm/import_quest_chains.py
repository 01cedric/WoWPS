#!/usr/bin/env python3
"""Compile pinned quest admission rules into bounded, fail-closed local gates.

Uses the exact SQL archives already shipped with this project. It never invents
quests, objectives, rewards, script actions or missing prerequisite quests.
The existing catalog is read-only; only the requested companion/report are written.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import tarfile
import tempfile

from import_azerothcore import PINNED_COMMIT, REPOSITORY, sql_rows

NONE, ACTIVE, COMPLETE, REWARDED, ALL = 1, 2, 4, 8, 15
MAX_ALTERNATIVES, MAX_PREDICATES, MAX_PLAYER_CONDITIONS, MAX_PREVIOUS_RULES = 16, 16, 16, 16
PINNED_SQL_HASHES = {
    "quest_template.sql": "5dc7e6c92ea6875a5fc68cf5d3e33150e86cda5819a11af0c59217b24c63a37f",
    "quest_template_addon.sql": "ca1081203af9dc5ab9b948d94652a66674d116ce387f629595ec38f89c4af5d2",
    "conditions.sql": "1a40806e454211118e0b459e09eb70e0e09807987ead6b321bbd371b93ffeb7d",
}
SOURCE_BASE = f"{REPOSITORY}/blob/{PINNED_COMMIT}/"
SEMANTIC_SOURCES = [SOURCE_BASE + p for p in (
    "src/server/game/Entities/Player/PlayerQuest.cpp",
    "src/server/game/Globals/ObjectMgr.cpp",
    "src/server/game/Globals/ObjectMgr.h",
    "src/server/game/Conditions/ConditionMgr.cpp",
    "src/server/game/Quests/QuestDef.h",
    "src/server/shared/SharedDefines.h")]


class Unsupported(ValueError):
    """A valid upstream rule cannot be represented faithfully by this runtime."""


def read_pack(path):
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError("Truncated quest pack")
    magic, count, stride = struct.unpack_from("<8sII", data)
    if magic != b"WPCAT01\0" or stride != 16 or count > 100000:
        raise ValueError("Invalid quest pack header")
    end = 16 + 16 * count
    if end > len(data):
        raise ValueError("Truncated quest pack index")
    out, previous = {}, 0
    for index in range(count):
        key, offset, size = struct.unpack_from("<IQI", data, 16 + index * 16)
        if key <= previous or offset != end or not 0 < size <= 16384 or offset + size > len(data):
            raise ValueError("Invalid quest pack record")
        row = json.loads(data[offset:offset + size])
        if not isinstance(row, dict) or type(row.get("id")) is not int or row["id"] != key:
            raise ValueError("Quest pack record ID mismatch")
        out[key], previous, end = row, key, offset + size
    if end != len(data):
        raise ValueError("Trailing quest pack bytes")
    return out


def archive_rows(archive, tables):
    result, hashes = {}, {}
    with tarfile.open(archive) as tar, tempfile.TemporaryDirectory(prefix="wowps-chain-sql-") as temp:
        for table in tables:
            name = table + ".sql"
            matches = [m for m in tar.getmembers() if m.name == name]
            if len(matches) != 1 or not matches[0].isfile() or matches[0].size > 16000000:
                raise ValueError(f"Missing or invalid source table: {name}")
            data = tar.extractfile(matches[0]).read()
            hashes[name] = hashlib.sha256(data).hexdigest()
            path = Path(temp) / name
            path.write_bytes(data)
            result[table] = list(sql_rows(path, table))
            if not result[table]:
                raise ValueError(f"Empty source table: {name}")
    return result, hashes


def indexed(rows, key="id"):
    result = {}
    for row in rows:
        value = row.get(key)
        if type(value) is not int or not 0 < value <= 0xffffffff or value in result:
            raise ValueError(f"Invalid or duplicate source ID: {value}")
        result[value] = row
    return result


def canonical(alternatives):
    """Intersect repeated predicates, remove false branches and redundant ORs."""
    unique = set()
    for predicates in alternatives:
        merged, player = {}, set()
        for predicate in predicates:
            if len(predicate) == 5:
                kind, identifier, value, bank, negative = predicate
                if kind not in ("item", "reputation", "spell") or type(identifier) is not int or not 0 < identifier <= 0xffffffff \
                        or type(value) is not int or not 0 <= value <= 0xffffffff or type(bank) is not bool or type(negative) is not bool \
                        or (kind == "item" and not value) or (kind == "reputation" and (not 0 < value <= 255 or bank)) \
                        or (kind == "spell" and (value or bank)):
                    raise ValueError("Invalid normalized player predicate")
                player.add(predicate)
                continue
            quest, mask = predicate
            if type(quest) is not int or not 0 < quest <= 0xffffffff or not 0 <= mask <= ALL:
                raise ValueError("Invalid normalized quest predicate")
            merged[quest] = merged.get(quest, ALL) & mask
        if any(mask == 0 for mask in merged.values()) or any(p[:-1] + (not p[-1],) in player for p in player):
            continue
        branch = tuple(sorted((quest, mask) for quest, mask in merged.items() if mask != ALL)) + tuple(sorted(player))
        unique.add(branch)
    # A weaker branch absorbs a stronger one; this also makes [()] unconditional.
    ordered = sorted(unique, key=lambda row: (len(row), repr(row)))
    kept = []
    for branch in ordered:
        values = dict(p for p in branch if len(p) == 2)
        if any(all((p[0] in values and values[p[0]] & p[1] == values[p[0]]) if len(p) == 2 else p in branch
                   for p in prior) for prior in kept):
            continue
        kept.append(branch)
    if len(kept) > MAX_ALTERNATIVES or any(sum(len(p) == 2 for p in row) > MAX_PREDICATES or
                                         sum(len(p) == 5 for p in row) > MAX_PLAYER_CONDITIONS for row in kept):
        raise Unsupported("quest rule exceeds the 16 by 16 runtime bound")
    return kept


def conjunction(left, right):
    return canonical(a + b for a in left for b in right)


def serialize_predicate(predicate):
    if len(predicate) == 2:
        return {"questId": predicate[0], "statusMask": predicate[1]}
    kind, identifier, value, bank, negative = predicate
    if kind == "item":
        return {"itemId": identifier, "count": value, "includeBank": bank, "negated": negative}
    if kind == "reputation":
        return {"factionId": identifier, "rankMask": value, "negated": negative}
    return {"spellId": identifier, "negated": negative}


def compile_rules(catalog, templates, addons, conditions):
    incoming, breadcrumbs, chains, exclusive = (collections.defaultdict(set) for _ in range(4))
    for quest, addon in addons.items():
        if addon.get("nextquestid"):
            incoming[addon["nextquestid"]].add(quest)
        if addon.get("breadcrumbforquestid"):
            breadcrumbs[addon["breadcrumbforquestid"]].add(quest)
        if addon.get("exclusivegroup"):
            exclusive[addon["exclusivegroup"]].add(quest)
    for quest, template in templates.items():
        if template.get("rewardnextquest"):
            chains[template["rewardnextquest"]].add(quest)
    availability = collections.defaultdict(list)
    for row in conditions:
        if row["sourcetypeorreferenceid"] == 19:
            availability[row["sourceentry"]].append(row)

    def ref(quest):
        if quest not in templates or quest not in addons:
            raise Unsupported(f"source prerequisite {quest} is missing")
        return addons[quest]

    def ordinary(quest):
        a = ref(quest)
        # This gate runtime has no daily/weekly/monthly/seasonal reward history.
        # Exact IsSeasonal() sort list from pinned QuestDef.h/SharedDefines.h.
        if a.get("specialflags", 0) & (1 | 16) or templates[quest].get("flags", 0) & (0x1000 | 0x8000) \
                or templates[quest].get("questsortid") in (-22, -284, -366, -369, -370, -374, -376):
            raise Unsupported(f"repeatable or seasonal prerequisite {quest}")
        return a

    def condition(row):
        if any(row.get(key, 0) for key in ("sourcegroup", "sourceid", "conditiontarget")) \
                or row.get("scriptname", ""):
            raise Unsupported("non-player or scripted quest availability condition")
        kind, quest, extra = (row["conditiontypeorreference"], row["conditionvalue1"], row["conditionvalue2"])
        value3 = row.get("conditionvalue3", 0)
        if any(type(value) is not int or not 0 <= value <= 0xffffffff for value in (quest, extra, value3)) or not quest:
            raise Unsupported("invalid quest condition parameter")
        if type(row["negativecondition"]) is not int or row["negativecondition"] not in (0, 1):
            raise Unsupported("invalid negative quest condition")
        negative = bool(row["negativecondition"])
        if kind == 2:
            if not extra:
                raise Unsupported("zero item quest condition count")
            return "item", quest, extra, bool(value3), negative
        if value3:
            raise Unsupported("unexpected quest condition parameter")
        if kind == 5:
            if not 0 < extra <= 255:
                raise Unsupported("invalid reputation rank mask")
            return "reputation", quest, extra, False, negative
        if kind == 25:
            if extra:
                raise Unsupported("unexpected spell quest condition parameter")
            return "spell", quest, 0, False, negative
        ordinary(quest) if kind in (8, 9, 14, 28, 47) else None
        if kind in (8, 9, 14, 28) and extra:
            raise Unsupported("unexpected quest condition parameter")
        # CONDITION_QUESTTAKEN is INCOMPLETE only in the pinned ConditionMgr.
        if kind in (8, 9, 14, 28):
            mask = {8: REWARDED, 9: ACTIVE, 14: NONE, 28: COMPLETE}[kind]
        elif kind == 47:
            if not extra or extra & ~(1 | 2 | 8 | 64):
                raise Unsupported("unsupported upstream quest status mask")
            mask = ((NONE if extra & 1 else 0) | (COMPLETE if extra & 2 else 0) |
                    (ACTIVE if extra & 8 else 0) | (REWARDED if extra & 64 else 0))
        else:
            raise Unsupported(f"unsupported quest availability condition {kind}")
        if negative:
            mask ^= ALL
        return quest, mask

    output, details = [], []
    for quest in sorted(catalog):
        meta = {"questId": quest, "title": catalog[quest].get("title", ""), "features": []}
        row = {"id": quest, "maxLevel": 0, "alternatives": [], "unsupportedReason": ""}
        try:
            a = ordinary(quest)
            maximum = a.get("maxlevel", 0)
            if not 0 <= maximum <= 80:
                raise Unsupported("maximum quest level exceeds the local level cap")
            row["maxLevel"] = maximum
            if maximum:
                meta["features"].append("maxLevel")
            previous = set(incoming[quest])
            if a.get("prevquestid"):
                previous.add(a["prevquestid"])
                meta["features"].append("PrevQuestID")
            if incoming[quest]:
                meta["features"].append("incomingNextQuestID")
            alternatives = [()]
            if previous:
                alternatives = []
                groups = {ordinary(abs(source)).get("exclusivegroup", 0) for source in previous}
                ordered = any(group < 0 for group in groups)
                if ordered and len(previous) > 1 and not (len(groups) == 1 and all(source > 0 for source in previous)):
                    # QuestMap is unordered_map in the pinned core. SQL rows do
                    # not determine its traversal order. Only compile multiple
                    # early-return rules when all orderings provably agree.
                    raise Unsupported("order-dependent predecessor traversal is not available from SQL")
                if ordered and len(previous) > MAX_PREVIOUS_RULES:
                    raise Unsupported("ordered previous rules exceed the runtime bound")
                ordered_rules = []
                for source in sorted(previous):
                    prior = ordinary(abs(source))
                    group = prior.get("exclusivegroup", 0)
                    members = exclusive[group] if group < 0 else {abs(source)}
                    for member in members:
                        ordinary(member)
                    if ordered:
                        requirements = [(member, REWARDED if source > 0 else NONE)
                                        for member in sorted(members - {abs(source)})]
                        if len(requirements) > MAX_PREDICATES:
                            raise Unsupported("ordered previous group exceeds the runtime bound")
                        ordered_rules.append({"when": serialize_predicate((abs(source), REWARDED if source > 0 else ALL ^ NONE)),
                                              "require": [serialize_predicate(p) for p in requirements]})
                    else:
                        alternatives.append(((abs(source), REWARDED if source > 0 else ALL ^ NONE),))
                if ordered:
                    row["orderedPrevious"] = ordered_rules
                    meta["features"].append("orderedPrevious")
                    alternatives = [()]
                else:
                    alternatives = canonical(alternatives)
            checks = []
            group = a.get("exclusivegroup", 0)
            if group:
                meta["features"].append("ExclusiveGroup")
            if group > 0:
                for member in sorted(exclusive[group] - {quest}):
                    ordinary(member)
                    checks.append((member, NONE))
            target = a.get("breadcrumbforquestid", 0)
            if target:
                ordinary(target)
                checks.append((target, NONE))
                meta["features"].append("BreadcrumbForQuestId")
            for source in sorted(breadcrumbs[quest]):
                ordinary(source)
                checks.append((source, NONE | REWARDED))
            if breadcrumbs[quest]:
                meta["features"].append("incomingBreadcrumb")
            target = templates[quest].get("rewardnextquest", 0)
            if target:
                ordinary(target)
                checks.append((target, NONE))
                meta["features"].append("RewardNextQuest")
            for source in sorted(chains[quest]):
                ordinary(source)
                checks.append((source, NONE | REWARDED))
            if chains[quest]:
                meta["features"].append("incomingRewardNextQuest")
            alternatives = conjunction(alternatives, [tuple(checks)])
            if availability[quest]:
                groups = collections.defaultdict(list)
                for source in availability[quest]:
                    groups[source["elsegroup"]].append(condition(source))
                alternatives = conjunction(alternatives, canonical(tuple(group) for group in groups.values()))
                meta["features"].append("questAvailabilityConditions")
            if not alternatives:
                raise Unsupported("contradictory upstream quest admission rules")
            row["alternatives"] = [[serialize_predicate(predicate) for predicate in branch]
                                   for branch in alternatives]
            meta["status"] = "compiled"
            dependencies = [p for branch in row["alternatives"] for p in branch]
            dependencies += [p for rule in row.get("orderedPrevious", []) for p in [rule["when"], *rule["require"]]]
            missing = sorted({p["questId"] for p in dependencies if "questId" in p and
                              p["questId"] not in catalog and not p["statusMask"] & NONE})
            if missing:
                # Preserve the actual rule for historical saves that rewarded an
                # absent quest. Runtime denies a fresh character naturally.
                meta["unavailablePositiveDependencies"] = missing
        except Unsupported as error:
            row["alternatives"] = []
            row.pop("orderedPrevious", None)
            row["unsupportedReason"] = str(error)[:192]
            meta["status"], meta["reason"] = "blocked", row["unsupportedReason"]
        meta["legacyPrerequisite"] = catalog[quest].get("prerequisite", 0)
        output.append(row)
        details.append(meta)

    # Prove which quests have at least one possible positive dependency path
    # from a fresh character. This is an over-approximation, not playability:
    # negative/state relationships and all objectives still need runtime checks.
    reachable = set()
    while True:
        added = {row["id"] for row in output if row["id"] not in reachable and row["alternatives"] and
                 any(all("questId" not in p or p["statusMask"] & NONE or p["questId"] in reachable for p in branch)
                     for branch in row["alternatives"]) and
                 (not row.get("orderedPrevious") or any(all(p["statusMask"] & NONE or p["questId"] in reachable
                    for p in [rule["when"], *rule["require"]]) for rule in row["orderedPrevious"]))}
        if not added:
            break
        reachable.update(added)
    for meta in details:
        meta["positiveDependencyReachableFromFreshSave"] = meta["questId"] in reachable
        meta["gameplayVerified"] = False
    return output, details


def convert(root, output, report):
    catalog_path = root / "assets/local_realm/catalog/quests.pack"
    manifest_path = catalog_path.parent / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    if manifest.get("sourceCommit") != PINNED_COMMIT:
        raise ValueError("Catalog source commit differs from the pinned source")
    digest = hashlib.sha256(catalog_path.read_bytes()).hexdigest()
    if manifest.get("files", {}).get("quests.pack", {}).get("sha256") != digest:
        raise ValueError("Catalog quest pack hash differs from its manifest")
    catalog = read_pack(catalog_path)
    source, hashes = archive_rows(root / "tools/local_realm/world_source_sql.tar.gz",
                                 ["quest_template", "quest_template_addon"])
    condition_source, condition_hashes = archive_rows(root / "tools/local_realm/vendor_source_sql.tar.gz", ["conditions"])
    hashes.update(condition_hashes)
    if hashes != PINNED_SQL_HASHES:
        raise ValueError("SQL source hashes differ from the pinned archives")
    hashes["input_quests.pack"] = digest
    rules, details = compile_rules(catalog, indexed(source["quest_template"]), indexed(source["quest_template_addon"]),
                                   condition_source["conditions"])
    companion = {"schemaVersion": 2, "sourceCommit": PINNED_COMMIT, "sourceHashes": hashes, "quests": rules}
    counts = {"inputCatalogQuests": len(catalog), "compiledGates": sum(not row["unsupportedReason"] for row in rules),
              "blockedGates": sum(bool(row["unsupportedReason"]) for row in rules),
              "gatesWithPredicates": sum(bool(any(row["alternatives"]) or row.get("orderedPrevious")) for row in rules),
              "orderedPreviousGates": sum(bool(row.get("orderedPrevious")) for row in rules),
              "gatesWithPlayerConditions": sum(any("questId" not in p for branch in row["alternatives"] for p in branch) for row in rules),
              "positiveDependencyReachableFromFreshSave": sum(row["positiveDependencyReachableFromFreshSave"] for row in details),
              "gameplayVerified": 0}
    provenance = {"schemaVersion": 1, "sourceCommit": PINNED_COMMIT, "sourceHashes": hashes,
                  "semanticSources": SEMANTIC_SOURCES, "counts": counts,
                  "blockedReasons": dict(sorted(collections.Counter(row["unsupportedReason"] for row in rules if row["unsupportedReason"]).items())),
                  "limitations": ["Admission rules only; no new quests, rewards, objectives, scripts or retail parity.",
                                  "Missing positive dependencies stay required, including for historical saves; they are never deleted from an OR branch.",
                                  "Fresh-save dependency reachability is an over-approximation, not a gameplay completion claim.",
                                  "Repeatable/seasonal rules, failed quest states, unsupported conditions and reference templates fail closed.",
                                  "SQL does not specify the upstream unordered-map predecessor order; order-dependent mixtures fail closed. Imported ordered rules are proven order-independent.",
                                  "A companion gate replaces only the legacy scalar prerequisite, preserving all other runtime admission gates."],
                  "quests": details}
    if output.resolve() == report.resolve():
        raise ValueError("Companion and report must have distinct paths")
    for path, value in ((output, companion), (report, provenance)):
        path.parent.mkdir(parents=True, exist_ok=True)
        data = json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n"
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_text(data)
        temporary.replace(path)
    return counts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(convert(args.root, args.output, args.report), sort_keys=True))


if __name__ == "__main__":
    main()
