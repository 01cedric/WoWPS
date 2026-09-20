#!/usr/bin/env python3
"""Generate bounded spell-critical metadata from pinned primary source tables.

Raw DBC/SQL inputs remain outside the clean deliverable. This generator emits
class/level coefficients and equipped-item spell critical rating only.
"""
import argparse, hashlib, json, math, struct
from pathlib import Path
from import_azerothcore import sql_rows
DBC_REV='cefe45546e001f7745022479912dd140861a3647'
SQL_REV='9c416aaacb5537636abb13c80f55a88947838e33'

def table(path, fields, fmt):
    b=path.read_bytes()
    magic,n,k,size,strings=struct.unpack_from('<4s4I',b)
    if magic!=b'WDBC' or k!=fields or size!=4*k or len(b)!=20+n*size+strings:
        raise ValueError('Invalid DBC: '+str(path))
    return [struct.unpack_from('<'+fmt,b,20+i*size) for i in range(n)]

def ff(x):
    if not math.isfinite(x):raise ValueError('Nonfinite coefficient')
    s=format(x,'.10g');return s+('f' if any(c in s for c in '.eE') else '.0f')

def generate(source, output, report):
    names=['gtChanceToSpellCritBase.dbc','gtChanceToSpellCrit.dbc','gtCombatRatings.dbc','gtOCTClassCombatRatingScalar.dbc','item_template.sql']
    base=table(source/names[0],1,'f');crit=table(source/names[1],1,'f');ratings=table(source/names[2],1,'f');scalars=dict(table(source/names[3],2,'If'))
    lines=[]
    for cls in (1,2,3,4,5,6,7,8,9,11):
        for level in range(1,81):
            vals=[base[cls-1][0],crit[(cls-1)*100+level-1][0],scalars[(cls-1)*32+11]/ratings[10*100+level-1][0],scalars[(cls-1)*32+10]/ratings[9*100+level-1][0]]
            if vals[1]<0 or vals[2]<=0 or vals[3]<=0:raise ValueError('Invalid critical ratio')
            lines.append('{'+','.join(map(str,(cls,level)))+','+','.join(map(ff,vals))+'},')
    prefix='// Generated from WotLK build12340 DBC '+DBC_REV+' and AzerothCore '+SQL_REV+'.\n'
    (output/'local_spell_crit_ratios_generated.inc').write_text(prefix+'\n'.join(lines)+'\n')
    items=[]
    for r in sql_rows(source/'item_template.sql','item_template'):
        if r['class'] not in (2,4):continue
        rating=sum(r['stat_value'+str(i)] for i in range(1,11) if r['stat_type'+str(i)] in (21,32))
        ranged=sum(r['stat_value'+str(i)] for i in range(1,11) if r['stat_type'+str(i)] in (20,32))
        if abs(rating)>1000000 or abs(ranged)>1000000:raise ValueError('Invalid item critical rating')
        if rating or ranged:items.append((r['entry'],rating,ranged))
    (output/'local_spell_crit_items_generated.inc').write_text(prefix+'\n'.join('{%d,%d,%d},'%r for r in sorted(items))+'\n')
    info={'schemaVersion':1,'dbcRepository':'https://github.com/Elwynsiaa/data','dbcCommit':DBC_REV,'sqlRepository':'https://github.com/azerothcore/azerothcore-wotlk','sqlCommit':SQL_REV,'classLevelRows':len(lines),'itemRows':len(items),'formula':'100*(classBase+currentIntellect*classLevelRatio)+spellRating*classRatingScalar/levelRating','ratingIndex':10,'rangedRatingIndex':9,'itemStatTypes':[21,32],'rangedItemStatTypes':[20,32],'sourceHashes':{n:hashlib.sha256((source/n).read_bytes()).hexdigest() for n in names},'scope':'Generated metadata, not gameplay/console acceptance'}
    report.write_text(json.dumps(info,indent=2)+'\n');print(json.dumps(info))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('report',type=Path);a=p.parse_args();generate(a.source,a.output,a.report)
