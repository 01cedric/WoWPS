#!/usr/bin/env python3
"""2.40: the gossip packs patch_gossip_catalog.py adds to the shipped catalog."""
import json
import shutil
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/local_realm"))
import patch_gossip_catalog as tool  # noqa: E402

CATALOG = ROOT / "assets/local_realm/catalog"
REPORT = ROOT / "docs/GOSSIP_CATALOG_REPORT.json"


def owners():
    data = (CATALOG / "gossip_owners.pack").read_bytes()
    magic, count, width = struct.unpack_from("<8sII", data, 0)
    assert magic == tool.OWNERS_MAGIC and width == 12 and len(data) == 16 + 12 * count
    return [struct.unpack_from("<III", data, 16 + 12 * i) for i in range(count)]


def parse_menu(blob):
    at = 0
    text_count, = struct.unpack_from("<H", blob, at); at += 2
    texts = []
    for _ in range(text_count):
        text_id, = struct.unpack_from("<I", blob, at); at += 4
        count, = struct.unpack_from("<B", blob, at); at += 1
        conditions = [struct.unpack_from("<BBBIII", blob, at + 15 * i) for i in range(count)]; at += 15 * count
        texts.append((text_id, conditions))
    option_count, = struct.unpack_from("<H", blob, at); at += 2
    options = []
    for _ in range(option_count):
        option_id, icon, kind, flag, action_menu, money, coded = struct.unpack_from("<HBBIIIB", blob, at); at += 17
        n, = struct.unpack_from("<H", blob, at); at += 2; text = blob[at:at + n].decode(); at += n
        n, = struct.unpack_from("<H", blob, at); at += 2; box = blob[at:at + n].decode(); at += n
        count, = struct.unpack_from("<B", blob, at); at += 1
        conditions = [struct.unpack_from("<BBBIII", blob, at + 15 * i) for i in range(count)]; at += 15 * count
        options.append((option_id, icon, kind, flag, action_menu, money, coded, text, box, conditions))
    assert at == len(blob)
    return texts, options


def parse_text(blob):
    at = 0
    count, = struct.unpack_from("<B", blob, at); at += 1
    variants = []
    for _ in range(count):
        probability, language = struct.unpack_from("<fB", blob, at); at += 5
        n, = struct.unpack_from("<H", blob, at); at += 2; male = blob[at:at + n].decode(); at += n
        n, = struct.unpack_from("<H", blob, at); at += 2; female = blob[at:at + n].decode(); at += n
        emotes = struct.unpack_from("<6H", blob, at); at += 12
        variants.append((probability, language, male, female, emotes))
    assert at == len(blob)
    return variants


class GossipCatalogTests(unittest.TestCase):
    def test_packs(self):
        report = json.loads(REPORT.read_text())
        rows = owners()
        self.assertEqual(len(rows), report["owners"])
        self.assertEqual([r[0] for r in rows], sorted({r[0] for r in rows}))
        self.assertTrue(all(r[1] or r[2] & 1 for r in rows))
        data, index = tool.keyed_pack_index(CATALOG / "gossip.pack")
        self.assertEqual(len(index), report["menus"])
        self.assertIn(0, index)  # the default per-npcflag options
        texts_seen, options_seen = 0, 0
        for menu, (offset, length) in index.items():
            texts, options = parse_menu(data[offset:offset + length])
            texts_seen += len(texts); options_seen += len(options)
            self.assertEqual([o[0] for o in options], sorted(o[0] for o in options))
            for o in options:
                self.assertIn(o[2], tool.OPTION_TYPES)
                self.assertEqual(o[6], 0)
                self.assertTrue(o[7])
            for _, conditions in texts + [(None, o[9]) for o in options]:
                for c in conditions:
                    self.assertIn(c[0], tool.CONDITION_LIST)
                    self.assertIn(c[2], (0, 1))
        self.assertEqual(texts_seen, report["menuTexts"])
        self.assertEqual(options_seen, report["options"])
        tdata, tindex = tool.keyed_pack_index(CATALOG / "gossip_texts.pack")
        self.assertEqual(len(tindex), report["texts"])
        variants = 0
        for text_id, (offset, length) in tindex.items():
            parsed = parse_text(tdata[offset:offset + length])
            self.assertTrue(parsed)
            variants += len(parsed)
            for p in parsed:
                self.assertGreaterEqual(p[0], 0)
                self.assertTrue(p[2] or p[3])
        self.assertEqual(variants, report["textVariants"])
        # Every text a menu names is carried; a menu an option chains to is
        # carried unless it is empty (menusEmpty: the page then shows the
        # default text and no options, as the reference's empty menu does).
        chained, absent, blank = 0, 0, set()
        for menu, (offset, length) in index.items():
            texts, options = parse_menu(data[offset:offset + length])
            for text_id, _ in texts:
                if text_id not in tindex:
                    blank.add(text_id)  # a text without words (textsEmpty): the page shows the greeting
            for o in options:
                if o[4]:
                    chained += 1
                    absent += o[4] not in index
        self.assertGreater(chained, 1000)
        self.assertLessEqual(absent, report["menusEmpty"])
        self.assertLessEqual(len(blank), report["textsEmpty"])
        # Ambassador Sunsorrow's menu (family test): three texts, one conditioned option.
        texts, options = parse_menu(data[index[7178][0]:index[7178][0] + index[7178][1]])
        self.assertEqual([t[0] for t in texts], [8458, 8740, 10378])
        self.assertEqual(len(options), 1)
        self.assertEqual(options[0][0], 0)
        self.assertEqual(sorted(c[0] for c in options[0][9]), [2, 8, 16])
        # Manifest: the three packs are listed with their sizes.
        manifest = json.loads((CATALOG / "manifest.json").read_text())
        for name in ("gossip.pack", "gossip_texts.pack", "gossip_owners.pack"):
            self.assertEqual(manifest["files"][name]["bytes"], (CATALOG / name).stat().st_size)
        self.assertTrue(manifest["fingerprint"])

    def test_reproduction(self):
        # The tool reproduces the checked-in packs byte for byte from the pinned archives.
        tables = tool.load_tables(ROOT / "tools/local_realm/world_source_sql.tar.gz", ROOT / "tools/local_realm/world_script_source_sql.tar.gz",
                                 ROOT / "tools/local_realm/gossip_source_sql.tar.gz", ROOT / "tools/local_realm/vendor_source_sql.tar.gz")
        with tempfile.TemporaryDirectory() as directory:
            copy = Path(directory) / "catalog"
            copy.mkdir()
            for name in ("spawns.pack", "manifest.json"):
                shutil.copy(CATALOG / name, copy / name)
            result = tool.patch(copy, tables, tool.spawned_entries_of(copy))
            for name in ("gossip.pack", "gossip_texts.pack", "gossip_owners.pack"):
                self.assertEqual((copy / name).read_bytes(), (CATALOG / name).read_bytes(), name)
            report = json.loads(REPORT.read_text())
            for key in ("owners", "menus", "options", "texts", "menuTexts", "textVariants"):
                self.assertEqual(result[key], report[key])


if __name__ == "__main__":
    unittest.main(verbosity=2)
