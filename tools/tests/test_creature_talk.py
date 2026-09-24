#!/usr/bin/env python3
import gzip
import importlib.util
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("creature_talk", ROOT / "tools/local_realm/compile_creature_talk.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
SOURCE = ROOT / "assets/local_realm/original_script_sources.json.gz"
COMMIT = "4e80596cdaa21fa31830522f6f2d7ed8750bfffd"


class CreatureTalkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source_bytes = SOURCE.read_bytes()
        cls.source = json.loads(gzip.decompress(cls.source_bytes))
        cls.content, cls.report = MODULE.compile_talk(cls.source_bytes, COMMIT)

    def test_counts_and_checked_in_companion(self):
        self.assertEqual(self.report["counts"]["installedRules"], 96)
        self.assertEqual(self.report["counts"]["smartRows"], 227)
        self.assertEqual(self.report["counts"]["installedRules"] + self.report["counts"]["blockedRows"], 227)
        self.assertEqual(self.report["installedByAction"], {"fleeForAssist": 32, "talk": 64})

    def test_checked_in_companion_is_the_full_pinned_sql_compile(self):
        tables, digest = MODULE.load_pinned_sql_tables()
        # 2.38: the creatures the generated SmartAI family summons are
        # reachable talkers (the second generator pass reads these groups).
        summoned = json.loads((ROOT / "docs/NPC_SPELL_PROFILE_REPORT.json").read_text())["summonEntries"]
        content, report = MODULE.compile_talk_tables(tables, COMMIT, digest, summoned_entries=frozenset(summoned))
        checked = json.loads((ROOT / "assets/local_realm/creature_talk.json").read_text())
        self.assertEqual(checked, content)
        self.assertEqual(report["counts"]["installedRules"], 1678)
        keys = {(g["entry"], g["group"]) for g in content["textGroups"]}
        # Obsidion (8421, SUMMON_CREATURE of Lathoric the Black) speaks five
        # groups although no `creature` row spawns it; the talkers only a
        # spell summons (e.g. 5781 Bloodpetal Pest through the report) are
        # carried with the report and stay out without it.
        self.assertTrue({(8421, g) for g in range(5)}.issubset(keys))
        self.assertTrue({(5781, 0), (5781, 1), (8035, 0)}.issubset(keys))
        without, _ = MODULE.compile_talk_tables(tables, COMMIT, digest)
        without_keys = {(g["entry"], g["group"]) for g in without["textGroups"]}
        self.assertTrue({(8421, g) for g in range(5)}.issubset(without_keys))
        self.assertFalse({(5781, 0), (5781, 1), (8035, 0)} & without_keys)
        self.assertGreater(len(keys), len(without_keys))
        self.assertEqual(report["counts"]["installedRules"] + report["counts"]["blockedRows"], report["counts"]["smartRows"])
        # The regional capture is a strict subset of the production companion.
        production = {(r["owner"], r["row"]) for r in content["rules"]}
        self.assertTrue({(r["owner"], r["row"]) for r in self.content["rules"]}.issubset(production))

    def test_installed_rows_match_original_script_rows(self):
        smart = {(r["entryorguid"], r["id"]): r for r in self.source["tables"]["smart_scripts"] if r["source_type"] == 0}
        texts = {}
        for row in self.source["tables"]["creature_text"]:
            texts.setdefault((row["creatureid"], row["groupid"]), []).append(row["text"])
        for rule in self.content["rules"]:
            row = smart[(rule["owner"], rule["row"])]
            self.assertEqual(MODULE.ACTIONS[row["action_type"]], rule["action"])
            self.assertEqual(MODULE.EVENTS[row["event_type"]], rule["event"])
            self.assertEqual(row["link"], 0)
            self.assertEqual(row["event_phase_mask"], 0)
            self.assertEqual(rule["chance"], row["event_chance"])
            if rule["action"] == "talk":
                self.assertEqual(sorted(l["text"] for l in rule["texts"]), sorted(texts[(rule["entry"], row["action_param1"])]))
            else:
                self.assertNotIn("texts", rule)
                self.assertEqual(rule.get("withEmote", False), bool(row["action_param1"]))
            if rule["event"] in ("updateIc", "updateOoc", "healthPct"):
                self.assertTrue(rule["repeatMaxMs"] or rule.get("once"))

    def test_text_groups_carry_the_talk_rows_of_the_generated_family(self):
        # 2.37: every (entry, group) a SMART_ACTION_TALK row of a spawned
        # creature script can name, sorted, bounded, with checked lines.
        self.assertEqual(self.content["schemaVersion"], 3)
        groups = self.content["textGroups"]
        keys = [(g["entry"], g["group"]) for g in groups]
        self.assertEqual(keys, sorted(keys))
        self.assertEqual(len(keys), len(set(keys)))
        texts = {}
        for row in self.source["tables"]["creature_text"]:
            texts.setdefault((row["creatureid"], row["groupid"]), []).append(row["text"])
        for g in groups:
            self.assertTrue(0 <= g["group"] <= 255)
            self.assertTrue(g["texts"])
            self.assertEqual(sorted(l["text"] for l in g["texts"]), sorted(texts[(g["entry"], g["group"])]))
            for line in g["texts"]:
                self.assertIn(line["type"], MODULE.TEXT_TYPES.values())
                self.assertIsNone(MODULE._text_blocker(line["text"]))
        # A TALK row of a spawned owner naming its own entry's group is carried.
        spawned = {row["id1"] for row in self.source["tables"]["creature"]}
        for row in self.source["tables"]["smart_scripts"]:
            if row["source_type"] == 0 and row["action_type"] == 1 and row["entryorguid"] > 0 and row["entryorguid"] in spawned and \
                    (row["entryorguid"], row["action_param1"]) in texts and row["target_type"] in (0, 1, 7):
                lines = texts[(row["entryorguid"], row["action_param1"])]
                if all(MODULE._text_blocker(t) is None for t in lines) and row["action_param1"] <= 255:
                    self.assertIn((row["entryorguid"], row["action_param1"]), set(keys))
        self.assertLessEqual(len(keys), 16384)

    def test_guid_scripts_are_listed(self):
        owners = {rule["owner"] for rule in self.content["rules"] if rule["owner"] < 0}
        self.assertTrue({-owner for owner in owners}.issubset(set(self.content["guidScriptedSpawns"])))

    def test_provenance_is_enforced(self):
        with self.assertRaisesRegex(ValueError, "provenance"):
            MODULE.compile_talk(self.source_bytes, "0" * 40)


if __name__ == "__main__":
    unittest.main(verbosity=2)
