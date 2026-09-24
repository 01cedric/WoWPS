#!/usr/bin/env python3
"""Import original SQL source rows and explicit runtime blockers, without executing SQL.

This is a lossless source companion, not an executable SmartAI interpreter.
It deliberately does not turn unsupported source semantics into approximations.
"""
from __future__ import annotations
import argparse
import collections
import gzip
import hashlib
import json
import math
from pathlib import Path
import tarfile
import tempfile

from import_azerothcore import PINNED_COMMIT, REPOSITORY, sql_rows
from import_quest_chains import read_pack
from fetch_world_script_sources import verify

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False) + "\n").encode()


def encoded_json(value, compressed=False):
    data = canonical(value)
    return gzip.compress(data, compresslevel=9, mtime=0) if compressed else data


def atomic_bytes(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    staged = path.with_name(path.name + ".tmp")
    try:
        staged.write_bytes(data)
        staged.replace(path)
    finally:
        staged.unlink(missing_ok=True)


def write_json(path, value, compressed=False):
    atomic_bytes(path, encoded_json(value, compressed))


def write_companion_report(companion_path, companion, report_path, report):
    """Publish the cross-referenced outputs as one recoverable transaction.

    Two filesystem paths cannot share one POSIX rename.  Both complete files
    are staged first, ordinary I/O failures roll back both previous versions,
    and a durable marker makes an abrupt process stop detectable instead of
    leaving a silently plausible mixed generation.
    """
    companion_data = encoded_json(companion, compressed=True)
    report = dict(report)
    report["sourceCompanionSha256"] = hashlib.sha256(companion_data).hexdigest()
    report_data = encoded_json(report)
    marker = report_path.with_name(report_path.name + ".pending")
    old_companion = companion_path.read_bytes() if companion_path.exists() else None
    old_report = report_path.read_bytes() if report_path.exists() else None
    atomic_bytes(marker, canonical({
        "companion": str(companion_path),
        "companionSha256": hashlib.sha256(companion_data).hexdigest(),
        "report": str(report_path),
        "reportSha256": hashlib.sha256(report_data).hexdigest(),
    }))
    try:
        atomic_bytes(companion_path, companion_data)
        atomic_bytes(report_path, report_data)
    except BaseException:
        if old_companion is None:
            companion_path.unlink(missing_ok=True)
        else:
            atomic_bytes(companion_path, old_companion)
        if old_report is None:
            report_path.unlink(missing_ok=True)
        else:
            atomic_bytes(report_path, old_report)
        raise
    else:
        marker.unlink()


class Sources:
    def __init__(self, manifest, archive_dir, source_dir=None):
        if manifest.get("commit") != PINNED_COMMIT or manifest.get("repository") != REPOSITORY:
            raise ValueError("The source manifest does not identify the supported pinned revision")
        self.manifest, self.archive_dir, self.source_dir = manifest, archive_dir, source_dir
        self.cache, self.hashes = {}, {}

    def read(self, name):
        if name in self.cache:
            return self.cache[name]
        filename = name + ".sql"
        row = self.manifest["tables"].get(filename)
        if not row:
            raise ValueError(f"Missing pinned source manifest entry: {filename}")
        if self.source_dir:
            path = self.source_dir / filename
            if not path.is_file():
                raise ValueError(f"Missing required original SQL input: {filename}")
            data = path.read_bytes()
        else:
            archive_path = self.archive_dir / row["archive"]
            if not archive_path.is_file():
                raise ValueError(f"Missing required original archive: {row['archive']}")
            with tarfile.open(archive_path) as archive:
                members = [m for m in archive.getmembers() if m.name == filename]
                if len(members) != 1 or not members[0].isfile() or members[0].size > 64 * 1024 * 1024:
                    raise ValueError(f"Missing or invalid original table: {filename}")
                data = archive.extractfile(members[0]).read()
        verify(data, row, filename)
        self.hashes[filename] = row["sha256"]
        with tempfile.TemporaryDirectory(prefix="wowps-original-script-") as temporary:
            path = Path(temporary) / filename
            path.write_bytes(data)
            rows = list(sql_rows(path, name))
        self.cache[name] = rows
        return rows


def in_region(row, region):
    if row["map"] != region["mapId"]:
        return False
    x, y = row["position_x"], row["position_y"]
    if not math.isfinite(x) or not math.isfinite(y):
        raise ValueError("Non-finite source position")
    bounds = region.get("bounds")
    return ((not bounds or bounds[0] <= x <= bounds[2] and bounds[1] <= y <= bounds[3]) and
            (x - region["x"]) ** 2 + (y - region["y"]) ** 2 <= region["radius"] ** 2)


def smart_key(row):
    # The upstream SQL primary key includes link. Distinct rows may share id.
    return row["source_type"], row["entryorguid"], row["id"], row["link"]


def select_smart(rows, entries, guids, object_entries, object_guids, quests):
    groups = collections.defaultdict(list)
    seen = set()
    for row in rows:
        key = smart_key(row)
        if key in seen:
            raise ValueError(f"Duplicate SmartAI source row: {key}")
        seen.add(key)
        groups[key[:2]].append(row)
    selected = set()
    for key, values in groups.items():
        source, entry = key
        actor_match = (source == 0 and (entry in entries or -entry in guids) or
                       source == 1 and (entry in object_entries or -entry in object_guids))
        quest_match = any(row["event_type"] in (19, 20) and row["event_param1"] in quests for row in values)
        if actor_match or quest_match:
            selected.add(key)
    missing = set()
    pending = list(sorted(selected))
    while pending:
        key = pending.pop()
        for row in groups[key]:
            action = row["action_type"]
            refs = []
            if action == 80:  # SMART_ACTION_CALL_TIMED_ACTIONLIST
                refs = [row["action_param1"]]
            elif action == 87:  # SMART_ACTION_CALL_RANDOM_TIMED_ACTIONLIST
                refs = [row[f"action_param{i}"] for i in range(1, 7)]
            elif action == 88:  # SMART_ACTION_CALL_RANDOM_RANGE_TIMED_ACTIONLIST
                low, high = row["action_param1"], row["action_param2"]
                if low > high or high - low > 10000:
                    missing.add((9, low, "unsupported timed-list range"))
                    continue
                refs = range(low, high + 1)
            for entry in refs:
                if not entry:
                    continue
                dependency = (9, entry)
                if dependency not in groups:
                    missing.add((9, entry, "missing timed action list"))
                elif dependency not in selected:
                    selected.add(dependency)
                    pending.append(dependency)
    result = sorted((row for key in selected for row in groups[key]), key=smart_key)
    # A linked event is local to one source group and must not disappear.
    event_ids = {key[:3] for key in seen}
    for row in result:
        if row["link"] and (row["source_type"], row["entryorguid"], row["link"]) not in event_ids:
            missing.add((row["source_type"], row["entryorguid"], f"missing linked row {row['link']}"))
    return result, sorted(missing)


def reference_closure(rows, seeds):
    by_entry = collections.defaultdict(list)
    for row in rows:
        by_entry[row["entry"]].append(row)
    selected, pending, missing = set(), list(seeds), set()
    while pending:
        entry = pending.pop()
        if not entry or entry in selected:
            continue
        selected.add(entry)
        if entry not in by_entry:
            missing.add(entry)
        for row in by_entry[entry]:
            if row["reference"]:
                pending.append(row["reference"])
    return [row for key in sorted(selected) for row in by_entry[key]], sorted(missing)


def event_blockers(row):
    reasons = ["runtime scheduler currently accepts simulation-time authored events only"]
    if row["start_time"] is None or row["end_time"] is None or row["holiday"]:
        reasons.append("requires original Holidays.dbc/calendar schedule resolution")
    else:
        reasons.append("requires absolute calendar timestamps and explicit source timezone policy")
    if row["world_event"]:
        reasons.append("requires original world-event state/progress machine")
    return reasons


def smart_blockers(row, quests):
    reasons = ["original SmartAI event/target/action semantics are not the authored ScriptAction ABI"]
    if row["event_type"] in (19, 20) and row["event_param1"] and row["event_param1"] not in quests:
        reasons.append("referenced quest is absent from the current adapted quest catalog")
    if row["event_chance"] != 100:
        reasons.append("probabilistic event dispatch")
    if row["event_phase_mask"]:
        reasons.append("per-AI event phases are not player world phase masks")
    if row["event_flags"]:
        reasons.append("original event lifecycle/difficulty flags")
    if row["source_type"] == 9:
        reasons.append("timed action-list ordering, delays and invoker context")
    if row["action_type"] == 11:
        reasons.append("original caster identity, cast flags and spell-effect semantics")
    if row["action_type"] in (1, 4, 5):
        reasons.append("original dialogue/emote/sound recipient and presentation semantics")
    return reasons


def object_blockers(spawn, template, loot, events, pools, addon, smart):
    reasons = []
    kind = template["type"] if template else -1
    if not template:
        return ["missing original gameobject_template row"]
    if kind not in (0, 3):
        reasons.append(f"original GameObject type {kind} is outside the local door/chest/resource profile")
    if events:
        reasons.append("original calendar event membership (including negative events)")
    if pools:
        reasons.append("original pool selection and respawn ownership")
    if spawn["spawnmask"] != 1 or spawn["phasemask"] != 1:
        reasons.append("original spawn/phase mask semantics")
    if spawn["scriptname"] or template["scriptname"] or template["ainame"] or smart:
        reasons.append("original C++/SmartAI object script")
    if abs(spawn["rotation0"]) > .00001 or abs(spawn["rotation1"]) > .00001:
        reasons.append("non-yaw object rotation")
    if spawn["state"] != 1:
        reasons.append("non-default original initial state")
    if spawn["spawntimesecs"] <= 0 or spawn["spawntimesecs"] > 86400:
        reasons.append("original negative/permanent/long respawn semantics")
    if kind in (0, 3):
        lock = template["data1"] if kind == 0 else template["data0"]
        if lock:
            reasons.append(f"requires original Lock.dbc record {lock}")
    if kind == 3:
        if not loot:
            reasons.append("missing original loot rows")
        if any(row["reference"] or row["chance"] != 100 or row["groupid"] or
               row["mincount"] != row["maxcount"] or row["lootmode"] != 1 for row in loot):
            reasons.append("loot reference/chance/group/count/mode behavior")
        if any(row["questrequired"] for row in loot):
            reasons.append("quest-specific loot eligibility and objective-count caps")
        if template["data3"] != 1:
            reasons.append("non-consumable original chest lifecycle")
    if addon and any(addon.get(field, 0) for field in ("faction", "flags", "mingold", "maxgold", "artkit0", "artkit1", "artkit2", "artkit3")):
        reasons.append("original addon faction/flags/gold/artkit behavior")
    # Even a row without the blockers above needs a reviewed conversion of all
    # remaining type-specific data fields, use radius and initial state.
    if not reasons:
        reasons.append("type-specific GameObject fields require reviewed conversion before activation")
    return reasons


def build(sources, world, quests):
    region = world["provenance"]["region"]
    quest_ids = set(quests) | {q["id"] for q in world.get("quests", [])}
    region_guids = {s["id"] for s in world["spawns"]}
    entries = {s["entry"] for s in world["spawns"]}
    selected = {}
    for table in ("creature_queststarter", "creature_questender"):
        selected[table] = [r for r in sources.read(table) if r["quest"] in quest_ids]
        entries.update(r["id"] for r in selected[table])
    for q in quests.values():
        for objective in q.get("objectives", []):
            if objective.get("type") in ("kill", "talk") and objective.get("entry", 0) > 0:
                entries.add(objective["entry"])
    selected["creature"] = [r for r in sources.read("creature") if r["guid"] in region_guids or r["id1"] in entries]
    creature_guids = {r["guid"] for r in selected["creature"]}
    entries.update(r["id1"] for r in selected["creature"])
    selected["creature_template"] = [r for r in sources.read("creature_template") if r["entry"] in entries]
    object_entries = set()
    for table in ("gameobject_queststarter", "gameobject_questender"):
        selected[table] = [r for r in sources.read(table) if r["quest"] in quest_ids]
        object_entries.update(r["id"] for r in selected[table])
    selected["gameobject"] = [r for r in sources.read("gameobject") if in_region(r, region) or r["id"] in object_entries]
    object_guids = {r["guid"] for r in selected["gameobject"]}
    object_entries.update(r["id"] for r in selected["gameobject"])
    selected["gameobject_template"] = [r for r in sources.read("gameobject_template") if r["entry"] in object_entries]
    selected["gameobject_template_addon"] = [r for r in sources.read("gameobject_template_addon") if r["entry"] in object_entries]
    smart, missing_smart = select_smart(sources.read("smart_scripts"), entries, creature_guids, object_entries, object_guids, quest_ids)
    selected["smart_scripts"] = smart
    selected["quest_template"] = [r for r in sources.read("quest_template") if r["id"] in quest_ids or
                                  r["id"] in {s["event_param1"] for s in smart if s["event_type"] in (19, 20)}]
    selected["quest_template_addon"] = [r for r in sources.read("quest_template_addon") if r["id"] in {q["id"] for q in selected["quest_template"]}]
    selected["creature_text"] = [r for r in sources.read("creature_text") if r["creatureid"] in entries]
    selected["creature_addon"] = [r for r in sources.read("creature_addon") if r["guid"] in creature_guids]
    selected["creature_template_addon"] = [r for r in sources.read("creature_template_addon") if r["entry"] in entries]
    paths = {r["path_id"] for table in ("creature_addon", "creature_template_addon") for r in selected[table] if r["path_id"]}
    selected["waypoint_data"] = [r for r in sources.read("waypoint_data") if r["id"] in paths]
    waypoint_actions = {r["action"] for r in selected["waypoint_data"] if r["action"]}
    selected["waypoint_scripts"] = [r for r in sources.read("waypoint_scripts") if r["id"] in waypoint_actions]
    # Source action 53 starts an escort path: parameter 2 is the path ID.
    escort_paths = {r["action_param2"] for r in smart if r["action_type"] == 53 and r["action_param2"]}
    selected["waypoints"] = [r for r in sources.read("waypoints") if r["entry"] in escort_paths]
    loot_entries = {r["data1"] for r in selected["gameobject_template"] if r["type"] == 3 and r["data1"]}
    selected["gameobject_loot_template"] = [r for r in sources.read("gameobject_loot_template") if r["entry"] in loot_entries]
    selected["reference_loot_template"], missing_loot = reference_closure(sources.read("reference_loot_template"),
        {r["reference"] for r in selected["gameobject_loot_template"] if r["reference"]})
    for table, ids in (("pool_creature", creature_guids), ("pool_gameobject", object_guids)):
        selected[table] = [r for r in sources.read(table) if r["guid"] in ids]
    pool_ids = {r["pool_entry"] for table in ("pool_creature", "pool_gameobject") for r in selected[table]}
    pool_links = sources.read("pool_pool")
    while True:
        before = len(pool_ids)
        for r in pool_links:
            if r["pool_id"] in pool_ids:
                pool_ids.add(r["mother_pool"])
        if len(pool_ids) == before:
            break
    selected["pool_pool"] = [r for r in pool_links if r["pool_id"] in pool_ids]
    selected["pool_template"] = [r for r in sources.read("pool_template") if r["entry"] in pool_ids]
    for table, ids in (("game_event_creature", creature_guids), ("game_event_gameobject", object_guids),
                       ("game_event_model_equip", creature_guids), ("game_event_npcflag", creature_guids)):
        selected[table] = [r for r in sources.read(table) if r["guid"] in ids]
    selected["game_event_pool"] = [r for r in sources.read("game_event_pool") if r["pool_entry"] in pool_ids]
    for table in ("game_event_creature_quest", "game_event_gameobject_quest", "game_event_quest_condition"):
        selected[table] = [r for r in sources.read(table) if r["quest"] in quest_ids]
    event_ids = {abs(r["evententry"]) for table, rows in selected.items() if table.startswith("game_event") for r in rows}
    for r in smart:
        if r["event_type"] in (68, 69) and r["event_param1"]:
            event_ids.add(r["event_param1"])
    prerequisites = sources.read("game_event_prerequisite")
    while True:
        before = len(event_ids)
        for r in prerequisites:
            if r["evententry"] in event_ids:
                event_ids.add(r["prerequisite_event"])
        if len(event_ids) == before:
            break
    selected["game_event_prerequisite"] = [r for r in prerequisites if r["evententry"] in event_ids]
    selected["game_event_condition"] = [r for r in sources.read("game_event_condition") if r["evententry"] in event_ids]
    selected["game_event"] = [r for r in sources.read("game_event") if r["evententry"] in event_ids]
    # Keep the complete condition graph: reference templates can be shared by
    # loot, SmartAI, quest and gossip conditions outside the immediate owner.
    selected["conditions"] = sources.read("conditions")
    template_by_id = {r["entry"]: r for r in selected["gameobject_template"]}
    addon_by_id = {r["entry"]: r for r in selected["gameobject_template_addon"]}
    loot_by_entry = collections.defaultdict(list)
    for row in selected["gameobject_loot_template"]:
        loot_by_entry[row["entry"]].append(row)
    event_objects = {r["guid"] for r in selected["game_event_gameobject"]}
    pool_objects = {r["guid"] for r in selected["pool_gameobject"]}
    smart_objects = {r["entryorguid"] for r in smart if r["source_type"] == 1}
    object_reports = []
    for r in selected["gameobject"]:
        template = template_by_id.get(r["id"])
        loot = loot_by_entry.get(template["data1"], []) if template and template["type"] == 3 else []
        object_reports.append({"guid": r["guid"], "entry": r["id"], "name": template["name"] if template else "",
            "region": in_region(r, region), "status": "blocked", "reasons": object_blockers(r, template, loot,
                r["guid"] in event_objects, r["guid"] in pool_objects, addon_by_id.get(r["id"]),
                r["id"] in smart_objects or -r["guid"] in smart_objects)})
    smart_reports = [{"sourceType": r["source_type"], "entryOrGuid": r["entryorguid"], "id": r["id"], "link": r["link"],
                      "event": r["event_type"], "action": r["action_type"], "target": r["target_type"],
                      "comment": r["comment"], "status": "blocked", "reasons": smart_blockers(r, quest_ids)} for r in smart]
    event_reports = [{"id": r["evententry"], "name": r["description"], "status": "blocked", "reasons": event_blockers(r)}
                     for r in selected["game_event"]]
    path_reports = [{"id": path, "table": table, "points": sum(r[field] == path for r in selected[table]), "status": "blocked",
                     "reasons": ["original waypoint ownership/movement/actions cannot be inferred as a local quest escort"]}
                    for table, field, ids in (("waypoint_data", "id", paths), ("waypoints", "entry", escort_paths)) for path in sorted(ids)]
    reports = {"schemaVersion": 1, "commit": PINNED_COMMIT, "status": "source-imported-runtime-blocked", "runtimeInstalled": False,
        "gameplayVerified": 0, "runtimeImportCounts": {"scriptActions": 0, "scriptTriggers": 0, "gameObjects": 0, "escortRoutes": 0, "worldEvents": 0},
        "scope": {"catalogQuestCount": len(quests), "questIds": sorted(quest_ids), "region": region,
                  "regionObjectPlacements": sum(r["region"] for r in object_reports),
                  "regionCreatureSmartRows": sum(r["source_type"] == 0 and (r["entryorguid"] in {s["entry"] for s in world["spawns"]} or -r["entryorguid"] in region_guids) for r in smart)},
        "tableCounts": {table: len(rows) for table, rows in sorted(selected.items())},
        "smartScripts": smart_reports, "gameObjects": sorted(object_reports, key=lambda r: r["guid"]),
        "worldEvents": sorted(event_reports, key=lambda r: r["id"]), "waypoints": path_reports,
        "missingDependencies": {"timedOrLinkedActions": missing_smart, "lootReferences": missing_loot,
                                "eventIds": sorted(event_ids - {r["evententry"] for r in selected["game_event"]}),
                                "waypointDataIds": sorted(paths - {r["id"] for r in selected["waypoint_data"]}),
                                "escortPathIds": sorted(escort_paths - {r["entry"] for r in selected["waypoints"]})},
        "requiredClientRecords": {
            "Lock.dbc": sorted({r["data1"] if r["type"] == 0 else r["data0"] for r in selected["gameobject_template"]
                                if r["type"] in (0, 3) and (r["data1"] if r["type"] == 0 else r["data0"])}),
            "Holidays.dbc": sorted({r["holiday"] for r in selected["game_event"] if r["holiday"]}),
            "Spell.dbc_for_SmartAction11": sorted({r["action_param1"] for r in smart if r["action_type"] == 11 and r["action_param1"]})},
        "requiredCppScriptNames": sorted({r["scriptname"] for table in ("creature_template", "gameobject", "gameobject_template")
                                           for r in selected[table] if r.get("scriptname")}),
        "referencedQuestIdsAbsentFromCatalog": sorted({r["event_param1"] for r in smart
            if r["event_type"] in (19, 20) and r["event_param1"] and r["event_param1"] not in quest_ids}),
        "additionalRequiredInputs": ["Lock.dbc and Holidays.dbc from the matching client when their semantics are implemented",
            "matching Spell.dbc/spell script implementation for original casts",
            "pinned C++ scripts for referenced ScriptName values; SmartAI rows alone do not replace them"],
        "nonExhaustiveDependencyBoundary": "Captures called timed lists, linked events, loot references, pool parents and calendar prerequisites; arbitrary target selectors, C++ scripts and spell chains remain unexecuted."}
    companion = {"schemaVersion": 1, "kind": "original-world-source-rows", "runtimeInstalled": False,
                 "repository": REPOSITORY, "commit": PINNED_COMMIT, "scope": reports["scope"], "tables": selected}
    return companion, reports


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=HERE / "world_script_source_manifest.json")
    parser.add_argument("--archive-dir", type=Path, default=HERE)
    parser.add_argument("--sql-dir", type=Path, help="Directory containing the exact pinned base SQL tables")
    parser.add_argument("--world", type=Path, default=PROJECT / "assets/local_realm/world.json")
    parser.add_argument("--catalog", type=Path, default=PROJECT / "assets/local_realm/catalog")
    parser.add_argument("--output", type=Path, default=PROJECT / "assets/local_realm/original_script_sources.json.gz")
    parser.add_argument("--report", type=Path, default=PROJECT / "docs/ORIGINAL_SCRIPT_IMPORT_REPORT.json")
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    sources = Sources(manifest, args.archive_dir, args.sql_dir)
    catalog_manifest = json.loads((args.catalog / "manifest.json").read_text())
    data = (args.catalog / "quests.pack").read_bytes()
    if catalog_manifest["sourceCommit"] != PINNED_COMMIT or hashlib.sha256(data).hexdigest() != catalog_manifest["files"]["quests.pack"]["sha256"]:
        raise ValueError("Quest catalog provenance/hash does not match its manifest")
    world = json.loads(args.world.read_text())
    if world["provenance"]["commit"] != PINNED_COMMIT:
        raise ValueError("Authored region does not identify the pinned original revision")
    companion, report = build(sources, world, read_pack(args.catalog / "quests.pack"))
    provenance = {"manifestSha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
                  "worldSha256": hashlib.sha256(args.world.read_bytes()).hexdigest(),
                  "questPackSha256": hashlib.sha256(data).hexdigest(), "sourceHashes": dict(sorted(sources.hashes.items()))}
    companion["provenance"] = provenance
    report["provenance"] = provenance
    write_companion_report(args.output, companion, args.report, report)
    print(json.dumps({"tables": len(companion["tables"]), "rows": sum(report["tableCounts"].values()),
                      "smartRows": len(report["smartScripts"]), "objectPlacements": len(report["gameObjects"]),
                      "events": len(report["worldEvents"]), "runtimeInstalled": False}, sort_keys=True))


if __name__ == "__main__":
    main()
