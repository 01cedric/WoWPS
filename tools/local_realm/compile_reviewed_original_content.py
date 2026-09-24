#!/usr/bin/env python3
"""Compile the reviewable original GameObject subset into local-runtime content.

The compiler stays deliberately conservative. It installs only rows whose
complete original semantics the local runtime represents:

* type 5 (GENERIC) and non-scripted type 8 (SPELL_FOCUS) presentation, whose
  addon may only carry presentation-neutral faction/NOT_SELECTABLE/NODESPAWN;
* type 7 chairs and benches (slots, height);
* type 3 chests and gathering nodes with their original loot template, the
  Lock.dbc requirement (open / herbalism / mining), respawn, consumable flag,
  GO_FLAG_INTERACT_COND quest-loot gating and exclusive pools;
* at most one positive calendar membership: a supported recurring holiday or a
  fixed game_event interval (GAMEEVENT_NORMAL).

Scripted, keyed, trapped, phased, multi-event and addon-gold rows stay blocked.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import math
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent

# WotLK recurring holidays for which the local wall-clock resolver implements
# the same stage model used by game_event.holiday / holidayStage.
SUPPORTED_HOLIDAYS = {141, 324, 341, 372, 374, 404, 409}
EVENT_ID_BASE = 0x70000000
LOCK_ROWS_PATH = HERE / "lock_rows.json"
POOL_TOTALS_PATH = HERE / "pool_totals.json"
# GameObject flags a non-interactive presentation may carry unchanged.
GO_FLAG_INTERACT_COND, GO_FLAG_NOT_SELECTABLE, GO_FLAG_NODESPAWN = 0x4, 0x10, 0x20
PRESENTATION_FLAGS = GO_FLAG_NOT_SELECTABLE | GO_FLAG_NODESPAWN
# LockType.dbc ids that any character may open without skill or key.
OPEN_LOCK_TYPES = {5, 6, 10, 12, 13, 17}
SKILL_HERBALISM, SKILL_MINING = 182, 186
# TotemCategory 165 (Mining Pick) members in 3.3.5a Item.dbc.
MINING_PICKS = [2901, 20723, 40772, 40892, 40893]
MAX_LOOT_ROWS = 192


def canonical(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False) + "\n").encode()


def atomic_json(path, value):
    data = canonical(value)
    staged = path.with_name(path.name + ".tmp")
    try:
        staged.write_bytes(data)
        staged.replace(path)
    finally:
        staged.unlink(missing_ok=True)


def load_lock_rows(path=LOCK_ROWS_PATH):
    data = json.loads(Path(path).read_text())
    if data.get("schemaVersion") != 1 or data.get("kind") != "wotlk-lock-rows":
        raise ValueError("Unsupported lock row companion")
    return {row["id"]: row for row in data["rows"]}, data["lockDbcSha256"]


def load_pool_totals(path=POOL_TOTALS_PATH):
    data = json.loads(Path(path).read_text())
    if data.get("schemaVersion") != 1 or data.get("kind") != "original-pool-totals":
        raise ValueError("Unsupported pool total companion")
    return {row["entry"]: row for row in data["pools"]}


def _local_seconds(text):
    value = datetime.strptime(text, "%Y-%m-%d %H:%M:%S")
    return (value - datetime(1970, 1, 1)).days * 86400 + value.hour * 3600 + value.minute * 60 + value.second


def _event_kind(event):
    """'holiday', 'interval' or None when the schedule is unsupported."""
    if event.get("holiday") in SUPPORTED_HOLIDAYS and 1 <= event.get("holidaystage", 0) <= 10:
        return "holiday"
    if (not event.get("holiday") and event.get("world_event") == 0 and event.get("start_time") and
            event.get("end_time") and event.get("occurence", 0) >= 1 and event.get("length", 0) >= 1):
        return "interval"
    return None


def _event_for_spawn(guid, memberships, events):
    ids = memberships.get(guid, ())
    if not ids:
        return None, None
    if len(ids) != 1 or ids[0] <= 0:
        return None, "negative or multiple calendar event membership"
    event = events.get(ids[0])
    if not event or not _event_kind(event):
        return None, "unsupported calendar event schedule"
    return event, None


def _lock_requirement(lock_id, locks):
    """(requiredSkillId, requiredSkill, tools) or a blocker string."""
    if not lock_id:
        return (0, 0, []), None
    lock = locks.get(lock_id)
    if not lock:
        return None, "missing Lock.dbc row"
    options = []
    for kind, index, skill in zip(lock["type"], lock["index"], lock["skill"]):
        if kind == 0:
            continue
        if kind != 2:
            return None, "key item or unsupported lock"
        if index in OPEN_LOCK_TYPES:
            options.append((0, 0, []))
        elif index == 2:
            options.append((SKILL_HERBALISM, skill, []))
        elif index == 3:
            options.append((SKILL_MINING, skill, list(MINING_PICKS)))
        else:
            return None, "unsupported lock type"
    if not options or len(set(map(lambda o: (o[0], o[1]), options))) != 1:
        return None, "ambiguous lock alternatives"
    return options[0], None


def _loot_rows(loot_id, loot_by_entry):
    rows = loot_by_entry.get(loot_id, [])
    if not rows:
        return None, "missing loot template"
    out = []
    for row in sorted(rows, key=lambda r: (r["groupid"], r["item"])):
        if row["reference"] or row["lootmode"] != 1 or row["mincount"] < 1 or row["maxcount"] < row["mincount"] or row["maxcount"] > 255:
            return None, "unsupported loot row"
        if row["groupid"] == 0 and row["chance"] <= 0:
            return None, "unsupported loot row"
        entry = {"itemId": row["item"], "chance": row["chance"], "group": row["groupid"],
                 "minCount": row["mincount"], "maxCount": row["maxcount"]}
        if row["questrequired"]:
            entry["questRequired"] = True
        out.append(entry)
    if len(out) > MAX_LOOT_ROWS:
        return None, "loot template too large"
    return out, None


def classify(spawn, template, addon, ctx):
    """Return (row-without-position, blockers)."""
    if not template:
        return None, ["missing template"]
    out = []
    kind = template["type"]
    guid = spawn["guid"]
    if kind not in (3, 5, 7, 8):
        out.append("interactive or unsupported GameObject type")
    if not template["displayid"]:
        out.append("missing display ID")
    _, event_reason = _event_for_spawn(guid, ctx["memberships"], ctx["events"])
    if event_reason:
        out.append(event_reason)
    if guid in ctx["pool_guids"] and kind != 3:
        out.append("pool membership")
    if spawn["id"] in ctx["smart_sources"] or -guid in ctx["smart_sources"] or spawn["scriptname"] or template["scriptname"] or template["ainame"]:
        out.append("C++ or SmartAI behavior")
    if spawn["spawnmask"] != 1 or spawn["phasemask"] != 1 or spawn["state"] != 1:
        out.append("non-default spawn, phase or state")
    if abs(spawn["rotation0"]) > 0.00001 or abs(spawn["rotation1"]) > 0.00001:
        out.append("non-yaw rotation")
    flags = (addon or {}).get("flags", 0)
    if addon and any(addon.get(field, 0) for field in ("mingold", "maxgold", "artkit0", "artkit1", "artkit2", "artkit3")):
        out.append("addon behavior")
    elif kind in (5, 7, 8) and flags & ~PRESENTATION_FLAGS:
        out.append("addon behavior")
    elif kind == 3 and flags & ~(GO_FLAG_INTERACT_COND | GO_FLAG_NODESPAWN):
        out.append("addon behavior")
    bounds = ctx["region"]["bounds"]
    if spawn["map"] != ctx["region"]["mapId"] or not (bounds[0] <= spawn["position_x"] <= bounds[2] and bounds[1] <= spawn["position_y"] <= bounds[3]):
        out.append("outside reviewed production region")
    numeric = [spawn[key] for key in ("position_x", "position_y", "position_z", "orientation")] + [template["size"]]
    if not all(math.isfinite(value) for value in numeric) or not 0.1 <= template["size"] <= 10:
        out.append("invalid presentation transform")

    row = {}
    if kind == 5 or kind == 8:
        row["kind"] = "decorative"
    elif kind == 7:
        slots, height = template["data0"], template["data1"]
        if not 1 <= slots <= 32 or not 0 <= height <= 2:
            out.append("unsupported chair geometry")
        row.update({"kind": "chair", "chairSlots": slots, "chairHeight": height})
    elif kind == 3:
        data = [template["data%d" % i] for i in range(24)]
        requirement, reason = _lock_requirement(data[0], ctx["locks"])
        if reason:
            out.append(reason)
        loot, reason = _loot_rows(data[1], ctx["loot"])
        if reason:
            out.append(reason)
        # data2 restock, data6 lootedEvent, data7 linkedTrap, data8 questId,
        # data9 level and data14+ presentation/text hooks are not modelled.
        if data[2] or data[6] or data[7] or data[8] or data[9] or data[4] > 1 or data[5] > 1:
            out.append("unsupported chest behavior")
        respawn = spawn["spawntimesecs"] * 1000
        consumable = data[3] == 1
        if consumable and not 1000 <= respawn <= 86400000:
            out.append("unsupported respawn")
        if guid in ctx["pool_guids"] and not consumable:
            out.append("pool membership")
        if requirement and loot is not None:
            skill_id, skill, tools = requirement
            row["kind"] = "resource" if skill_id else "chest"
            row["lootTable"] = loot
            if consumable:
                row["respawnMs"] = respawn
            else:
                row["persistent"] = True
            if skill_id:
                row["requiredSkillId"], row["requiredSkill"] = skill_id, skill
            if tools:
                row["toolItemIds"] = tools
            if flags & GO_FLAG_INTERACT_COND:
                if not any(r.get("questRequired") for r in loot):
                    out.append("interaction condition without quest loot")
                row["questLootOnly"] = True
            if guid in ctx["pool_guids"]:
                row["poolId"] = ctx["pool_guids"][guid]
    return row, out


def _used_phase_mask(world):
    used = 1
    for event in world.get("worldEvents", []):
        used |= int(event.get("activePhaseMask", 0)) | int(event.get("inactivePhaseMask", 0))
    for key in ("scriptTriggers", "scriptTimerActions"):
        for row in world.get(key, []):
            used |= int(row.get("addPhaseMask", 0)) | int(row.get("removePhaseMask", 0))
    for row in world.get("gameObjects", []):
        used |= int(row.get("requiredPhaseMask", 0)) | int(row.get("excludedPhaseMask", 0))
    return used


def _allocate_event_phases(event_entries, world):
    used = _used_phase_mask(world or {})
    phases = {}
    for event_entry in sorted(event_entries):
        bit = next((1 << shift for shift in range(1, 32) if not (used & (1 << shift))), None)
        if bit is None:
            raise ValueError("No free phase bits for reviewed calendar events")
        phases[event_entry] = bit
        used |= bit
    return phases


def compile_content(source_bytes, expected_commit, world=None, lock_rows=None, pool_totals=None):
    source = json.loads(gzip.decompress(source_bytes))
    if source.get("schemaVersion") != 1 or source.get("kind") != "original-world-source-rows":
        raise ValueError("Unsupported original source companion")
    if source.get("runtimeInstalled") is not False or source.get("commit") != expected_commit:
        raise ValueError("Original source provenance mismatch")
    locks, lock_sha = lock_rows if lock_rows is not None else load_lock_rows()
    totals = pool_totals if pool_totals is not None else load_pool_totals()
    tables = source["tables"]
    templates = {row["entry"]: row for row in tables["gameobject_template"]}
    addons = {row["entry"]: row for row in tables["gameobject_template_addon"]}
    events = {row["evententry"]: row for row in tables["game_event"]}
    memberships = defaultdict(list)
    for row in tables["game_event_gameobject"]:
        memberships[row["guid"]].append(row["evententry"])
    pool_guids = {row["guid"]: row["pool_entry"] for row in tables["pool_gameobject"]}
    pool_templates = {row["entry"]: row for row in tables["pool_template"]}
    loot = defaultdict(list)
    for row in tables["gameobject_loot_template"]:
        loot[row["entry"]].append(row)
    ctx = {"memberships": memberships, "events": events, "pool_guids": pool_guids,
           "smart_sources": {row["entryorguid"] for row in tables["smart_scripts"] if row["source_type"] == 1},
           "region": source["scope"]["region"], "locks": locks, "loot": loot}

    candidates, blocked = {}, {}
    for spawn in sorted(tables["gameobject"], key=lambda row: row["guid"]):
        template = templates.get(spawn["id"])
        row, row_reasons = classify(spawn, template, addons.get(spawn["id"]), ctx)
        if row_reasons:
            blocked[spawn["guid"]] = {"guid": spawn["guid"], "entry": spawn["id"], "reasons": row_reasons}
        else:
            candidates[spawn["guid"]] = (spawn, template, row)

    # A pool is installed only as a whole: every member of the original pool
    # must be representable (partial pools would change spawn probabilities).
    pool_members = defaultdict(list)
    for guid, entry in pool_guids.items():
        pool_members[entry].append(guid)
    pools = []
    for entry in sorted(pool_members):
        members = sorted(pool_members[entry])
        if not all(guid in candidates for guid in members):
            for guid in members:
                if guid in candidates:
                    spawn = candidates.pop(guid)[0]
                    blocked[guid] = {"guid": guid, "entry": spawn["id"], "reasons": ["pool has blocked members"]}
            continue
        template = pool_templates.get(entry)
        if not template or template["max_limit"] < 1 or any(r["chance"] for r in tables["pool_gameobject"] if r["pool_entry"] == entry):
            for guid in members:
                spawn = candidates.pop(guid)[0]
                blocked[guid] = {"guid": guid, "entry": spawn["id"], "reasons": ["unsupported pool template"]}
            continue
        if any(entry == row.get("pool_entry") for row in tables["game_event_pool"]):
            for guid in members:
                spawn = candidates.pop(guid)[0]
                blocked[guid] = {"guid": guid, "entry": spawn["id"], "reasons": ["event-owned pool"]}
            continue
        total = totals.get(entry)
        if not total or total["motherPool"] or total["totalMembers"] < len(members):
            for guid in members:
                spawn = candidates.pop(guid)[0]
                blocked[guid] = {"guid": guid, "entry": spawn["id"], "reasons": ["unsupported pool template"]}
            continue
        # PoolMgr keeps max_limit of all members spawned. When the regional
        # capture holds only part of a pool, keep the same expected density:
        # round(max_limit * captured / total), never below one member.
        clipped = total["totalMembers"] != len(members)
        active = max(1, round(template["max_limit"] * len(members) / total["totalMembers"])) if clipped else template["max_limit"]
        pools.append({"id": entry, "maxActive": min(active, len(members)), "members": members,
                      "name": template["description"].strip(), "originalMaxLimit": template["max_limit"],
                      "originalMembers": total["totalMembers"], "regionClipped": clipped})

    event_entries = set()
    for spawn, _, _ in candidates.values():
        event, _ = _event_for_spawn(spawn["guid"], memberships, events)
        if event:
            event_entries.add(event["evententry"])
    phases = _allocate_event_phases(event_entries, world or {})
    installed = []
    for guid in sorted(candidates):
        spawn, template, extra = candidates[guid]
        row = {
            "id": spawn["guid"],
            "entry": spawn["id"],
            "displayId": template["displayid"],
            "name": template["name"],
            "mapId": spawn["map"],
            "x": spawn["position_x"],
            "y": spawn["position_y"],
            "z": spawn["position_z"],
            "orientation": spawn["orientation"],
            "scale": template["size"],
        }
        row.update(extra)
        event, _ = _event_for_spawn(guid, memberships, events)
        if event:
            row["requiredPhaseMask"] = phases[event["evententry"]]
        installed.append(row)

    world_events = []
    for event_entry in sorted(event_entries):
        event = events[event_entry]
        row = {
            "id": EVENT_ID_BASE | event_entry,
            "name": event["description"],
            "mapId": ctx["region"]["mapId"],
            "instanceId": 0,
            "activePhaseMask": phases[event_entry],
            "enabled": True,
        }
        if _event_kind(event) == "holiday":
            row.update({"clock": "holiday", "holidayId": event["holiday"], "holidayStage": event["holidaystage"]})
        else:
            row.update({"clock": "interval", "intervalStartSeconds": _local_seconds(event["start_time"]),
                        "intervalEndSeconds": _local_seconds(event["end_time"]),
                        "occurrenceMinutes": event["occurence"], "lengthMinutes": event["length"]})
        world_events.append(row)

    digest = hashlib.sha256(source_bytes).hexdigest()
    content = {
        "schemaVersion": 2,
        "kind": "reviewed-original-runtime-content",
        "sourceCommit": expected_commit,
        "sourceSha256": digest,
        "lockDbcSha256": lock_sha,
        "worldEvents": world_events,
        "gameObjects": installed,
        "gameObjectPools": [{"id": p["id"], "maxActive": p["maxActive"], "members": p["members"]} for p in pools],
    }
    kinds = Counter(row["kind"] for row in installed)
    blocker_counts = Counter(reason for row in blocked.values() for reason in row["reasons"])
    report = {
        "schemaVersion": 2,
        "status": "reviewed-subset-installed",
        "sourceCommit": expected_commit,
        "sourceSha256": digest,
        "lockDbcSha256": lock_sha,
        "policy": ("presentation, chairs and complete chest/resource semantics (loot template, Lock.dbc, "
                   "consumable, INTERACT_COND, whole pools); one supported holiday or fixed interval may gate visibility"),
        "counts": {
            "sourceGameObjects": len(tables["gameobject"]),
            "installedGameObjects": len(installed),
            "installedDecorations": kinds["decorative"],
            "installedChairs": kinds["chair"],
            "installedChests": kinds["chest"],
            "installedResources": kinds["resource"],
            "installedPools": len(pools),
            "installedCalendarEvents": len(world_events),
            "blockedGameObjects": len(blocked),
            "gameplayVerified": 0,
        },
        "installed": [{"guid": row["id"], "entry": row["entry"], "name": row["name"], "kind": row["kind"]} for row in installed],
        "pools": pools,
        "calendarEvents": [{"eventEntry": event_entry, "clock": _event_kind(events[event_entry]), "holiday": events[event_entry]["holiday"],
                            "stage": events[event_entry]["holidaystage"], "phaseMask": phases[event_entry]} for event_entry in sorted(event_entries)],
        "blockerCounts": dict(sorted(blocker_counts.items())),
        "blocked": [blocked[guid] for guid in sorted(blocked)],
    }
    return content, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=PROJECT / "assets/local_realm/original_script_sources.json.gz")
    parser.add_argument("--world", type=Path, default=PROJECT / "assets/local_realm/world.json")
    parser.add_argument("--output", type=Path, default=PROJECT / "assets/local_realm/reviewed_original_content.json")
    parser.add_argument("--report", type=Path, default=PROJECT / "docs/REVIEWED_ORIGINAL_RUNTIME_REPORT.json")
    args = parser.parse_args()
    world = json.loads(args.world.read_text())
    commit = world["provenance"]["commit"]
    content, report = compile_content(args.source.read_bytes(), commit, world)
    atomic_json(args.output, content)
    atomic_json(args.report, report)
    print(json.dumps(report["counts"], sort_keys=True))


if __name__ == "__main__":
    main()
