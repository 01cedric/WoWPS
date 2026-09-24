#!/usr/bin/env python3
"""Add the creature definitions the SmartAI family summons to a compiled catalog.

import_world_catalog.py carries a definition for every spawned entry only; a
creature that enters the world through SMART_ACTION_SUMMON_CREATURE or a
SPELL_EFFECT_SUMMON of an installed script (2.38) may never be spawned by the
`creature` table. This tool reads the generator report's `summonEntries`
(tools/local_realm/generate_npc_spell_profiles.py) and appends the missing
definitions to npcs.pack with the same formulas the compiler uses
(import_world_catalog.npc_record): level and stats from creature_template and
creature_classlevelstats, the first model, immunity set, resistances, combat
reach. A summon carries no loot here and is marked `summonOnly`; it never
respawns, so its respawn delay is the compiler's cap. Entries already in the
catalog are left untouched; the manifest fingerprint is refreshed.
"""
from __future__ import annotations
import argparse, collections, json, tarfile, tempfile
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from import_azerothcore import sql_rows  # noqa: E402
from import_world_catalog import npc_record, NEUTRAL_FACTIONS, AGGRESSIVE_FACTIONS  # noqa: E402
from patch_reputation_catalog import read_pack, write_pack, refresh_manifest  # noqa: E402

TABLES = ['creature_template', 'creature_template_model', 'creature_model_info', 'creature_classlevelstats',
          'creature_immunities', 'creature_template_resistance']


def load_tables(archive: Path):
    tables = {}
    with tempfile.TemporaryDirectory() as directory, tarfile.open(archive) as tar:
        for table in TABLES:
            tar.extract(table + '.sql', directory, filter='data')
            tables[table] = list(sql_rows(Path(directory) / (table + '.sql'), table))
    return tables


def patch(catalog: Path, entries, tables):
    npcs = read_pack(catalog / 'npcs.pack')
    templates = {r['entry']: r for r in tables['creature_template']}
    stats = {(r['level'], r['class']): r for r in tables['creature_classlevelstats']}
    models = {}
    for r in sorted(tables['creature_template_model'], key=lambda r: r['idx']):
        if r['creaturedisplayid']:
            models.setdefault(r['creatureid'], r)
    model_info = {r['displayid']: r for r in tables['creature_model_info']}
    immunities = {r['id']: r for r in tables['creature_immunities']}
    resistances = collections.defaultdict(lambda: [0] * 6)
    for r in tables['creature_template_resistance']:
        if not 1 <= r['school'] <= 6:
            raise ValueError(f"creature_template_resistance school out of range: {r}")
        resistances[r['creatureid']][r['school'] - 1] = max(0, min(65535, r['resistance'] or 0))
    added, present, skipped = [], 0, []
    for e in sorted(entries):
        if e in npcs:
            present += 1
            continue
        t = templates.get(e)
        if t is None or e not in models or t['flags_extra'] & 128 or t['vehicleid']:
            skipped.append(e)
            continue
        hostile = t['faction'] in NEUTRAL_FACTIONS | AGGRESSIVE_FACTIONS and not t['npcflag']
        record = npc_record(e, t, stats, models, model_info, immunities, resistances, 300, hostile=hostile,
                            aggressive_faction=t['faction'] in AGGRESSIVE_FACTIONS, quest_giver=False, loot=[])
        record['summonOnly'] = True
        npcs[e] = record
        added.append(e)
    write_pack(catalog / 'npcs.pack', npcs)
    fingerprint = refresh_manifest(catalog)
    return {'added': added, 'alreadyPresent': present, 'skipped': skipped, 'fingerprint': fingerprint, 'npcs': len(npcs)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, default=HERE / 'world_source_sql.tar.gz')
    parser.add_argument('--report', type=Path, default=HERE.parent.parent / 'docs/NPC_SPELL_PROFILE_REPORT.json')
    parser.add_argument('--catalog', type=Path, default=HERE.parent.parent / 'assets/local_realm/catalog')
    parser.add_argument('--output-report', type=Path, default=HERE.parent.parent / 'docs/SUMMON_CATALOG_REPORT.json')
    args = parser.parse_args()
    entries = json.loads(args.report.read_text())['summonEntries']
    result = patch(args.catalog, entries, load_tables(args.archive))
    args.output_report.write_text(json.dumps(result, indent=1) + '\n')
    print(json.dumps({k: (len(v) if isinstance(v, list) else v) for k, v in result.items()}))


if __name__ == '__main__':
    main()
