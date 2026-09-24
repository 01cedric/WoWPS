#!/usr/bin/env python3
"""Compile original SmartAI creature speech and flight into a runtime companion.

Only SMART_ACTION_TALK (1) and SMART_ACTION_FLEE_FOR_ASSIST (25) rows whose
complete meaning the local authority reproduces are installed:

* events UPDATE_IC (0), UPDATE_OOC (1), HEALTH_PCT (2), AGGRO (4), KILL (5,
  player victims), DEATH (6), ACCEPTED_QUEST (19) and REWARD_QUEST (20);
  timed events need a bounded repeat or NOT_REPEATABLE;
* no link, no event phase, no attached SMART_EVENT conditions, event flags
  limited to NOT_REPEATABLE / DONT_RESET;
* the owner talks (target none/self/invoker), no talk-target redirection;
* every creature_text line of the group is say/yell/emote/whisper/boss emote
  and uses only the $n/$N/$r/$c/$g placeholders.
* WHILE_CHARMED is accepted because local creatures are never charmed.

Since 2.37 the companion also carries `textGroups`: every creature_text group
a SMART_ACTION_TALK row of a spawned creature script or a timed action list can
name, keyed by (entry, group), for the generated SmartAI family's own TALK
action (tools/local_realm/generate_npc_spell_profiles.py installs those rows
and refuses a talker whose group is missing here). Since 2.38 the entries a
SMART_ACTION_SUMMON_CREATURE row or an installed summon spell brings into the
world count as reachable talkers too (the spell-summoned ones come from the
generator's report, so the two tools converge in two passes).

Guid-specific scripts (negative entryorguid) replace the entry script of that
spawn, as SmartScript does. Everything else stays blocked with a reason.

The production companion is compiled from the full pinned SQL archives in this
directory (every spawned creature); --captured compiles the regional capture.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent

EVENTS = {0: "updateIc", 1: "updateOoc", 2: "healthPct", 4: "aggro", 5: "kill", 6: "death",
          19: "questAccept", 20: "questReward"}
ACTIONS = {1: "talk", 25: "fleeForAssist"}
MAX_TIMER_MS = 3600000
TEXT_TYPES = {12: "say", 14: "yell", 16: "emote", 15: "whisper", 41: "bossEmote"}
FLAG_NOT_REPEATABLE, FLAG_DONT_RESET, FLAG_WHILE_CHARMED = 0x1, 0x100, 0x200
TOKEN = re.compile(r"\$(\w)")
GENDER = re.compile(r"\$[gG]\s*([^:;]*):([^;]*);")


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


def _text_blocker(text):
    if not text or len(text.encode()) > 255 or "\0" in text:
        return "unsupported text length"
    rest = GENDER.sub("", text)
    for token in TOKEN.findall(rest):
        if token not in "nNrRcC":
            return "unsupported text placeholder"
    return None


PINNED_SQL = {
    "creature": "world_source_sql.tar.gz",
    "smart_scripts": "world_script_source_sql.tar.gz",
    "creature_text": "world_script_source_sql.tar.gz",
    "conditions": "vendor_source_sql.tar.gz",
}


def load_pinned_sql_tables():
    """Full pinned AzerothCore tables from the repository's SQL archives.

    Returns (tables, digest) where digest binds the exact archive bytes."""
    import sys
    import tarfile
    import tempfile
    sys.path.insert(0, str(HERE))
    from import_azerothcore import sql_rows
    digest = hashlib.sha256()
    tables = {}
    with tempfile.TemporaryDirectory() as directory:
        for table, archive in sorted(PINNED_SQL.items()):
            path = HERE / archive
            digest.update(table.encode() + b"\0" + hashlib.sha256(path.read_bytes()).digest())
            with tarfile.open(path) as tar:
                tar.extract(table + ".sql", directory, filter="data")
            tables[table] = list(sql_rows(Path(directory) / (table + ".sql"), table))
    return tables, digest.hexdigest()


def compile_talk(source_bytes, expected_commit):
    """Compile from the captured regional source companion (used by tests)."""
    source = json.loads(gzip.decompress(source_bytes))
    if source.get("schemaVersion") != 1 or source.get("kind") != "original-world-source-rows":
        raise ValueError("Unsupported original source companion")
    if source.get("runtimeInstalled") is not False or source.get("commit") != expected_commit:
        raise ValueError("Original source provenance mismatch")
    return compile_talk_tables(source["tables"], expected_commit, hashlib.sha256(source_bytes).hexdigest())


def compile_talk_tables(tables, expected_commit, digest, summoned_entries=frozenset()):
    spawns = {row["guid"]: row for row in tables["creature"]}
    spawned_entries = {row["id1"] for row in tables["creature"]}
    # 2.38: creatures a script summons are reachable talkers as well.
    reachable_entries = set(spawned_entries) | set(summoned_entries) | \
        {row["action_param1"] for row in tables["smart_scripts"] if row["action_type"] == 12 and row["action_param1"]}
    texts = defaultdict(list)
    for row in tables["creature_text"]:
        texts[(row["creatureid"], row["groupid"])].append(row)
    conditions = {(row["sourceentry"], row["sourcegroup"]) for row in tables["conditions"]
                  if row["sourcetypeorreferenceid"] == 22 and row["sourceid"] == 0}
    scripts = defaultdict(list)
    for row in tables["smart_scripts"]:
        if row["source_type"] == 0:
            scripts[row["entryorguid"]].append(row)

    rules, blocked = [], []
    for owner in sorted(scripts):
        rows = sorted(scripts[owner], key=lambda r: r["id"])
        linked = {r["link"] for r in rows if r["link"]}
        if owner < 0:
            spawn = spawns.get(-owner)
            entry = spawn["id1"] if spawn else None
        else:
            entry = owner if owner in spawned_entries else None
        for row in rows:
            if row["action_type"] not in ACTIONS:
                continue
            action = ACTIONS[row["action_type"]]
            reasons = []
            if row["event_type"] not in EVENTS:
                reasons.append("unsupported event")
            if entry is None:
                reasons.append("owner has no spawn")
            if row["link"] or row["id"] in linked:
                reasons.append("linked row")
            if row["event_phase_mask"]:
                reasons.append("event phase")
            # WHILE_CHARMED only widens when the event may run; local creatures are
            # never charmed, so it does not change behaviour here.
            if row["event_flags"] & ~(FLAG_NOT_REPEATABLE | FLAG_DONT_RESET | FLAG_WHILE_CHARMED):
                reasons.append("unsupported event flags")
            if not 1 <= row["event_chance"] <= 100:
                reasons.append("unsupported event chance")
            if (owner, row["id"] + 1) in conditions:
                reasons.append("SmartAI condition")
            if action == "talk" and (row["target_type"] not in (0, 1, 7) or row["action_param3"]):
                reasons.append("talk target redirection")
            if action == "fleeForAssist" and (row["action_param1"] not in (0, 1) or any(row["action_param%d" % i] for i in range(2, 7))):
                reasons.append("unsupported flee parameters")
            event = EVENTS.get(row["event_type"])
            params = [row["event_param%d" % i] for i in range(1, 7)]
            if event == "kill" and (params[3] or params[0] > params[1] or params[1] > 3600000):
                reasons.append("unsupported kill filter")
            if event in ("questAccept", "questReward") and any(params[1:]):
                reasons.append("unsupported quest filter")
            if event in ("aggro", "death") and any(params):
                reasons.append("unsupported event parameters")
            if event in ("updateIc", "updateOoc", "healthPct"):
                if any(params[4:]) or params[2] > params[3] or params[3] > MAX_TIMER_MS:
                    reasons.append("unsupported timer parameters")
                if event != "healthPct" and (params[0] > params[1] or params[1] > MAX_TIMER_MS):
                    reasons.append("unsupported timer parameters")
                if event == "healthPct" and not (0 <= params[0] <= params[1] <= 100):
                    reasons.append("unsupported health range")
                if not params[3] and not row["event_flags"] & FLAG_NOT_REPEATABLE:
                    reasons.append("unbounded timed repeat")
            group = texts.get((entry, row["action_param1"]), []) if entry is not None and action == "talk" else []
            if action == "talk" and not group:
                reasons.append("missing creature_text group")
            lines = []
            for text in sorted(group, key=lambda r: r["id"]):
                if text["type"] not in TEXT_TYPES:
                    reasons.append("unsupported text type")
                    break
                problem = _text_blocker(text["text"])
                if problem:
                    reasons.append(problem)
                    break
                lines.append({"text": text["text"], "type": TEXT_TYPES[text["type"]], "weight": max(0.0, float(text["probability"]))})
            if reasons:
                blocked.append({"owner": owner, "id": row["id"], "event": row["event_type"], "reasons": sorted(set(reasons))})
                continue
            rule = {"owner": owner, "row": row["id"], "entry": entry, "event": event, "action": action,
                    "chance": row["event_chance"]}
            if action == "talk":
                rule["texts"] = lines
            elif row["action_param1"]:
                rule["withEmote"] = True
            if event in ("updateIc", "updateOoc"):
                rule.update({"initialMinMs": params[0], "initialMaxMs": params[1], "repeatMinMs": params[2], "repeatMaxMs": params[3]})
            if event == "healthPct":
                rule.update({"minPct": params[0], "maxPct": params[1], "repeatMinMs": params[2], "repeatMaxMs": params[3]})
            if row["event_flags"] & FLAG_NOT_REPEATABLE:
                rule["once"] = True
                if row["event_flags"] & FLAG_DONT_RESET:
                    rule["keepOnEvade"] = True
            if event == "kill":
                rule["cooldownMinMs"], rule["cooldownMaxMs"] = params[0], params[1]
            if event in ("questAccept", "questReward") and params[0]:
                rule["questId"] = params[0]
            if action == "talk" and row["target_type"] == 7:
                rule["invokerTarget"] = True
            rules.append(rule)

    # SmartScript runs a spawn's guid script *instead of* its entry script.
    guid_scripted = sorted(-owner for owner in scripts if owner < 0 and -owner in spawns)
    text_groups, blocked_groups = compile_text_groups(tables, texts, spawns, reachable_entries)
    content = {"schemaVersion": 3, "kind": "original-creature-talk", "sourceCommit": expected_commit,
               "sourceSha256": digest, "rules": rules, "guidScriptedSpawns": guid_scripted, "textGroups": text_groups}
    talk_rows = sum(1 for rows in scripts.values() for r in rows if r["action_type"] in ACTIONS)
    report = {
        "schemaVersion": 1, "status": "reviewed-subset-installed", "sourceCommit": expected_commit, "sourceSha256": digest,
        "counts": {"smartRows": talk_rows, "installedRules": len(rules), "blockedRows": len(blocked),
                   "installedOwners": len({r["owner"] for r in rules}), "gameplayVerified": 0},
        "installedByEvent": dict(sorted(Counter(r["event"] for r in rules).items())),
        "installedByAction": dict(sorted(Counter(r["action"] for r in rules).items())),
        "blockerCounts": dict(sorted(Counter(reason for row in blocked for reason in row["reasons"]).items())),
        "blocked": blocked,
        "textGroups": {"installed": len(text_groups), "blocked": len(blocked_groups),
                       "blockerCounts": dict(sorted(Counter(reason for g in blocked_groups for reason in g["reasons"]).items()))},
    }
    return content, report


def compile_text_groups(tables, texts, spawns, spawned_entries):
    """Every creature_text group a SMART_ACTION_TALK row of a spawned creature
    script or a timed action list can name, as (entry, group) with its lines,
    for the generated SmartAI family's TALK action (2.37). The talker of a row
    is its owner's entry, a creature target's entry, or a guid target's spawn
    entry; a list row's talker is unknown here, so every spawned entry whose
    text the list names is carried. Lines follow the same text blockers as the
    companion rules."""
    wanted = set()
    for row in tables["smart_scripts"]:
        if row["action_type"] != 1:
            continue
        group = row["action_param1"]
        if row["source_type"] == 0:
            owner = row["entryorguid"]
            if owner < 0:
                spawn = spawns.get(-owner)
                entry = spawn["id1"] if spawn else None
            else:
                entry = owner if owner in spawned_entries else None
            talkers = {entry}
            if row["target_type"] in (9, 11, 19):
                talkers.add(row["target_param1"])
            elif row["target_type"] == 10:
                spawn = spawns.get(row["target_param1"])
                talkers.add(spawn["id1"] if spawn else None)
        elif row["source_type"] == 9:
            talkers = {entry for (entry, g) in texts if g == group and entry in spawned_entries}
        else:
            continue
        for talker in talkers:
            if talker is not None and (talker, group) in texts:
                wanted.add((talker, group))
    installed, blocked = [], []
    for entry, group in sorted(wanted):
        reasons, lines = [], []
        for text in sorted(texts[(entry, group)], key=lambda r: r["id"]):
            if text["type"] not in TEXT_TYPES:
                reasons.append("unsupported text type")
                break
            problem = _text_blocker(text["text"])
            if problem:
                reasons.append(problem)
                break
            lines.append({"text": text["text"], "type": TEXT_TYPES[text["type"]], "weight": max(0.0, float(text["probability"]))})
        if not lines:
            reasons.append("empty creature_text group")
        if group > 255:
            reasons.append("unsupported text group id")
        if reasons:
            blocked.append({"entry": entry, "group": group, "reasons": sorted(set(reasons))})
            continue
        installed.append({"entry": entry, "group": group, "texts": lines})
    return installed, blocked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=PROJECT / "assets/local_realm/original_script_sources.json.gz")
    parser.add_argument("--captured", action="store_true",
                        help="compile only the captured regional rows instead of the full pinned SQL archives")
    parser.add_argument("--world", type=Path, default=PROJECT / "assets/local_realm/world.json")
    parser.add_argument("--output", type=Path, default=PROJECT / "assets/local_realm/creature_talk.json")
    parser.add_argument("--report", type=Path, default=PROJECT / "docs/CREATURE_TALK_REPORT.json")
    parser.add_argument("--summon-report", type=Path, default=PROJECT / "docs/NPC_SPELL_PROFILE_REPORT.json",
                        help="the SmartAI family report whose summonEntries are reachable talkers (2.38)")
    args = parser.parse_args()
    commit = json.loads(args.world.read_text())["provenance"]["commit"]
    if args.captured:
        content, report = compile_talk(args.source.read_bytes(), commit)
    else:
        tables, digest = load_pinned_sql_tables()
        summoned = set()
        if args.summon_report.exists():
            summoned = set(json.loads(args.summon_report.read_text()).get("summonEntries", []))
        content, report = compile_talk_tables(tables, commit, digest, summoned)
    atomic_json(args.output, content)
    atomic_json(args.report, report)
    print(json.dumps(report["counts"], sort_keys=True), json.dumps(report["installedByEvent"]))


if __name__ == "__main__":
    main()
