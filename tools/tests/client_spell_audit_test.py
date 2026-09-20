#!/usr/bin/env python3
"""Exercise the standalone audit with synthetic, non-game DBC records."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile


def dbc(path, fields, rows):
    assert all(len(row) == fields for row in rows)
    data = struct.pack('<4sIIII', b'WDBC', len(rows), fields, fields * 4, 1)
    data += b''.join(struct.pack('<' + 'I' * fields, *row) for row in rows)
    path.write_bytes(data + b'\0')


def main(binary):
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        output = root / 'audit.json'
        def run(expected):
            result = subprocess.run([binary, str(root), str(output)], capture_output=True, text=True)
            assert result.returncode == expected, result.stderr + result.stdout
            return json.loads(output.read_text())
        missing = run(3)
        assert not missing['inputComplete'] and len(missing['problems']) == 10
        assert missing['sourceSpells'] is None and missing['importAudit'] is None
        print('PASS missing inputs produce unknown coverage, not zero support')

        spell = [0] * 234
        spell[0] = 78
        spell[28] = spell[46] = 1
        spell[68] = 0xffffffff
        spell[71], spell[80], spell[86] = 2, 9, 6
        blocked = spell.copy()
        blocked[0], blocked[71] = 90001, 3
        passive = spell.copy()
        passive[0], passive[4], passive[71], passive[86], passive[95] = 90002, 64, 6, 1, 34
        dbc(root / 'Spell.dbc', 234, [spell, blocked, passive])
        distance = [0] * 40
        distance[0] = 1
        distance[3] = distance[4] = struct.unpack('<I', struct.pack('<f', 30))[0]
        dbc(root / 'SpellRange.dbc', 40, [distance])
        dbc(root / 'SpellCastTimes.dbc', 4, [[1,0,0,0]])
        dbc(root / 'SpellDuration.dbc', 4, [[1,1000,0,0]])
        dbc(root / 'SpellIcon.dbc', 2, [[1,0]])
        dbc(root / 'SpellRuneCost.dbc', 5, [[1,0,0,0,0]])
        ability = [0] * 14
        ability[0:3] = [1,1,90001]
        ability[4] = 1
        dbc(root / 'SkillLineAbility.dbc', 14, [ability])
        line = [0] * 38
        line[0:2] = [1,7]
        dbc(root / 'SkillLine.dbc', 38, [line])
        talent = [0] * 23
        talent[0:2], talent[4] = [1,1], 90002
        dbc(root / 'Talent.dbc', 23, [talent])
        tab = [0] * 24
        tab[0], tab[20] = 1,1
        dbc(root / 'TalentTab.dbc', 24, [tab])
        result = run(0)
        assert result['inputComplete'] and len(result['sourceSpells']) == 3
        audit = {(r['spellId'], r['talent']): r for r in result['importAudit']}
        assert audit[78,False]['firstResult'] == 'Supported decoder; imported'
        assert audit[90001,False]['firstResult'] == 'Unsupported effect 3'
        assert audit[90002,True]['firstResult'] == 'Supported decoder; imported'
        assert result['sourceTalentRanks'] == [{'talentId':1,'tabId':1,'rank':1,'spellId':90002}]
        assert next(r for r in result['retainedDefinitions'] if r['spellId']==90002)['decoderAccepted']
        print('PASS production importer acceptance, rejection, passive talent and source identities')
        dbc(root / 'SpellRange.dbc', 4, [[1,0,0,0]])
        invalid = run(3)
        assert not invalid['inputComplete'] and invalid['sourceSpells'] is None
        assert 'SpellRange.dbc incompatible' in invalid['problems']
        print('PASS incompatible input does not produce misleading coverage')


if __name__ == '__main__':
    main(str(Path(sys.argv[1]).resolve()))
