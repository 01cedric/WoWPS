#!/usr/bin/env python3
"""Generated SmartAI script family (2.37-2.39): structure always, reproduction with WOWPS_DBC_DIR."""
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INC = ROOT / "include/game/local_npc_spell_profiles_generated.inc"
LISTS = ROOT / "include/game/local_npc_spell_lists_generated.inc"
GEOMETRY = ROOT / "include/game/local_npc_spell_geometry_generated.inc"
WAYPOINTS = ROOT / "include/game/local_npc_waypoints_generated.inc"
REPORT = ROOT / "docs/NPC_SPELL_PROFILE_REPORT.json"
GENERATED = ("local_npc_spell_profiles_generated.inc", "local_npc_spell_lists_generated.inc",
             "local_npc_spell_geometry_generated.inc", "local_npc_spell_scaling_generated.inc",
             "local_npc_spell_mana_generated.inc", "local_npc_spell_mana_scaler_generated.inc",
             "local_npc_waypoints_generated.inc", "local_npc_reward_flags_generated.inc")

# Row layout of the two script includes (local_npc_spell_profiles.hpp):
# owner (signed: a spawn guid script is the negated guid), row, entry, event,
# p1..p6, chance, flags, phaseMask, link, action, a1..a6, target, t1..t4,
# unitClass, expansion, scales, spellLevel, costScales, regenMana, manaModifier,
# and (2.38) the row's position x, y, z, o.
COLUMNS = 37
EVENTS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 31, 32, 33, 34,
          35, 36, 37, 38, 39, 40, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 72, 74, 75, 77, 82, 101, 102,
          105, 106, 107, 108, 109}
ACTIONS = {1, 2, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 17, 18, 19, 20, 21, 22, 23, 24, 25, 27, 28, 29, 30, 31, 33, 34, 35, 37, 38, 39,
           40, 41, 42, 43, 45, 46, 47, 48, 49, 51, 53, 54, 55, 56, 57, 59, 60, 61, 63, 64, 65, 66, 67, 69, 71, 72, 73, 74, 75, 78,
           79, 80, 81, 82, 83, 85, 86, 87, 88, 89, 90, 91, 92, 94, 95, 96, 97, 98, 100, 101, 102, 103, 108, 109, 110, 115, 116,
           117, 120, 121, 123, 125, 136, 142, 201, 204, 205, 207, 208, 209, 211, 212, 223, 224, 227, 229, 232, 233, 234, 235,
           236, 240}
TARGETS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 17, 18, 19, 21, 23, 24, 25, 26, 27, 28, 201, 202, 204}
POSITION_TARGETS = {8, 202}
POSITION_ACTIONS = {12, 69, 97, 101, 66, 201}
SUMMON_TYPES = {1, 2, 3, 4, 5, 6, 7, 8}
CAST_FLAGS = 1 | 2 | 32 | 64 | 0x400
ROW_FLAGS = 1 | 0x100


def catalog_gossip():
    """The menus, texts and (menu, option) pairs the catalog's gossip packs carry (2.40)."""
    sys.path.insert(0, str(ROOT / "tools/local_realm"))
    from patch_gossip_catalog import catalog_gossip as read
    return read(ROOT / "assets/local_realm/catalog")


def catalog_paths():
    """The waypoint_data path ids the catalog's paths.pack carries (2.39)."""
    import struct
    data = (ROOT / "assets/local_realm/catalog/paths.pack").read_bytes()
    magic, count, index = struct.unpack_from("<8sII", data, 0)
    assert magic == b"WPCAT01\0" and index == 16
    return {struct.unpack_from("<I", data, 16 + i * 16)[0] for i in range(count)}


def rows(path):
    out = []
    for line in path.read_text().splitlines():
        if line.startswith("{"):
            values = []
            for token in line[1:line.index("}")].split(","):
                token = token.strip()
                values.append(float(token[:-1]) if token.endswith("f") else int(token.rstrip("u")))
            out.append(values)
    return out


