#!/usr/bin/env python3
"""Join importer observations to all provided spell, skill and talent IDs.

No overall completion percentage is calculated. Unselected records remain
explicitly unassessed. This does not verify gameplay or repository authenticity.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct

CLASSES={1:'Warrior',2:'Paladin',3:'Hunter',4:'Rogue',5:'Priest',6:'Death Knight',
         7:'Shaman',8:'Mage',9:'Warlock',11:'Druid'}


def table(path):
    data=path.read_bytes()
    magic,count,fields,stride,strings=struct.unpack_from('<4sIIII',data)
    if magic!=b'WDBC' or stride!=fields*4 or len(data)!=20+count*stride+strings:
        raise ValueError(f'Incompatible DBC: {path.name}')
    return [struct.unpack_from('<'+'I'*fields,data,20+i*stride) for i in range(count)]


def summarize(audit_path,dbc_dir,output,source_url,commit):
    audit=json.loads(audit_path.read_text())
    if not audit['inputComplete']:raise ValueError('Complete compatible input required')
    output.mkdir(parents=True,exist_ok=True)
    spells={r['spellId']:r for r in audit['sourceSpells']}
    if len(spells)!=len(audit['sourceSpells']):raise ValueError('Duplicate source spell IDs')
    retained={r['spellId']:r for r in audit['retainedDefinitions']}
    observations=collections.defaultdict(list)
    for row in audit['importAudit']:observations[row['spellId']].append(row)
    tabs={row[0]:row for row in table(dbc_dir/'TalentTab.dbc')}
    skills={row[0]:row[1] for row in table(dbc_dir/'SkillLine.dbc')}
    talent_ids={r['spellId'] for r in audit['sourceTalentRanks']}
    candidates=collections.defaultdict(set)
    skill_rows=[]
    for row in table(dbc_dir/'SkillLineAbility.dbc'):
        if skills.get(row[1])!=7:continue
        skill_rows.append({'abilityRowId':row[0],'skillId':row[1],'spellId':row[2],
            'classMask':row[4],'sourceSpellPresent':row[2] in spells,
            'importerObserved':row[2] in observations})
        if row[2] not in talent_ids:
            for cls in CLASSES:
                if row[4] & (1<<(cls-1)):candidates[cls].add(row[2])
    # Starter abilities outside skill-line selection still belong in the inventory.
    for row in audit['importAudit']:
        if not row['talent']:
            for cls in CLASSES:
                if row['classMask'] & (1<<(cls-1)):candidates[cls].add(row['spellId'])
    talents=[]
    for row in audit['sourceTalentRanks']:
        tab=tabs.get(row['tabId']);mask=tab[20] if tab else 0
        found=next((r for r in observations[row['spellId']] if r['talent']),None)
        talents.append({**row,'classMask':mask,'sourceSpellPresent':row['spellId'] in spells,
            'status':found['firstResult'] if found else ('pet_or_unassigned_tab' if not mask else 'not_selected_by_importer'),
            'gameplayVerified':False})
    by_class=[]
    for cls,name in CLASSES.items():
        ids=candidates[cls];talent_rows=[r for r in talents if r['classMask']&(1<<(cls-1))]
        accepted={i for i in ids if retained.get(i,{}).get('decoderAccepted',False) and not retained[i]['talentId'] and not retained[i].get('triggeredOnly', False) and not retained[i].get('npcOnly', False)}
        by_class.append({'classId':cls,'class':name,'nonTalentCandidateIds':len(ids),
            'nonTalentDecoderAccepted':len(accepted),'talentRankReferences':len(talent_rows),
            'talentRanksDecoderAccepted':sum(r['status']=='Supported decoder; imported' for r in talent_rows),
            'nonTalentSpellIds':sorted(ids),'acceptedNonTalentSpellIds':sorted(accepted)})
    spell_rows=[{'spellId':i,'status':'importer_observed' if observations[i] else 'not_selected_by_importer',
        'observations':observations[i],'gameplayVerified':False} for i in sorted(spells)]
    source_files={x['name']+'.dbc' for x in audit['tables']}
    source_hashes={name:hashlib.sha256((dbc_dir/name).read_bytes()).hexdigest() for name in sorted(source_files)}
    summary={'schemaVersion':1,'source':{'url':source_url,'commit':commit,'sha256':source_hashes},
        'sourceSpellRows':len(spells),'sourceTalentRankReferences':len(talents),
        'npcOnlyDefinitions':sum(r.get('npcOnly',False) and r['decoderAccepted'] for r in retained.values()),
        'importAuditRows':len(audit['importAudit']),
        'acceptedAuditedSpellIds':len({r['spellId'] for r in audit['importAudit'] if r['firstResult']=='Supported decoder; imported'}),
        'classes':by_class,'scope':'Provided DBC snapshot, current importer acceptance; not full class support',
        'remainingVerification':['Player installation may differ from this repository snapshot.',
            'Missing gameplay mechanics remain missing despite complete inventory.',
            'Unselected spell and talent IDs remain unassessed; source-only rows are not counted as implemented.',
            'No original DBC data is included in these inventory outputs.'],
        'completeInventory':True,'fullGameplaySupport':False}
    for name,value in [('client_summary',summary),('client_spells',spell_rows),('client_talents',talents),('client_class_skill_rows',skill_rows)]:
        (output/(name+'.json')).write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
    lines=['# Client spell and talent inventory','',f'Source: {source_url}',f'Pinned commit: `{commit}`.','',
        f"{len(spells)} spell rows; {len(talents)} talent-rank references; {summary['acceptedAuditedSpellIds']} distinct audited spell IDs accepted by the decoder.",'',
        'Counts below describe this importer and dataset, not class completion percentages.',
        'Talent ranks are counted separately from non-talent candidates. Pet/unassigned talent tabs are retained in client_talents.json.','',
        '| Class | Non-talent candidates | Decoder accepts | Talent-rank references | Decoder accepts |',
        '|---|---:|---:|---:|---:|']
    lines += [f"| {r['class']} | {r['nonTalentCandidateIds']} | {r['nonTalentDecoderAccepted']} | {r['talentRankReferences']} | {r['talentRanksDecoderAccepted']} |" for r in by_class]
    lines += ['','A candidate is a masked class-skill spell excluding talent ranks, or a starter explicitly audited by the importer.',
        'The complete raw source-ID list additionally preserves spells outside that candidate set.',
        'Source compatibility and hashes establish reproducibility; they do not authenticate the files as an unmodified official installation.','']
    (output/'CLIENT_INVENTORY.md').write_text('\n'.join(lines))
    print(json.dumps({k:summary[k] for k in ['sourceSpellRows','sourceTalentRankReferences','acceptedAuditedSpellIds']}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--audit',type=Path,required=True)
    parser.add_argument('--dbc-dir',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--source-url',required=True)
    parser.add_argument('--commit',required=True)
    args=parser.parse_args()
    summarize(args.audit,args.dbc_dir,args.output,args.source_url,args.commit)
