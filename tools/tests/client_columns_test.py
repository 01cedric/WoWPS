#!/usr/bin/env python3
"""Check the importer against independently indexed provided WDBC bytes.

Takes an audit JSON produced by the production importer and the DBC directory.
No original game data is bundled with this test.
"""
import json
from pathlib import Path
import struct
import sys

audit=json.loads(Path(sys.argv[1]).read_text())
data=(Path(sys.argv[2])/'Spell.dbc').read_bytes()
magic,count,fields,stride,strings=struct.unpack_from('<4sIIII',data)
assert magic==b'WDBC' and fields==234 and stride==936
assert len(data)==20+count*stride+strings and audit['inputComplete']
rows={}
for i in range(count):
    row=struct.unpack_from('<234I',data,20+i*stride)
    assert row[0] not in rows
    rows[row[0]]=row
assert len(rows)==len(audit['sourceSpells'])
for item in audit['sourceSpells']:
    row=rows[item['spellId']]
    assert item['interruptFlags']==row[31]
    assert item['procFlags']==row[34] and item['procChance']==row[35]
    assert item['procCharges']==row[36] and item['stackLimit']==row[49]
print('PASS every supplied Spell.dbc row uses distinct interrupt/proc/chance/charge/stack columns')
retained={r['spellId']:r for r in audit['retainedDefinitions']}
accepted={r['spellId'] for r in audit['importAudit'] if r['firstResult']=='Supported decoder; imported'}
assert accepted
for spell in accepted:
    row=rows[spell]
    assert not row[34] and not row[36], spell
    assert retained[spell]['maxAuraStacks']==max(1,row[49]),spell
assert any(rows[spell][31] and rows[spell][35]==101 and not rows[spell][49] for spell in accepted)
assert 133 in accepted # Fireball's direct and periodic effects no longer reject casting interrupt flags.
assert retained[133]['maxAuraStacks']==1
assert 324 not in accepted and rows[324][34] and rows[324][36] # Lightning Shield still needs proc execution.
print('PASS normal casting flags accepted, genuine procs/charges rejected and chance 101 never becomes 101 stacks')
