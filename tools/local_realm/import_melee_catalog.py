#!/usr/bin/env python3
"""Build bounded melee metadata from attributed SQL and build-12340 game tables."""
import argparse,hashlib,json,struct,math
from pathlib import Path
from import_azerothcore import sql_rows,PINNED_COMMIT,REPOSITORY

def dbc(path,fields):
 b=path.read_bytes();magic,n,k,size,strings=struct.unpack_from('<4s4I',b)
 if magic!=b'WDBC' or k!=fields or size!=4*k or len(b)!=20+n*size+strings:raise ValueError(str(path))
 return [struct.unpack_from('<'+'f'*fields,b,20+i*size) for i in range(n)]
def emit(path,lines):path.write_text('// Generated from AzerothCore '+PINNED_COMMIT+'; see assets/local_realm/NOTICE.txt.\n'+'\n'.join(lines)+'\n')
def generate(sql,extra,dbcdir,out,report):
 items=[]
 for r in sql_rows(sql/'item_template.sql','item_template'):
  if r['class'] not in (2,4):continue
  stats={}
  for n in range(1,11):stats[r['stat_type'+str(n)]]=stats.get(r['stat_type'+str(n)],0)+r['stat_value'+str(n)]
  # Primary stats, AP, defense/dodge/parry/block/hit/crit/haste/expertise, shield block value.
  st=[stats.get(k,0) for k in (4,3,7,5,6,38,12,13,14,15,16,19,28,37,48)]
  st[10]+=stats.get(31,0);st[11]+=stats.get(32,0);st[12]+=stats.get(36,0)
  nums=[r['entry'],r['class'],r['subclass'],r['inventorytype'],r['delay'],r['block'],r['scalingstatdistribution'] or r['scalingstatvalue']]
  dmg=[float(r[k]) for k in ('dmg_min1','dmg_max1','dmg_min2','dmg_max2')]
  if any(not math.isfinite(x) or x<0 or x>1000000 for x in dmg) or any(abs(x)>1000000 for x in st):raise ValueError(r['entry'])
  items.append((r['entry'],'{'+','.join(map(str,nums))+',{'+','.join(format(x,'.9g')+'f' if any(c in format(x,'.9g') for c in '.eE') else format(x,'.9g')+'.0f' for x in dmg)+'},{'+str(r['dmg_type1'])+','+str(r['dmg_type2'])+'},{'+','.join(map(str,st))+'}},'))
 emit(out/'local_melee_items_generated.inc',[v for _,v in sorted(items)])
 lines=[];count=0
 for r in sorted(sql_rows(extra/'player_class_stats.sql','player_class_stats'),key=lambda r:(r['class'],r['level'])):
  if r['level']>80 or r['class'] not in (1,2,3,4,5,6,7,8,9,11):continue
  lines.append('{'+','.join(str(r[k]) for k in ['class','level'])+',{'+','.join(str(r[k]) for k in ['strength','agility','stamina','intellect','spirit'])+'}},');count+=1
 if count!=746:raise ValueError('Missing class/level records')
 emit(out/'local_melee_levels_generated.inc',lines)
 races=list(sql_rows(extra/'player_race_stats.sql','player_race_stats'))
 emit(out/'local_melee_races_generated.inc',['{'+str(r['race'])+',{'+','.join(str(r[k]) for k in ['strength','agility','stamina','intellect','spirit'])+'}},' for r in races])
 crit=dbc(dbcdir/'gtChanceToMeleeCrit.dbc',1);base=dbc(dbcdir/'gtChanceToMeleeCritBase.dbc',1);ratings=dbc(dbcdir/'gtCombatRatings.dbc',1)
 b=(dbcdir/'gtOCTClassCombatRatingScalar.dbc').read_bytes();n=struct.unpack_from('<I',b,4)[0];scalars=dict(struct.unpack_from('<If',b,20+i*8) for i in range(n))
 lines=[]
 for cls in [1,2,3,4,5,6,7,8,9,11]:
  for level in range(1,81):
   vals=[base[cls-1][0],crit[(cls-1)*100+level-1][0]]+[scalars[(cls-1)*32+cr+1]/ratings[cr*100+level-1][0] for cr in [1,2,3,4,5,8,17,23]]
   if any(not math.isfinite(x) for x in vals):raise ValueError('Nonfinite combat ratio')
   lines.append('{'+str(cls)+','+str(level)+',{'+','.join(format(v,'.10g')+'f' if any(c in format(v,'.10g') for c in '.eE') else format(v,'.10g')+'.0f' for v in vals)+'}},')
 emit(out/'local_melee_ratios_generated.inc',lines)
 npcs=list(sql_rows(sql/'creature_template.sql','creature_template'))
 emit(out/'local_melee_npcs_generated.inc',['{'+str(r['entry'])+','+str(r['flags_extra'])+'u,'+str(r['type'])+','+str(r['rank'])+'},' for r in sorted(npcs,key=lambda r:r['entry'])])
 paths=[sql/'item_template.sql',sql/'creature_template.sql',extra/'player_class_stats.sql',extra/'player_race_stats.sql']+[dbcdir/(x+'.dbc') for x in ['gtChanceToMeleeCrit','gtChanceToMeleeCritBase','gtCombatRatings','gtOCTClassCombatRatingScalar']]
 report.write_text(json.dumps({'repository':REPOSITORY,'sqlCommit':PINNED_COMMIT,'items':len(items),'classLevelRecords':count,'raceRecords':len(races),'npcMetadataRecords':len(npcs),'ratingRecords':800,'sourceHashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'scope':'Source metadata, not gameplay acceptance'},indent=2)+'\n');print('Melee item records:',len(items),'class levels:',count)
if __name__=='__main__':
 p=argparse.ArgumentParser();[p.add_argument(x,type=Path) for x in ['sql','extra','dbc','output','report']];a=p.parse_args();generate(a.sql,a.extra,a.dbc,a.output,a.report)
