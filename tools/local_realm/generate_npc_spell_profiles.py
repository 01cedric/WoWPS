#!/usr/bin/env python3
"""Regenerate the bounded NPC spell family from pinned AC SQL and build12340 DBC.

SQL source is source input, never downloaded implicitly. Full entry scripts must
match; unsupported companion actions or attached conditions reject the entry.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from import_azerothcore import sql_rows

ENTRIES = {4008: 5401, 4323: 11985, 5858: 11985}
PIN = '9c416aaacb5537636abb13c80f55a88947838e33'
FUNCTIONAL = tuple(range(1, 131)) + tuple(range(204, 234))
SOURCE_HASHES = {
    "SmartScript.cpp": "ee6ff4eff8446099699bc4f3b77103741caf3b938a72e8757f429c07c8ba2e0b",
    "Spell.dbc": "d5cce1a83550dcfa9eb2f0251dbb11fd24c272534b2b1a9b230924a44d817ab3",
    "SpellCastTimes.dbc": "919ca9b65cb144a3a9cf0ce10d2a25fcc7cdccf33c752ed376e086ff62f8ccec",
    "SpellRange.dbc": "82d261be5e42d90f62a13642a3fd8f421fe1b0056ad8ed7dea73cdf4f8c8cb7f",
    "conditions.sql": "1a40806e454211118e0b459e09eb70e0e09807987ead6b321bbd371b93ffeb7d",
    "creature_classlevelstats.sql": "54f9a4ea68a8e02d6ea68362e036ed526089b3b026dc821273d4372b89c15c2d",
    "creature_template.sql": "0066e4fd35b809986dd325487515087f5a95f4ffe50ea03846c8d8bf5196870a",
    "smart_scripts.sql": "622b5865f171b0b7a4dd93ecba55afd673c393c579485c17f827b31cb6e47dd2",
    "spell_script_names.sql": "9cb7ccf86ae2542fb4dae77f49944dc47f2865080c9367a9f7402a04db4d4da1"
}

def dbc(path):
    data=path.read_bytes()
    magic,count,fields,size,strings=struct.unpack_from('<4s4I',data)
    assert magic==b'WDBC' and fields*4==size and len(data)==20+count*size+strings
    return {row[0]:row for row in (struct.unpack_from('<'+str(fields)+'I',data,20+i*size) for i in range(count))}

def profiles(sql):
    creatures={r['entry']:r for r in sql_rows(sql/'creature_template.sql','creature_template')}
    scripts={entry:[] for entry in ENTRIES}
    for row in sql_rows(sql/'smart_scripts.sql','smart_scripts'):
        if row['source_type']==0 and row['entryorguid'] in scripts:scripts[row['entryorguid']].append(row)
    conditions=list(sql_rows(sql/'conditions.sql','conditions'))
    result=[]
    for entry,spell in ENTRIES.items():
        npc=creatures[entry]
        assert npc['ainame']=='SmartAI' and not npc['scriptname'] and not npc['npcflag'] and not npc['rank']
        assert npc['unit_class']==1 and npc['exp']==0, f'{entry}: unreviewed creature scaling class/expansion'
        assert len(scripts[entry])==1, f'{entry}: incomplete whole-entry script'
        row=scripts[entry][0]
        required={'entryorguid':entry,'source_type':0,'id':0,'link':0,'event_type':0,
                  'event_phase_mask':0,'event_chance':100,'event_flags':0,'action_type':11,
                  'action_param1':spell,'target_type':2}
        for key,value in row.items():
            if key in ('comment','event_param1','event_param2','event_param3','event_param4'):continue
            assert value==required.get(key,0), f'{entry}: unsupported {key}={value}'
        assert 0<=row['event_param1']<=row['event_param2']<=60000
        assert 0<row['event_param3']<=row['event_param4']<=60000
        # CONDITION_SOURCE_TYPE_SMART_EVENT=22, SourceEntry is creature entry.
        assert not any(r['sourcetypeorreferenceid']==22 and abs(r['sourceentry'])==entry for r in conditions)
        # Spell source/target conditions and scripted spell callbacks are also
        # outside this family. Fail closed if the pinned source grows either.
        assert not any(r['sourcetypeorreferenceid'] in (13,17) and r['sourceentry']==spell for r in conditions)
        result.append((npc,row))
    names=list(sql_rows(sql/'spell_script_names.sql','spell_script_names'))
    assert not any(abs(r['spell_id']) in ENTRIES.values() for r in names)
    return result

def main():
    p=argparse.ArgumentParser();p.add_argument('sql',type=Path);p.add_argument('dbc',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    for name,expected in SOURCE_HASHES.items():
        path=(a.dbc if name.endswith('.dbc') else a.sql)/name
        assert hashlib.sha256(path.read_bytes()).hexdigest()==expected, f'Unexpected pinned input: {name}'
    rows=profiles(a.sql);spells=dbc(a.dbc/'Spell.dbc')
    lines=['// AzerothCore '+PIN+'; generated; do not hand-edit.']
    for npc,row in rows:
        values=[npc['entry'],row['action_param1']]+[row['event_param'+str(i)] for i in range(1,5)]
        lines.append('{'+','.join(str(v)+'u' for v in values)+'}, // '+npc['name'])
    a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'local_npc_spell_profiles_generated.inc').write_text('\n'.join(lines)+'\n')
    lines=['// Functional Spell.dbc columns; display names/icons are locale independent.']
    for col in FUNCTIONAL:lines.append('{'+str(col)+','+','.join(str(spells[spell][col])+'u' for spell in (5401,11985))+'},')
    (a.output/'local_npc_spell_columns_generated.inc').write_text('\n'.join(lines)+'\n')
    stats={r['level']:r for r in sql_rows(a.sql/'creature_classlevelstats.sql','creature_classlevelstats') if r['class']==1}
    lines=['// Creature class1 expansion0 BaseDamage; index is level; 0 is invalid.','0.f,']
    for level in range(1,84):
        value=float(stats[level]['damage_base']);assert value>0
        lines.append(repr(value)+'f, // '+str(level))
    (a.output/'local_npc_spell_scaling_generated.inc').write_text('\n'.join(lines)+'\n')
    evidence={'revision':PIN,'entries':[{'creature':n,'script':r} for n,r in rows],
              'inputs':{str(path.name):hashlib.sha256(path.read_bytes()).hexdigest() for path in
                  [a.sql/n for n in ('creature_template.sql','smart_scripts.sql','conditions.sql','spell_script_names.sql','SmartScript.cpp','creature_classlevelstats.sql')]+[a.dbc/'Spell.dbc',a.dbc/'SpellRange.dbc',a.dbc/'SpellCastTimes.dbc']}}
    print(json.dumps(evidence,indent=2))

if __name__=='__main__':main()
