#!/usr/bin/env python3
"""Focused rejection/closure tests for the original SQL source companion."""
import gzip
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "local_realm"))
from import_azerothcore import PINNED_COMMIT, REPOSITORY
from fetch_world_script_sources import verify, write_archive
from import_world_script_sources import (Sources, canonical, event_blockers, in_region,
    object_blockers, reference_closure, select_smart, smart_blockers, write_companion_report, write_json)


def smart(entry, source=0, row_id=0, action=1, **values):
    row = {"entryorguid": entry, "source_type": source, "id": row_id, "link": 0,
           "event_type": 19, "event_param1": 7, "event_chance": 100, "event_flags": 0,
           "event_phase_mask": 0, "action_type": action}
    row.update({f"action_param{i}": 0 for i in range(1, 7)})
    row.update(values)
    return row


class OriginalSourceTests(unittest.TestCase):
    def test_nested_timed_lists_and_cycle_are_preserved_once(self):
        rows = [smart(42, action=80, action_param1=4200),
                smart(4200, source=9, action=80, action_param1=4201),
                smart(4201, source=9, action=80, action_param1=4200)]
        selected, missing = select_smart(rows, {42}, set(), set(), set(), set())
        self.assertEqual(len(selected), 3)
        self.assertEqual(missing, [])

    def test_random_list_closure_and_missing_list_are_explicit(self):
        rows = [smart(42, action=87, action_param1=4200, action_param2=999), smart(4200, source=9)]
        selected, missing = select_smart(rows, {42}, set(), set(), set(), set())
        self.assertEqual(len(selected), 2)
        self.assertEqual(missing, [(9, 999, "missing timed action list")])

    def test_random_range_is_bounded(self):
        rows = [smart(42, action=88, action_param1=1, action_param2=1000000000)]
        _, missing = select_smart(rows, {42}, set(), set(), set(), set())
        self.assertEqual(missing, [(9, 1, "unsupported timed-list range")])

    def test_missing_local_link_is_explicit(self):
        rows = [smart(42, link=2), smart(43, row_id=2)]
        _, missing = select_smart(rows, {42}, set(), set(), set(), set())
        self.assertEqual(missing, [(0, 42, "missing linked row 2")])

    def test_duplicate_source_identity_rejected(self):
        with self.assertRaisesRegex(ValueError, "Duplicate SmartAI"):
            select_smart([smart(42), smart(42)], {42}, set(), set(), set(), set())

    def test_upstream_same_id_different_link_is_not_a_duplicate(self):
        rows = [smart(42), smart(42, link=1), smart(42, row_id=1)]
        selected, missing = select_smart(rows, {42}, set(), set(), set(), set())
        self.assertEqual(len(selected), 3)
        self.assertEqual(missing, [])

    def test_guid_override_and_quest_seed_select_complete_group(self):
        rows = [smart(-12, event_type=4), smart(100, row_id=0), smart(100, row_id=1, event_type=6),
                smart(999, event_type=4)]
        selected, _ = select_smart(rows, set(), {12}, set(), set(), {7})
        self.assertEqual([(r["entryorguid"], r["id"]) for r in selected], [(-12, 0), (100, 0), (100, 1)])

    def test_loot_reference_cycle_and_missing_reference(self):
        rows = [{"entry": 1, "reference": 2}, {"entry": 2, "reference": 1}, {"entry": 2, "reference": 3}]
        selected, missing = reference_closure(rows, {1})
        self.assertEqual(selected, rows)
        self.assertEqual(missing, [3])

    def test_holiday_schedule_cannot_become_simulation_timer(self):
        reasons = event_blockers({"start_time": None, "end_time": None, "holiday": 341, "world_event": 0})
        self.assertTrue(any("Holidays.dbc" in reason for reason in reasons))
        self.assertTrue(any("simulation-time" in reason for reason in reasons))

    def test_absolute_world_event_preserves_both_blockers(self):
        reasons = event_blockers({"start_time": "2000-01-01 00:00:00", "end_time": "2030-01-01 00:00:00",
                                 "holiday": 0, "world_event": 1})
        self.assertTrue(any("timezone" in reason for reason in reasons))
        self.assertTrue(any("state/progress" in reason for reason in reasons))

    def test_absent_quest_and_cast_semantics_stay_blocked(self):
        reasons = smart_blockers(smart(197, action=11, event_param1=54), {7})
        self.assertTrue(any("absent" in reason for reason in reasons))
        self.assertTrue(any("caster identity" in reason for reason in reasons))

    def test_phase_is_not_silently_player_phase(self):
        reasons = smart_blockers(smart(42, event_phase_mask=2, event_chance=30), {7})
        self.assertTrue(any("world phase" in reason for reason in reasons))
        self.assertTrue(any("probabilistic" in reason for reason in reasons))

    def test_object_lock_pool_event_and_questloot_stay_blocked(self):
        spawn = {"spawnmask": 1, "phasemask": 1, "scriptname": "", "rotation0": 0, "rotation1": 0,
                 "state": 1, "spawntimesecs": 60}
        template = {"type": 3, "scriptname": "", "ainame": "", "data0": 43, "data3": 1}
        loot = [{"reference": 0, "chance": 100, "groupid": 0, "mincount": 1, "maxcount": 1,
                 "lootmode": 1, "questrequired": 1}]
        reasons = object_blockers(spawn, template, loot, True, True, None, False)
        for text in ("Lock.dbc", "pool", "calendar", "objective-count"):
            self.assertTrue(any(text in reason for reason in reasons), text)

    def test_no_unreviewed_door_is_automatically_activated(self):
        spawn = {"spawnmask": 1, "phasemask": 1, "scriptname": "", "rotation0": 0, "rotation1": 0,
                 "state": 1, "spawntimesecs": 60}
        template = {"type": 0, "scriptname": "", "ainame": "", "data1": 0}
        self.assertEqual(len(object_blockers(spawn, template, [], False, False, None, False)), 1)

    def test_region_uses_both_bounds_and_radius(self):
        region = {"mapId": 0, "x": 0, "y": 0, "radius": 10, "bounds": [-5, -5, 5, 5]}
        self.assertTrue(in_region({"map": 0, "position_x": 4, "position_y": 4}, region))
        self.assertFalse(in_region({"map": 0, "position_x": 6, "position_y": 0}, region))
        self.assertFalse(in_region({"map": 1, "position_x": 0, "position_y": 0}, region))

    def test_tampered_or_wrong_pinned_bytes_rejected(self):
        data = b"CREATE TABLE `test` (`id` int) ENGINE=InnoDB;"
        row = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        verify(data, row, "test.sql")
        with self.assertRaisesRegex(ValueError, "differ"):
            verify(data + b"UPDATE `test` SET id=2;", row, "test.sql")

    def test_archive_and_gzip_output_are_reproducible(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_archive(root / "a.gz", {"b.sql": b"second", "a.sql": b"first"})
            write_archive(root / "b.gz", {"a.sql": b"first", "b.sql": b"second"})
            self.assertEqual((root / "a.gz").read_bytes(), (root / "b.gz").read_bytes())
            write_json(root / "x.gz", {"b": 2, "a": 1}, compressed=True)
            write_json(root / "y.gz", {"a": 1, "b": 2}, compressed=True)
            self.assertEqual((root / "x.gz").read_bytes(), (root / "y.gz").read_bytes())
            self.assertEqual(json.loads(gzip.decompress((root / "x.gz").read_bytes())), {"a": 1, "b": 2})

    def test_companion_report_publish_has_matching_hash_and_no_staging_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            companion, report = root / "source.json.gz", root / "report.json"
            write_companion_report(companion, {"rows": [2, 1]}, report, {"status": "blocked"})
            published = json.loads(report.read_text())
            self.assertEqual(published["sourceCompanionSha256"], hashlib.sha256(companion.read_bytes()).hexdigest())
            self.assertEqual(json.loads(gzip.decompress(companion.read_bytes())), {"rows": [2, 1]})
            self.assertFalse((root / "source.json.gz.tmp").exists())
            self.assertFalse((root / "report.json.tmp").exists())
            self.assertFalse((root / "report.json.pending").exists())

    def test_missing_input_and_wrong_revision_fail_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = {"commit": PINNED_COMMIT, "repository": REPOSITORY,
                        "tables": {"test.sql": {"archive": "absent.tar.gz"}}}
            source = Sources(manifest, Path(directory))
            with self.assertRaisesRegex(ValueError, "Missing required original archive"):
                source.read("test")
            with self.assertRaisesRegex(ValueError, "supported pinned revision"):
                Sources({**manifest, "commit": "0" * 40}, Path(directory))

    def test_duplicate_tar_members_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with tarfile.open(root / "test.tar.gz", "w:gz") as archive:
                for _ in range(2):
                    member = tarfile.TarInfo("test.sql")
                    member.size = 3
                    archive.addfile(member, io.BytesIO(b"bad"))
            manifest = {"commit": PINNED_COMMIT, "repository": REPOSITORY,
                        "tables": {"test.sql": {"archive": "test.tar.gz"}}}
            with self.assertRaisesRegex(ValueError, "Missing or invalid original table"):
                Sources(manifest, root).read("test")


if __name__ == "__main__":
    unittest.main(verbosity=2)
