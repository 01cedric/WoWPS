#!/usr/bin/env python3
"""Record real-source regeneration decoder coverage and current talent-tree reachability.

This is a metadata diagnostic, not gameplay acceptance. Requires the production
importer's audit JSON plus the pinned raw Spell/Talent/TalentTab tables.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def records(path):
    data = path.read_bytes()
    magic, count, fields, stride, strings = struct.unpack_from('<4sIIII', data)
    if magic != b'WDBC' or stride != fields * 4 or len(data) != 20 + count * stride + strings:
        raise ValueError(f'Invalid DBC {path.name}')
    rows = [struct.unpack_from('<' + 'I' * fields, data, 20 + i * stride) for i in range(count)]
    return {r[0]: r for r in rows}, data[20 + count * stride:]


def audit_regeneration(audit_path, dbc_dir, output):
    audit = json.loads(audit_path.read_text())
    if not audit.get('inputComplete'):
        raise ValueError('Complete production importer audit required')
    raw, strings = records(dbc_dir / 'Spell.dbc')
    defs = {d['spellId']: d for d in audit['retainedDefinitions']}
    candidates = [d for d in defs.values() if d['talentId'] and
                  any(raw[d['spellId']][95 + e] in (134, 219) for e in range(3))]
    trees = {}
    for tab in sorted({d['talentTab'] for d in candidates}):
        # With these monotone rank/tier/prerequisite gates and no exclusion
        # branches, saturating a tree below the 71-point cap gives its closure.
        supported = sorted((d for d in defs.values() if d['talentId'] and
                            d['talentTab'] == tab and d['decoderAccepted']),
                           key=lambda d: (d['talentRow'], d['talentId'], d['talentRank']))
        learned, path, points = {}, [], 0
        while points < 71:
            changed = False
            for d in supported:
                if points == 71:
                    break
                if learned.get(d['talentId'], 0) + 1 != d['talentRank'] or points < d['talentRow'] * 5:
                    continue
                if any(req and learned.get(req, 0) < rank + 1 for req, rank in
                       zip(d['talentPrerequisites'], d['talentPrerequisiteRanks'])):
                    continue
                learned[d['talentId']] = d['talentRank']
                path.append(d['spellId'])
                points += 1
                changed = True
            if not changed:
                break
        trees[tab] = {'tabId': tab, 'reachablePoints': points,
                      'learnedRanks': learned, 'spellRankLearningOrder': path}
    profiles = []
    for d in sorted(candidates, key=lambda d: d['spellId']):
        row = raw[d['spellId']]
        tree = trees[d['talentTab']]
        profiles.append({'spellId': d['spellId'],
                         'name': strings[row[136]:].split(b'\0', 1)[0].decode('utf8'),
                         'talentId': d['talentId'], 'rank': d['talentRank'],
                         'tabId': d['talentTab'], 'requiredTreePoints': d['talentRow'] * 5,
                         'decoderAccepted': d['decoderAccepted'], 'decoderRejection': d['firstRejection'],
                         'reachableWithCurrentDecoder': tree['learnedRanks'].get(d['talentId'], 0) >= d['talentRank'],
                         'spiritPercentWhileInterrupted': d['passiveManaRegenInterruptPct'],
                         'statPercentAsMP5': d['passiveManaRegenStatPct'],
                         'nonzeroSourceFields': {str(i): value for i, value in enumerate(row) if value},
                         'gameplayVerified': False})
    result = {'schemaVersion': 1, 'scope': 'Source/decoder/progression metadata only; P35 gameplay pending',
              'dbcRepository': 'https://github.com/Elwynsiaa/data',
              'dbcCommit': 'cefe45546e001f7745022479912dd140861a3647',
              'referenceRepository': 'https://github.com/azerothcore/azerothcore-wotlk',
              'referenceCommit': '4e80596cdaa21fa31830522f6f2d7ed8750bfffd',
              'referencePath': 'src/server/game/Entities/Unit/StatSystem.cpp',
              'sourceHashes': {name: hashlib.sha256((dbc_dir / name).read_bytes()).hexdigest()
                               for name in ['Spell.dbc', 'Talent.dbc', 'TalentTab.dbc']},
              'acceptedRankCount': sum(d['decoderAccepted'] for d in profiles),
              'reachableAcceptedRankCount': sum(d['decoderAccepted'] and d['reachableWithCurrentDecoder'] for d in profiles),
              'profiles': profiles, 'trees': list(trees.values()),
              'limitations': ['Reachability assumes level 80, 71 points and an initially empty tree.',
                              'Current production tier, prerequisite and sequential rank rules are preserved.',
                              'No PS4, LAN, UI, save round-trip or regeneration gameplay acceptance performed.']}
    output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ['acceptedRankCount', 'reachableAcceptedRankCount']}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('audit', type=Path)
    parser.add_argument('dbc_dir', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    audit_regeneration(args.audit, args.dbc_dir, args.output)