class NpcSpellProfileTests(unittest.TestCase):
    def test_structure(self):
        data = rows(INC)
        report = json.loads(REPORT.read_text())
        self.assertEqual(report["schemaVersion"], 5)
        self.assertEqual(len(data), report["counts"]["rows"])
        self.assertEqual(len(data), 12965)
        self.assertEqual(data, sorted(data, key=lambda r: (r[0], r[1])))
        owners, entries, casts, spells, per_owner = {}, set(), 0, set(), {}
        summon_rows, summoned, paths, patrols = 0, set(), set(), set()
        for values in data:
            self.assertEqual(len(values), COLUMNS)
            (owner, row, entry, event, p1, p2, p3, p4, p5, p6, chance, flags, phase, link, action,
             a1, a2, a3, a4, a5, a6, target, t1, t2, t3, t4, cls, exp, scales, level, cost_scales, regen, mod,
             x, y, z, o) = values
            self.assertNotEqual(owner, 0)
            self.assertGreater(entry, 0)
            # Rows keep their smart_scripts ids and rise per owner (the source
            # repeats an id under a different link, e.g. Nurse Judith 19455 id
            # 5, and the runtime resolves a link to the first row of that id
            # like SmartScript::FindLinkedEvent); the gaps are the rows the
            # creature-talk companion installs.
            self.assertGreaterEqual(row, owners.get(owner, -1))
            owners[owner] = row
            per_owner[owner] = per_owner.get(owner, 0) + 1
            entries.add(entry)
            self.assertIn(event, EVENTS)
            self.assertIn(action, ACTIONS)
            self.assertIn(target, TARGETS)
            self.assertTrue(1 <= chance <= 100)
            self.assertEqual(flags & ~ROW_FLAGS, 0)
            if event in (0, 1, 60):
                # 2.39: a repeat of 0 re-arms the row every update (RecalcTimer 0, 0).
                self.assertLessEqual(p1, p2)
                self.assertLessEqual(p3, p4)
            if action in (11, 85):
                self.assertGreater(a1, 0)
                self.assertEqual(a2 & ~CAST_FLAGS, 0)
                casts += 1
                spells.add(a1)
            if action == 75:  # ADD_AURA counts as a cast row of its spell
                self.assertGreater(a1, 0)
                casts += 1
                spells.add(a1)
            if action in (80, 87, 88):
                self.assertGreater(a1, 0)
            # 2.38: the row position is only read by the position actions;
            # a position target (8, 202) belongs to one of them.
            self.assertTrue(all(abs(v) <= 1e5 for v in (x, y)) and abs(z) <= 2e4 and abs(o) <= 100)
            if target in POSITION_TARGETS:
                self.assertIn(action, POSITION_ACTIONS)
            if action == 12:  # SUMMON_CREATURE: a catalog entry, a lifetime type, a duration for the timed ones
                self.assertGreater(a1, 0)
                self.assertIn(a2, SUMMON_TYPES)
                self.assertTrue(a3 or a2 in (5, 7, 8))
                self.assertNotEqual(target, 0)
                summon_rows += 1
                summoned.add(a1)
            if action == 53:  # ESCORT_START names a compiled waypoint path
                self.assertGreater(a2, 0)
                paths.add(a2)
            if action == 232:  # WAYPOINT_START names a waypoint_data path the catalog carries
                patrols.add(a1)
            if action == 233:
                self.assertTrue(0 < a1 <= a2 < a1 + 64)
                patrols.update(range(a1, a2 + 1))
            self.assertLessEqual(phase, 0xfff)
            self.assertIn(cls, (1, 2, 4, 8, 11))
            self.assertIn(exp, (0, 1, 2))
            self.assertIn(scales, (0, 1))
            self.assertIn(cost_scales, (0, 1))
            self.assertIn(regen, (0, 1))
            self.assertGreaterEqual(mod, 0.0)
        self.assertEqual(len(owners), report["counts"]["owners"])
        self.assertEqual(len(entries), report["counts"]["entries"])
        self.assertEqual(casts, report["counts"]["castRows"])
        self.assertLessEqual(max(per_owner.values()), 24)  # kLocalMaxNpcSpellProfilesPerEntry rows per owner
        self.assertTrue({4008, 4323, 5858, 432, 1551, 744, 17083, 352, 948, 3273, 27212, 8391, 5042, 20685, 3815, 26200, 10433, 31012, 25810, 28577,
                         16287, 2357, 3836}.issubset(entries))
        # Every guid script owner is the negated spawn guid, and its entry is
        # the spawn's creature template (an entry script for the same creature
        # may coexist: the guid script replaces it for that spawn only).
        self.assertTrue(any(owner < 0 for owner in owners))
        # Timed action lists: the same rows keyed by list id.
        lists = rows(LISTS)
        self.assertEqual(len(lists), report["counts"]["listRows"])
        self.assertEqual(len({values[0] for values in lists}), report["counts"]["lists"])
        list_ids = set()
        for values in lists:
            self.assertEqual(len(values), COLUMNS)
            self.assertGreater(values[0], 0)
            list_ids.add(values[0])
            if values[14] in (11, 85, 75):
                spells.add(values[15])
            if values[14] == 12:
                summon_rows += 1
                summoned.add(values[15])
            if values[14] == 53:
                paths.add(values[16])
            if values[14] == 232:
                patrols.add(values[15])
            if values[14] == 233:
                patrols.update(range(values[15], values[16] + 1))
        self.assertEqual(len(spells), report["counts"]["spells"])
        # 2.39: every patrol path a row starts is in the catalog's paths.pack.
        self.assertTrue(patrols)
        self.assertTrue(patrols.issubset(catalog_paths()))
        # 2.40: every GOSSIP_SELECT row names an option the catalog's gossip
        # pack carries; every SEND_GOSSIP_MENU / SET_GOSSIP_MENU names a menu
        # (or 0) and a text (or 0) it carries.
        menus, texts, options = catalog_gossip()
        gossip_rows = 0
        for values in data + lists:
            if values[3] == 62:
                self.assertIn((values[4], values[5]), options)
                gossip_rows += 1
            if values[14] == 98:
                self.assertTrue(not values[15] or values[15] in menus)
                self.assertTrue(not values[16] or values[16] in texts)
            if values[14] == 240:
                self.assertTrue(not values[15] or values[15] in menus)
        self.assertGreaterEqual(gossip_rows, 100)
        # 2.38: every list a row calls exists; the summons and paths of the report.
        for values in data + lists:
            if values[14] in (80, 87, 88):
                self.assertIn(values[15], list_ids)
                if values[14] in (87, 88):
                    self.assertIn(values[16], list_ids)
        self.assertLessEqual(summon_rows, report["counts"]["summonRows"])  # spell summons are counted by the generator
        self.assertTrue(summoned.issubset(set(report["summonEntries"])))
        self.assertEqual(len(report["summonEntries"]), report["counts"]["summonEntries"])
        self.assertEqual(report["summonEntries"], sorted(set(report["summonEntries"])))
        self.assertTrue({9526, 2462, 8421}.issubset(set(report["summonEntries"])))
        waypoints = rows(WAYPOINTS)
        self.assertEqual(len(waypoints), report["counts"]["waypoints"])
        self.assertEqual(len({w[0] for w in waypoints}), report["counts"]["waypointPaths"])
        self.assertEqual(paths, {w[0] for w in waypoints})
        self.assertEqual(waypoints, sorted(waypoints, key=lambda w: (w[0], w[1])))
        points = {}
        for path, point, x, y, z in waypoints:
            self.assertGreater(path, 0)
            self.assertEqual(point, points.get(path, 0) + 1)  # points 1..n without gaps
            points[path] = point
            self.assertTrue(abs(x) <= 1e5 and abs(y) <= 1e5 and abs(z) <= 2e4)
        self.assertLessEqual(max(points.values()), 255)
        # Geometry: cone degrees (signed: a negative value is a reverse cone)
        # and jump distances of installed spells, sorted by spell id.
        geometry = []
        for line in GEOMETRY.read_text().splitlines():
            if line.startswith("{"):
                spell, cone, jump = (int(v) for v in re.findall(r"(-?\d+)u?", line.split("}")[0]))
                geometry.append((spell, cone, jump))
                self.assertIn(spell, spells)
                self.assertTrue(cone or jump)
                self.assertTrue(-360 <= cone <= 360)
                self.assertTrue(0 <= jump <= 100)
        self.assertEqual(geometry, sorted(geometry))
        self.assertEqual(len(set(g[0] for g in geometry)), len(geometry))

    @unittest.skipUnless(os.environ.get("WOWPS_DBC_DIR"), "needs the user's WotLK DBC directory")
    def test_reward_flags(self):
        # 2.39: the CREATURE_FLAG_EXTRA_NO_PLAYER_DAMAGE_REQ entries, sorted and unique.
        text = (ROOT / "include/game/local_npc_reward_flags_generated.inc").read_text().splitlines()
        self.assertTrue(text[0].startswith("// AzerothCore "))
        entries = [int(v[:-1]) for v in text[1].rstrip(",").split(",")]
        self.assertEqual(entries, sorted(set(entries)))
        self.assertGreaterEqual(len(entries), 30)
        self.assertIn(10184, entries)  # Onyxia

    def test_generator_reproduces_checked_in_include(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([sys.executable, str(ROOT / "tools/local_realm/generate_npc_spell_profiles.py"),
                            os.environ["WOWPS_DBC_DIR"], "--output", directory, "--report", str(Path(directory) / "r.json")],
                           check=True, capture_output=True)
            for name in GENERATED:
                self.assertEqual((Path(directory) / name).read_text(), (ROOT / "include/game" / name).read_text())
            generated = json.loads((Path(directory) / "r.json").read_text())
            checked_in = json.loads(REPORT.read_text())
            self.assertEqual(generated["counts"], checked_in["counts"])
            self.assertEqual(generated["spells"], checked_in["spells"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
