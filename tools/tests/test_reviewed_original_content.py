#!/usr/bin/env python3
import gzip
import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/local_realm/compile_reviewed_original_content.py"
SPEC = importlib.util.spec_from_file_location("reviewed_content", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
SOURCE_PATH = ROOT / "assets/local_realm/original_script_sources.json.gz"
WORLD_PATH = ROOT / "assets/local_realm/world.json"
COMMIT = "4e80596cdaa21fa31830522f6f2d7ed8750bfffd"


class ReviewedOriginalContentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source_bytes = SOURCE_PATH.read_bytes()
        cls.world = json.loads(WORLD_PATH.read_text())
        cls.content, cls.report = MODULE.compile_content(cls.source_bytes, COMMIT, cls.world)

    def test_actual_source_installs_reviewed_sets(self):
        self.assertEqual(len(self.content["gameObjects"]), 861)
        self.assertEqual(len(self.content["worldEvents"]), 9)
        self.assertEqual(len(self.content["gameObjectPools"]), 14)
        self.assertEqual(self.report["counts"], {
            "sourceGameObjects": 944, "installedGameObjects": 861, "installedDecorations": 606,
            "installedChairs": 79, "installedChests": 67, "installedResources": 109, "installedPools": 14,
            "installedCalendarEvents": 9, "blockedGameObjects": 83, "gameplayVerified": 0})
        ids = {row["id"] for row in self.content["gameObjects"]}
        self.assertTrue({26785,26786,26794,26795,26796,26797,26798,26799}.issubset(ids))

    def test_output_matches_checked_in_runtime_companion(self):
        checked = json.loads((ROOT / "assets/local_realm/reviewed_original_content.json").read_text())
        self.assertEqual(checked, self.content)

    def test_output_is_reproducible_and_source_bound(self):
        second, report = MODULE.compile_content(self.source_bytes, COMMIT, self.world)
        self.assertEqual(MODULE.canonical(second), MODULE.canonical(self.content))
        self.assertEqual(report, self.report)
        self.assertEqual(second["sourceSha256"], hashlib.sha256(self.source_bytes).hexdigest())

    def test_installed_rows_preserve_original_presentation(self):
        source = json.loads(gzip.decompress(self.source_bytes))
        spawns = {row["guid"]: row for row in source["tables"]["gameobject"]}
        templates = {row["entry"]: row for row in source["tables"]["gameobject_template"]}
        expected_kind = {5: {"decorative"}, 8: {"decorative"}, 7: {"chair"}, 3: {"chest", "resource"}}
        for row in self.content["gameObjects"]:
            spawn, template = spawns[row["id"]], templates[row["entry"]]
            self.assertEqual((row["entry"], row["displayId"], row["name"]), (spawn["id"], template["displayid"], template["name"]))
            self.assertEqual((row["x"], row["y"], row["z"], row["orientation"], row["scale"]),
                             (spawn["position_x"], spawn["position_y"], spawn["position_z"], spawn["orientation"], template["size"]))
            self.assertIn(row["kind"], expected_kind[template["type"]])

    def test_chest_rows_carry_original_loot_lock_and_respawn(self):
        source = json.loads(gzip.decompress(self.source_bytes))
        spawns = {row["guid"]: row for row in source["tables"]["gameobject"]}
        templates = {row["entry"]: row for row in source["tables"]["gameobject_template"]}
        loot = {}
        for row in source["tables"]["gameobject_loot_template"]:
            loot.setdefault(row["entry"], set()).add((row["item"], row["groupid"], row["chance"], row["mincount"], row["maxcount"]))
        for row in self.content["gameObjects"]:
            if row["kind"] not in ("chest", "resource"):
                continue
            template = templates[row["entry"]]
            rolled = {(r["itemId"], r["group"], r["chance"], r["minCount"], r["maxCount"]) for r in row["lootTable"]}
            self.assertEqual(rolled, loot[template["data1"]])
            if template["data3"]:
                self.assertEqual(row["respawnMs"], spawns[row["id"]]["spawntimesecs"] * 1000)
            else:
                self.assertTrue(row["persistent"])
            if template["data0"] in (29, 30):
                self.assertEqual(row["requiredSkillId"], 182)
            if template["data0"] == 30:
                self.assertEqual(row["requiredSkill"], 15)
            if template["data0"] == 38:
                self.assertEqual((row["requiredSkillId"], row["toolItemIds"][0]), (186, 2901))

    def test_pools_are_whole_and_density_preserving(self):
        members = {}
        for pool in self.content["gameObjectPools"]:
            self.assertGreaterEqual(pool["maxActive"], 1)
            self.assertLessEqual(pool["maxActive"], len(pool["members"]))
            for guid in pool["members"]:
                members[guid] = pool["id"]
        for row in self.content["gameObjects"]:
            self.assertEqual(row.get("poolId"), members.get(row["id"]))
        clipped = {p["id"]: p for p in self.report["pools"]}
        self.assertEqual(clipped[20378]["maxActive"], 5)   # complete: 17/17 captured
        self.assertEqual(clipped[20383]["maxActive"], 5)   # round(8 * 20 / 30)
        self.assertTrue(clipped[20383]["regionClipped"])

    def test_interval_event_uses_original_fixed_schedule(self):
        interval = [e for e in self.content["worldEvents"] if e["clock"] == "interval"]
        self.assertEqual(len(interval), 1)
        self.assertEqual(interval[0]["name"], "Arena Tournament")
        self.assertEqual(interval[0]["intervalStartSeconds"], interval[0]["intervalEndSeconds"])
        gated = [o for o in self.content["gameObjects"] if o.get("requiredPhaseMask") == interval[0]["activePhaseMask"]]
        self.assertEqual(len(gated), 129)

    def test_supported_event_membership_becomes_phase_gated(self):
        objects = {row["id"]: row for row in self.content["gameObjects"]}
        self.assertIn(141, objects)  # Hallow's End decoration
        self.assertGreater(objects[141]["requiredPhaseMask"], 1)
        matching = [event for event in self.content["worldEvents"] if event["activePhaseMask"] == objects[141]["requiredPhaseMask"]]
        self.assertEqual(len(matching), 1)
        self.assertEqual((matching[0]["holidayId"], matching[0]["holidayStage"]), (324, 1))

    def test_interactive_objects_are_never_downgraded_to_decorations(self):
        source = json.loads(gzip.decompress(self.source_bytes))
        templates = {row["entry"]: row for row in source["tables"]["gameobject_template"]}
        smart = {row["entryorguid"] for row in source["tables"]["smart_scripts"] if row["source_type"] == 1}
        for row in self.content["gameObjects"]:
            template = templates[row["entry"]]
            self.assertIn(template["type"], (3, 5, 7, 8))
            self.assertNotIn(row["entry"], smart)
            self.assertFalse(template["scriptname"] or template["ainame"])
        blocked_types = {templates[row["entry"]]["type"] for row in self.report["blocked"] if row["entry"] in templates}
        installed = {row["id"] for row in self.content["gameObjects"]}
        self.assertTrue(all(row["guid"] not in installed for row in self.report["blocked"]))
        self.assertTrue({0, 1, 2, 10}.issubset(blocked_types))

    def test_commit_mismatch_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "provenance"):
            MODULE.compile_content(self.source_bytes, "0" * 40, self.world)

    def test_runtime_installed_source_is_rejected(self):
        source = json.loads(gzip.decompress(self.source_bytes))
        source["runtimeInstalled"] = True
        tampered = gzip.compress(MODULE.canonical(source), mtime=0)
        with self.assertRaisesRegex(ValueError, "provenance"):
            MODULE.compile_content(tampered, COMMIT, self.world)

    def test_unknown_schema_is_rejected(self):
        source = json.loads(gzip.decompress(self.source_bytes))
        source["schemaVersion"] = 2
        tampered = gzip.compress(MODULE.canonical(source), mtime=0)
        with self.assertRaisesRegex(ValueError, "Unsupported"):
            MODULE.compile_content(tampered, COMMIT, self.world)

    def test_atomic_writer_leaves_no_staging_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "content.json"
            MODULE.atomic_json(path, self.content)
            self.assertEqual(json.loads(path.read_text()), self.content)
            self.assertFalse(path.with_name(path.name + ".tmp").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
