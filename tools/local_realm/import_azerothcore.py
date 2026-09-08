#!/usr/bin/env python3
"""Convert an AzerothCore MySQL base dump into a bounded local-realm content pack.

No database server and no SQL execution. This is a deliberately smaller game
model: unsupported scripted/conditional quests are reported and omitted.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import sys

PINNED_COMMIT = "4e80596cdaa21fa31830522f6f2d7ed8750bfffd"
REPOSITORY = "https://github.com/azerothcore/azerothcore-wotlk"
TABLES = ["creature", "creature_template", "creature_template_model",
          "creature_classlevelstats", "quest_template", "quest_template_addon",
          "creature_queststarter", "creature_questender", "item_template",
          "creature_loot_template", "playercreateinfo", "game_event_creature", "pool_creature"]
TOKEN = re.compile(r"\s*(NULL|'(?:[^'\\]|\\.|'')*'|[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|[,();])", re.S)
INSERT = re.compile(r"INSERT\s+INTO\s+`([^`]+)`\s*(\([^;]*?\))?\s+VALUES\s*", re.I)


def value(token):
    if token == "NULL":
        return None
    if not token.startswith("'"):
        return float(token) if any(c in token for c in ".eE") else int(token)
    text, out, i = token[1:-1], [], 0
    escapes = {"0": "\0", "b": "\b", "n": "\n", "r": "\r", "t": "\t", "Z": "\x1a"}
    while i < len(text):
        c = text[i]
        if c == "\\" and i + 1 < len(text):
            i += 1
            out.append(escapes.get(text[i], text[i]))
        elif c == "'" and i + 1 < len(text) and text[i + 1] == "'":
            out.append("'")
            i += 1
        else:
            out.append(c)
        i += 1
    return "".join(out)


def sql_rows(path, table):
    """Read CREATE+INSERT base dumps, including explicit columns/extended inserts.

    Refuse expressions and unsupported input rather than silently evaluating
    SQL or making misaligned data. UPDATE patches are not a base dump.
    """
    text = path.read_text(encoding="utf-8-sig")
    create = re.search(r"CREATE\s+TABLE\s+`" + re.escape(table) + r"`\s*\((.*?)\)\s*ENGINE", text, re.S | re.I)
    if not create:
        raise ValueError(f"{path}: CREATE TABLE schema missing; provide base dumps")
    columns = [s.lower() for s in re.findall(r"^\s*`([^`]+)`\s", create.group(1), re.M)]
    for match in INSERT.finditer(text):
        if match.group(1) != table:
            continue
        names = [s.lower() for s in re.findall(r"`([^`]+)`", match.group(2))] if match.group(2) else columns
        pos, row, expect = match.end(), None, "row"
        while True:
            m = TOKEN.match(text, pos)
            if not m:
                raise ValueError(f"{path}: unsupported INSERT syntax at byte {pos}")
            token, pos = m.group(1), m.end()
            if expect == "row" and token == "(":
                row, expect = [], "value"
            elif expect == "value" and token not in ",();":
                row.append(value(token))
                expect = "separator"
            elif expect == "separator" and token == ",":
                expect = "value"
            elif expect == "separator" and token == ")":
                if len(row) != len(names):
                    raise ValueError(f"{path}: {len(row)} values for {len(names)} columns")
                yield dict(zip(names, row))
                expect = "next"
            elif expect == "next" and token == ",":
                expect = "row"
            elif expect == "next" and token == ";":
                break
            else:
                raise ValueError(f"{path}: unexpected {token!r} at byte {pos}")


class Source:
    def __init__(self, sql_dir=None, source_rows=None):
        self.sql_dir = sql_dir
        self.captured = {}
        self.hashes = {}
        self.snapshot = None
        if source_rows:
            opener = gzip.open if str(source_rows).endswith(".gz") else open
            with opener(source_rows, "rt", encoding="utf-8") as f:
                self.snapshot = json.load(f)
            self.hashes = self.snapshot["sourceHashes"]

    def select(self, table, predicate=lambda row: True):
        if self.snapshot:
            source = iter(self.snapshot["tables"][table])
        else:
            path = self.sql_dir / (table + ".sql")
            self.hashes[table] = hashlib.sha256(path.read_bytes()).hexdigest()
            source = sql_rows(path, table)
        rows = [r for r in source if predicate(r)]
        self.captured[table] = rows
        return rows


def clean(text, size=96):
    # WoW's quest text tokens normally expand in the original glue/UI system.
    text = (text or "").replace("$B", "\n").replace("$b", "\n")
    text = re.sub(r"\$[Nn]", "Abenteurer", text)
    text = re.sub(r"\$[Cc]", "Held", text)
    text = re.sub(r"\$[Rr]", "Mensch", text)
    text = re.sub(r"\$[Gg]([^:;]+):([^;]+);", r"\1", text)
    # Keep within runtime byte bounds, not merely Python's character count.
    return text.encode("utf-8")[:size].decode("utf-8", errors="ignore")


def region_contains(row, args):
    return (row["map"] == args.map and row["spawnmask"] & 1 and row["phasemask"] & 1
            and (not args.bounds or (args.bounds[0] <= row["position_x"] <= args.bounds[2]
                                     and args.bounds[1] <= row["position_y"] <= args.bounds[3]))
            and (row["position_x"] - args.x) ** 2 + (row["position_y"] - args.y) ** 2 <= args.radius ** 2)


def build(source, args):
    reports = {"excludedQuests": [], "excludedCreatures": [], "fixedChoiceRewards": [], "simplifications": [
        "Bounded region only; base SQL snapshot, without subsequent SQL updates.",
        "NPC base stats are converted to local integer damage/armor; local AI replaces SmartAI/scripts.",
        "NPC hostility is an explicit Human-starter mapping, not the full faction/condition engine.",
        "At most eight direct loot items per NPC, guaranteed count; reference loot/groups/chance rolls omitted.",
        "Only one fixed item reward; choice rewards become the first upstream choice, reported individually.",
        "Timed/scripted/GO/reputation/class quests omitted.",
        "Quest and kill XP use a documented local curve, not QuestXP.dbc/retail formulas.",
        "Starting equipment/consumable quantities and the two 900000+ abilities are local starter choices.",
        "NPC paths/waypoints, spell scripts, gameobjects, vendors and trainers are not imported.",
        "Baseline has no active world events: positive event spawns excluded; one stable spawn per creature pool.",
    ]}
    spawns = source.select("creature", lambda r: region_contains(r, args))
    region_guids = {s["guid"] for s in spawns}
    events = source.select("game_event_creature", lambda r: r["guid"] in region_guids)
    event_guids = {r["guid"] for r in events if r["evententry"] > 0}
    spawns = [s for s in spawns if s["guid"] not in event_guids]
    pools = source.select("pool_creature", lambda r: r["guid"] in region_guids)
    first_pool_spawn, pool_of = {}, {}
    for row in sorted(pools, key=lambda r: r["guid"]):
        if row["guid"] in event_guids: continue
        first_pool_spawn.setdefault(row["pool_entry"], row["guid"])
        pool_of[row["guid"]] = row["pool_entry"]
    spawns = [s for s in spawns if s["guid"] not in pool_of or first_pool_spawn[pool_of[s["guid"]]] == s["guid"]]
    reports["excludedEventSpawns"] = len(event_guids)
    reports["excludedPoolAlternatives"] = len(region_guids) - len(event_guids) - len(spawns)
    entries = {s["id1"] for s in spawns if s["id1"]}
    templates = {r["entry"]: r for r in source.select("creature_template", lambda r: r["entry"] in entries)}
    models = {}
    for row in source.select("creature_template_model", lambda r: r["creatureid"] in entries):
        if row["creatureid"] not in models or row["idx"] < models[row["creatureid"]][0]:
            models[row["creatureid"]] = (row["idx"], row["creaturedisplayid"])
    # Invisible triggers, critters, scripted vehicles and class/event summons
    # are not interchangeable with ordinary combat-capable world creatures.
    valid = set()
    for entry, row in templates.items():
        why = None
        if entry not in models or not models[entry][1]: why = "missing display model"
        elif row["type"] in (0, 8, 10) or row["flags_extra"] & 128: why = "trigger/totem/critter"
        elif row["vehicleid"]: why = "vehicle AI unsupported"
        elif row["minlevel"] > 20 and not row["npcflag"]: why = "high-level event/script creature"
        if why:
            reports["excludedCreatures"].append({"id": entry, "name": row["name"], "reason": why})
        else: valid.add(entry)
    spawns = [s for s in spawns if s["id1"] in valid]
    entries = {s["id1"] for s in spawns}
    starters = source.select("creature_queststarter", lambda r: r["id"] in entries)
    candidate_ids = {r["quest"] for r in starters}
    enders = source.select("creature_questender", lambda r: r["quest"] in candidate_ids)
    quests = source.select("quest_template", lambda r: r["id"] in candidate_ids)
    addons = {r["id"]: r for r in source.select("quest_template_addon", lambda r: r["id"] in candidate_ids)}
    stats = {(r["level"], r["class"]): r for r in source.select("creature_classlevelstats")}
    lootids = {templates[e]["lootid"] for e in entries}
    loot = source.select("creature_loot_template", lambda r: r["entry"] in lootids)
    # Preserve direct quest drops before filtering quests. References are not
    # falsely treated as items; they require the separate loot group engine.
    direct_loot = [r for r in loot if r["item"] > 0 and not r["reference"] and r["lootmode"] & 1]
    available_items = {r["item"] for r in direct_loot}
    starts = source.select("playercreateinfo", lambda r: r["race"] == 1 and r["class"] == 1)
    if not starts: raise ValueError("Human Warrior playercreateinfo row missing")
    start = starts[0]
    starters_by_q = {r["quest"]: r["id"] for r in starters}
    enders_by_q = {r["quest"]: r["id"] for r in enders if r["id"] in entries}
    output_quests = []
    for q in quests:
        qid, a = q["id"], addons.get(q["id"], {})
        reason, objectives = None, []
        if qid not in enders_by_q: reason = "turn-in NPC outside region"
        elif q["minlevel"] > args.max_quest_level: reason = "minimum level outside starter range"
        elif q["allowableraces"] and not q["allowableraces"] & 1: reason = "not available to Human"
        elif a.get("allowableclasses", 0): reason = "class-specific quest not supported"
        elif q["timeallowed"] or q["requiredplayerkills"]: reason = "timed/PvP objective"
        elif q["rewardspell"] or q["startitem"] or q["rewardtitle"]: reason = "spell/start-item/title requirement"
        elif a.get("specialflags", 0) & ~4: reason = "repeat/script/exploration special flag"
        elif a.get("exclusivegroup", 0) or a.get("prevquestid", 0) < 0: reason = "exclusive/group prerequisite"
        elif any(a.get(k, 0) for k in ("sourcespellid", "requiredskillid", "requiredminrepfaction", "requiredmaxrepfaction", "rewardmailtemplateid")): reason = "skill/spell/reputation/mail condition"
        elif any(q.get(k, 0) for k in ("requiredfactionid1", "requiredfactionid2", "requireditemid5", "requireditemid6")): reason = "unsupported faction/extra objectives"
        elif any(q.get(f"rewarditem{i}", 0) for i in range(2, 5)): reason = "multiple fixed rewards unsupported"
        elif q["rewardmoney"] < 0: reason = "money payment objective"
        for i in range(1, 5):
            entry, count = q[f"requirednpcorgo{i}"], q[f"requirednpcorgocount{i}"]
            if entry < 0: reason = reason or "gameobject objective unsupported"
            elif entry and count:
                if entry not in entries: reason = reason or "kill target outside region"
                else: objectives.append({"type": "kill", "entry": entry, "count": count})
            item, count = q[f"requireditemid{i}"], q[f"requireditemcount{i}"]
            if item and count:
                if item not in available_items: reason = reason or "required item lacks direct creature drop in region"
                else: objectives.append({"type": "collect", "entry": item, "count": count})
        if len(objectives) > 4: reason = reason or "more than four objectives"
        if not reason and not objectives:
            if not q["logdescription"] and not q["questdescription"]: reason = "empty quest definition"
            else: objectives = [{"type": "talk", "entry": enders_by_q[qid], "count": 1}]
        if reason:
            reports["excludedQuests"].append({"id": qid, "title": q["logtitle"], "reason": reason})
            continue
        reward_item, reward_count = q["rewarditem1"], q["rewardamount1"]
        choices = [q[f"rewardchoiceitemid{i}"] for i in range(1, 7) if q[f"rewardchoiceitemid{i}"]]
        if choices and reward_item:
            reports["excludedQuests"].append({"id": qid, "title": q["logtitle"], "reason": "fixed plus choice rewards unsupported"})
            continue
        if choices:
            reward_item, reward_count = q["rewardchoiceitemid1"], q["rewardchoiceitemquantity1"]
            reports["fixedChoiceRewards"].append({"quest": qid, "selectedItem": reward_item, "originalChoices": choices})
        output_quests.append({"id": qid, "title": clean(q["logtitle"]),
            "description": clean(q["logdescription"] or q["questdescription"], 1024),
            "giverEntry": starters_by_q[qid], "turnInEntry": enders_by_q[qid],
            "minLevel": max(1, q["minlevel"]), "prerequisite": max(0, a.get("prevquestid", 0)),
            "xp": max(50, q["questlevel"] * 80), "money": max(0, q["rewardmoney"]),
            "rewardItem": reward_item, "rewardCount": reward_count, "objectives": objectives})
    # Never leave a quest visible if its prerequisite was rejected.
    while True:
        ids = {q["id"] for q in output_quests}
        removed = [q for q in output_quests if q["prerequisite"] and q["prerequisite"] not in ids]
        if not removed: break
        for q in removed:
            reports["excludedQuests"].append({"id": q["id"], "title": q["title"], "reason": "prerequisite excluded"})
            output_quests.remove(q)
    quest_drops = {o["entry"] for q in output_quests for o in q["objectives"] if o["type"] == "collect"}
    npc_loot = {}
    for e in entries:
        rows = [r for r in direct_loot if r["entry"] == templates[e]["lootid"]]
        rows.sort(key=lambda r: (r["item"] not in quest_drops, -r["chance"], r["item"]))
        # Keep quest drops + at most two ordinary drops to limit inventory churn.
        selected, ordinary = [], 0
        for r in rows:
            if r["questrequired"] and r["item"] not in quest_drops: continue
            if r["item"] not in quest_drops:
                if ordinary >= 2: continue
                ordinary += 1
            selected.append(r)
        if len(selected) > 8: raise ValueError(f"NPC {e}: more than eight required direct drops")
        npc_loot[e] = selected
    start_items = [(25, 1), (39, 1), (40, 1), (117, 5), (118, 3), (159, 5)]
    itemids = {i for i, _ in start_items} | {q["rewardItem"] for q in output_quests if q["rewardItem"]}
    itemids |= {r["item"] for rows in npc_loot.values() for r in rows}
    items = source.select("item_template", lambda r: r["entry"] in itemids)
    if itemids - {r["entry"] for r in items}: raise ValueError("Required item template missing")
    mapped_items = []
    slots = {13: 1, 17: 1, 21: 1, 5: 2, 20: 2, 7: 3, 8: 4}
    for i in items:
        stamina = sum(i[f"stat_value{n}"] for n in range(1, 11) if i[f"stat_type{n}"] == 7)
        slot = slots.get(i["inventorytype"], 0)
        mapped_items.append({"id": i["entry"], "name": clean(i["name"]), "displayId": i["displayid"],
            "inventoryType": i["inventorytype"], "slot": slot, "stack": max(1, min(1000, i["stackable"] or 1)),
            "maxHealth": max(0, stamina * 10), "attack": max(0, round((i["dmg_min1"] + i["dmg_max1"]) / 2)) if slot == 1 else 0,
            "armor": max(0, round(i["armor"] / 10)) if slot in (2, 3, 4) else 0,
            "heal": {117: 30, 118: 70}.get(i["entry"], 0), "mana": 40 if i["entry"] == 159 else 0,
            "value": i["sellprice"]})
    # Human-friendly factions observed in this regional source. Everything else
    # needs an explicit opt-in below; unknown factions do not become killable.
    neutral_attackable = {7, 25, 31, 32, 45, 49, 73, 90, 91}
    aggressive = {14, 16, 18, 21, 22, 26, 28, 36, 38, 40, 41, 44, 48, 67, 168, 189}
    questgivers = {q["giverEntry"] for q in output_quests} | {q["turnInEntry"] for q in output_quests}
    mapped_npcs = []
    for e in sorted(entries):
        t = templates[e]
        level = max(1, t["minlevel"])
        st = stats.get((level, t["unit_class"]), stats.get((level, 1)))
        if not st: raise ValueError(f"Missing level stats for {e}")
        hostile = t["faction"] in (neutral_attackable | aggressive) and not t["npcflag"] and e not in questgivers
        mapped_npcs.append({"id": e, "name": clean(t["name"]), "displayId": models[e][1], "level": level,
            "health": max(1, round(st["basehp0"] * t["healthmodifier"])),
            "damage": max(1, round((st["damage_base"] + st["attackpower"] / 14) * 2 * t["damagemodifier"])),
            "armor": max(0, round(st["basearmor"] * t["armormodifier"] / 20)),
            "hostile": hostile, "questGiver": e in questgivers,
            "respawnSeconds": max(15, min(300, min(s["spawntimesecs"] for s in spawns if s["id1"] == e))),
            "aggroRadius": min(20, max(0, t["detection_range"])) if hostile and t["faction"] in aggressive else 0,
            "loot": [{"itemId": r["item"], "count": max(1, r["mincount"])} for r in npc_loot[e]] if hostile else [],
            "xp": max(10, round((level * 5 + 45) * t["experiencemodifier"])) if hostile else 0,
            "money": round((t["mingold"] + t["maxgold"]) / 2) if hostile else 0})
    unkillable_targets = {n["id"] for n in mapped_npcs if not n["hostile"]}
    for q in output_quests:
        if any(o["type"] == "kill" and o["entry"] in unkillable_targets for o in q["objectives"]):
            raise ValueError(f"Quest {q['id']} targets nonattackable NPC; extend explicit faction mapping")
    # Validate collect objectives after loot truncation and hostility filtering.
    emitted_loot = {r["itemId"] for n in mapped_npcs for r in n["loot"]}
    if quest_drops - emitted_loot: raise ValueError("Required quest drops absent from emitted NPCs")
    world = {"schemaVersion": 1, "name": "Northshire and Goldshire — local realm adaptation",
        "provenance": {"repository": REPOSITORY, "commit": args.commit, "source": "data/sql/base/db_world",
            "licenseFiles": ["LICENSE-AC-GPL-2.0.txt"],
            "region": {"mapId": args.map, "x": args.x, "y": args.y, "radius": args.radius, "bounds": args.bounds},
            "exactFields": "NPC/item/quest IDs, names, model display IDs, spawn positions, objective targets/counts, quest prerequisites, item sell values",
            "adaptedFields": "combat stats, XP, abilities, consumable effects, drop selection/count probability, spawn respawn clamp; see IMPORT_REPORT.json"},
        "start": {"mapId": start["map"], "x": start["position_x"], "y": start["position_y"],
            "z": start["position_z"], "orientation": start["orientation"],
            "items": [{"itemId": i, "count": count} for i, count in start_items], "spells": [900001, 900002]},
        "items": sorted(mapped_items, key=lambda r: r["id"]),
        "spells": [{"id": 900001, "name": "Kraftschlag (lokal)", "mana": 6, "cooldownMs": 1500, "range": 4.5, "damage": 18, "heal": 0},
                   {"id": 900002, "name": "Erholung (lokal)", "mana": 20, "cooldownMs": 10000, "range": 0, "damage": 0, "heal": 50}],
        "quests": sorted(output_quests, key=lambda r: r["id"]), "npcs": mapped_npcs,
        "spawns": [{"id": s["guid"], "entry": s["id1"], "mapId": s["map"], "x": s["position_x"], "y": s["position_y"],
            "z": s["position_z"], "orientation": s["orientation"]} for s in sorted(spawns, key=lambda r: r["guid"])]}
    for name, limit in [("spawns", 65536), ("npcs", 16384), ("items", 16384), ("quests", 16384), ("spells", 4096)]:
        if len(world[name]) > limit: raise ValueError(f"{name}: exceeds runtime limit {limit}; shrink region")
    reports["coverage"] = {k: len(world[k]) for k in ("spawns", "npcs", "items", "quests", "spells")}
    reports["coverage"]["attackableNpcTypes"] = sum(n["hostile"] for n in mapped_npcs)
    reports["coverage"]["questCandidates"] = len(quests)
    reports["sourceHashes"] = source.hashes
    reports["sourceCommit"] = args.commit
    reports["region"] = world["provenance"]["region"]
    return world, reports


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    source = p.add_mutually_exclusive_group(required=True)
    source.add_argument("--sql-dir", type=Path, help="directory containing AC base db_world table SQL files")
    source.add_argument("--source-rows", type=Path, help="reproduce bundled pack from captured source_rows.json.gz")
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--capture-source", type=Path, help="write exact selected upstream rows for reproducible source distribution")
    p.add_argument("--commit", default=PINNED_COMMIT)
    p.add_argument("--map", type=int, default=0)
    p.add_argument("--x", type=float, default=-8949.95)
    p.add_argument("--y", type=float, default=-132.493)
    p.add_argument("--radius", type=float, default=850)
    p.add_argument("--bounds", nargs=4, type=float, metavar=("XMIN", "YMIN", "XMAX", "YMAX"), default=[-9850, -1050, -8550, 250])
    p.add_argument("--max-quest-level", type=int, default=10)
    args = p.parse_args(argv)
    if not (math.isfinite(args.x) and math.isfinite(args.y) and math.isfinite(args.radius) and 0 < args.radius <= 20000):
        p.error("finite coordinates and radius 0..20000 required")
    src = Source(args.sql_dir, args.source_rows)
    world, report = build(src, args)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(world, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    args.output.with_name("IMPORT_REPORT.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.capture_source:
        args.capture_source.parent.mkdir(parents=True, exist_ok=True)
        captured = {"repository": REPOSITORY, "commit": args.commit, "sourceHashes": src.hashes, "tables": src.captured}
        # Stable gzip timestamp keeps the source checkpoint reproducible.
        args.capture_source.write_bytes(gzip.compress(json.dumps(captured, ensure_ascii=False, sort_keys=True).encode(), mtime=0))
    print(json.dumps(report["coverage"], sort_keys=True))
    print(f"Rejected quests: {len(report['excludedQuests'])}; details: {args.output.with_name('IMPORT_REPORT.json')}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError) as exc:
        print(f"Import failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
