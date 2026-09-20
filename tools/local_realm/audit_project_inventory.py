#!/usr/bin/env python3
"""Reproduce the shipped catalog and enumerate its source/runtime coverage.

This developer tool writes only to --output and temporary directories.
It does not change the runtime catalog or infer full gameplay support.
"""
import argparse
import collections
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import tarfile
import tempfile

from import_world_catalog import ALL_TABLES, compile_catalog
from import_azerothcore import sql_rows


def read_records(path):
    data = path.read_bytes()
    magic, count, stride = struct.unpack_from('<8sII', data)
    if magic != b'WPCAT01\0' or stride != 16 or 16 + count * 16 > len(data):
        raise ValueError(f'Invalid record pack: {path}')
    records = {}
    for index in range(count):
        key, offset, size = struct.unpack_from('<IQI', data, 16 + index * 16)
        if key in records or offset < 16 + count * 16 or offset + size > len(data):
            raise ValueError(f'Invalid record index: {path}')
        records[key] = json.loads(data[offset:offset + size])
    return records


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + '\n')


def audit(root, output):
    output.mkdir(parents=True, exist_ok=True)
    shipped = root / 'assets/local_realm/catalog'
    with tempfile.TemporaryDirectory(prefix='wowps-inventory-') as temp:
        temp = Path(temp)
        sql = temp / 'sql'
        sql.mkdir()
        archive = root / 'tools/local_realm/world_source_sql.tar.gz'
        with tarfile.open(archive) as source:
            for table in ALL_TABLES:
                member = source.getmember(table + '.sql')
                if not member.isfile():
                    raise ValueError('SQL source must be a regular file')
                (sql / member.name).write_bytes(source.extractfile(member).read())
        with contextlib.redirect_stdout(io.StringIO()):
            _, report = compile_catalog(sql, temp / 'regenerated', root / 'assets/local_realm/world.json')
        manifest = json.loads((shipped / 'manifest.json').read_text())
        comparisons = {}
        for name in [*manifest['files'], 'manifest.json']:
            if Path(name).name != name:
                raise ValueError('Invalid manifest filename')
            actual = (shipped / name).read_bytes()
            expected = (temp / 'regenerated' / name).read_bytes()
            comparisons[name] = {'identical': actual == expected,
                'sha256': hashlib.sha256(actual).hexdigest()}
            if actual != expected:
                raise ValueError(f'Regenerated catalog differs: {name}; update the baseline assessment')
        source_quests = {q['id']: q for q in sql_rows(sql / 'quest_template.sql', 'quest_template')}
        quests = read_records(shipped / 'quests.pack')
        rejected = {q['id']: q['reason'] for q in report['excludedQuests']}
        quest_rows = []
        for qid in sorted(set(source_quests) | set(quests)):
            runtime = quests.get(qid)
            row = {'questId': qid, 'title': source_quests.get(qid, {}).get('logtitle',
                   (runtime or {}).get('title', '')), 'inSourceSnapshot': qid in source_quests,
                   'inRuntimeCatalog': runtime is not None,
                   'status': 'adapted_catalog_entry' if runtime else 'excluded',
                   'firstImportExclusion': rejected.get(qid), 'gameplayVerified': False}
            if runtime is not None:
                row['objectiveTypes'] = sorted({o['type'] for o in runtime['objectives']})
                row['prerequisite'] = runtime.get('prerequisite', 0)
            elif qid not in rejected:
                raise ValueError(f'Unclassified source quest: {qid}')
            quest_rows.append(row)
        # Baseline overlays can restore initially rejected IDs. Count final status,
        # while retaining the original first exclusion as provenance.
        exclusions = collections.Counter(r['firstImportExclusion'] for r in quest_rows if not r['inRuntimeCatalog'])
        source_instances = list(sql_rows(sql / 'instance_template.sql', 'instance_template'))
        instance_rows = [{'mapId': r['map'], 'sourceScript': r['script'],
            'scriptExecutionStatus': 'not_implemented_in_local_realm',
            'encounterCoverage': None} for r in sorted(source_instances, key=lambda r:r['map'])]

    statuses = []
    section = ''
    readme = (root / 'README.md').read_text()
    for number, line in enumerate(readme.splitlines(), 1):
        if line.startswith('### '):
            section = line[4:]
        match = re.match(r'^\| (✅|⚠️|❌) \| (.*?) \| (.*?) \|$', line)
        if match and section:
            statuses.append({'section': section, 'status': match[1], 'feature': match[2],
                'scope': match[3], 'evidence': f'README.md:{number}',
                'evidenceKind': 'documented_baseline_not_new_runtime_test'})
    spell_source = root / 'include/game/local_spell_import.hpp'
    blockers = [{'sourceLine': number, 'reasonExpression': line.strip()}
        for number, line in enumerate(spell_source.read_text().splitlines(), 1)
        if 'unavailable(' in line or 'unsupportedReason=' in line or 'unsupportedReason =' in line]
    summary = {'schemaVersion': 1, 'buildVersion': (root / 'BUILD_VERSION').read_text().strip(),
        'sourceCommit': manifest['sourceCommit'], 'catalogReproduction': comparisons,
        'catalogCounts': report['coverage'], 'sourceQuestCount': len(source_quests),
        'finalExcludedQuestCount': sum(exclusions.values()), 'questExclusions': dict(exclusions),
        'readmeStatusCounts': dict(collections.Counter(r['status'] for r in statuses)),
        'installedClientSpellCoverage': None, 'installedClientTalentCoverage': None,
        'overallCompletionPercent': None,
        'limitations': ['Quest count denominator is this pinned SQL snapshot, not all retail content.',
            'Adapted catalog entry is not original quest/script parity or new gameplay verification.',
            'Exclusion reasons are the first importer rejection, not an exhaustive list of blockers.',
            'This project-only stage does not consume DBC tables; consult the companion client inventory.',
            'No console or connected-server test was performed by this inventory tool.',
            'Source inventory completion is tracked separately from full gameplay support.']}
    write_json(output / 'inventory.json', summary)
    write_json(output / 'quests.json', quest_rows)
    write_json(output / 'instances.json', instance_rows)
    write_json(output / 'systems.json', statuses)
    write_json(output / 'spell_import_blockers.json', blockers)
    text = ['# WoWPS implementation inventory', '', f"Baseline: {summary['buildVersion']}.", '',
        '**Status: source/catalog audit completed; client-table coverage is recorded in the companion client inventory.**', '',
        f"Catalog recreated byte-for-byte: {len(comparisons)} files.",
        f"Quest source records: {len(source_quests)}; adapted runtime entries: {len(quests)}; "
        f"final excluded source records: {sum(exclusions.values())}.",
        f"Instance templates: {len(instance_rows)}. Their original encounter scripts are not executed locally.", '',
        '## Quest exclusions', '', '| First blocking reason | Final excluded entries |', '|---|---:|']
    text += [f'| {key} | {value} |' for key, value in sorted(exclusions.items(), key=lambda v:(-v[1],v[0]))]
    text += ['', '## Evidence and remaining inputs', ''] + ['- ' + x for x in summary['limitations']]
    text += ['', 'See quests.json for every source/runtime quest ID, instances.json for every instance template,',
        'systems.json for the documented feature baseline, and spell_import_blockers.json for source locations.',
        'Use tools/local_realm/run_client_spell_audit.sh with your extracted 3.3.5a DBC tables to populate',
        'the missing spell and talent inventory. Decoder acceptance is only one layer of completion.', '']
    (output / 'INVENTORY.md').write_text('\n'.join(text))
    print(json.dumps({'quests': len(quest_rows), 'adapted': len(quests),
        'excluded': sum(exclusions.values()), 'instances': len(instance_rows),
        'catalogFilesIdentical': len(comparisons), 'spellCoverage': None}, sort_keys=True))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    audit(args.root, args.output)
