#!/usr/bin/env python3
"""Compile bounded class base pools and direct item health/mana from source SQL."""
import argparse,hashlib,json
from pathlib import Path
from import_azerothcore import sql_rows,PINNED_COMMIT,REPOSITORY

def generate(class_sql,item_sql,out,report):
    rows=[]
    for r in sql_rows(class_sql,'player_class_stats'):
        if r['class'] not in (1,2,3,4,5,6,7,8,9,11) or not 1<=r['level']<=80:continue
        if not 0<r['basehp']<=1000000 or not 0<=r['basemana']<=1000000:raise ValueError('Invalid class pool')
        rows.append((r['class'],r['level'],r['basehp'],r['basemana']))
    rows.sort()
    if len(rows)!=746 or len({r[:2] for r in rows})!=746:raise ValueError('Missing/duplicate class pool rows')
    expected={(c,l) for c in (1,2,3,4,5,6,7,8,9,11) for l in range(55 if c==6 else 1,81)}
    if {r[:2] for r in rows}!=expected:raise ValueError('Unexpected class/level coverage')
    items=[]
    for r in sql_rows(item_sql,'item_template'):
        if r['class'] not in (2,4):continue
        hp=sum(r['stat_value'+str(i)] for i in range(1,11) if r['stat_type'+str(i)]==1)
        mana=sum(r['stat_value'+str(i)] for i in range(1,11) if r['stat_type'+str(i)]==0)
        if abs(hp)>1000000 or abs(mana)>1000000:raise ValueError('Invalid direct item pool')
        if hp or mana:items.append((r['entry'],hp,mana))
    out.mkdir(parents=True,exist_ok=True)
    for name,data in [('local_class_pools_generated.inc',rows),('local_resource_items_generated.inc',sorted(items))]:
        (out/name).write_text('// Generated from AzerothCore '+PINNED_COMMIT+'; see assets/local_realm/NOTICE.txt.\n'+''.join('{'+','.join(map(str,r))+'},\n' for r in data))
    report.write_text(json.dumps({'repository':REPOSITORY,'sqlCommit':PINNED_COMMIT,'classLevelRows':len(rows),'directItemPoolRows':len(items),'sourceHashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (class_sql,item_sql)},'scope':'Source metadata; full gameplay acceptance remains P35.'},indent=2)+'\n')
    print('Class pool rows:',len(rows),'direct item pool rows:',len(items))
if __name__=='__main__':
    p=argparse.ArgumentParser();[p.add_argument(k,type=Path) for k in ['class_sql','item_sql','output','report']];a=p.parse_args();generate(a.class_sql,a.item_sql,a.output,a.report)
