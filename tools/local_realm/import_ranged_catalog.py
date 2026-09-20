#!/usr/bin/env python3
"""Emit reviewed ranged rating, item AP and projectile metadata; no raw assets."""
import argparse,hashlib,json,math,struct
from pathlib import Path
from import_azerothcore import sql_rows
REV='9c416aaacb5537636abb13c80f55a88947838e33'
DBC_REV='cefe45546e001f7745022479912dd140861a3647'
def ff(v):
 if not math.isfinite(v):raise ValueError('Nonfinite ranged coefficient')
 s=format(v,'.10g');return s+('f' if any(x in s for x in '.eE') else '.0f')
def dbc(path,fmt):
 b=path.read_bytes();magic,n,k,size,strings=struct.unpack_from('<4s4I',b)
 if magic!=b'WDBC' or size!=4*k or k!=len(fmt) or len(b)!=20+n*size+strings:raise ValueError(path)
 return [struct.unpack_from('<'+fmt,b,20+i*size) for i in range(n)]
def main():
 p=argparse.ArgumentParser();p.add_argument('sql',type=Path);p.add_argument('dbc',type=Path);p.add_argument('out',type=Path);p.add_argument('report',type=Path);a=p.parse_args()
 prefix='// Generated from AzerothCore '+REV+' / DBC '+DBC_REV+'.\n'
 items=[];ammo=[]
 for r in sql_rows(a.sql,'item_template'):
  if r['class']==6 and r['subclass'] in (2,3):
   ammo.append((r['entry'],r['subclass'],(float(r['dmg_min1'])+float(r['dmg_max1']))/2,r['requiredlevel']))
  if r['class'] not in (2,4):continue
  stats={}
  for i in range(1,11):
   k=r['stat_type'+str(i)];stats[k]=stats.get(k,0)+r['stat_value'+str(i)]
  values=(stats.get(38,0)+stats.get(39,0),stats.get(17,0)+stats.get(31,0),stats.get(29,0)+stats.get(36,0),stats.get(18,0)+stats.get(31,0))
  if any(abs(x)>1000000 for x in values):raise ValueError(r['entry'])
  if any(values):items.append((r['entry'],*values))
 (a.out/'local_ranged_items_generated.inc').write_text(prefix+'\n'.join('{%d,%d,%d,%d,%d},'%x for x in sorted(items))+'\n')
 (a.out/'local_ranged_ammo_generated.inc').write_text(prefix+'\n'.join('{%d,%d,%s,%d},'%(x[0],x[1],ff(x[2]),x[3]) for x in sorted(ammo))+'\n')
 ratings=dbc(a.dbc/'gtCombatRatings.dbc','f');scalars=dict(dbc(a.dbc/'gtOCTClassCombatRatingScalar.dbc','If'));ratios=[]
 for cls in (1,2,3,4,5,6,7,8,9,11):
  for lv in range(1,81):
   hit=scalars[(cls-1)*32+7]/ratings[6*100+lv-1][0];haste=scalars[(cls-1)*32+19]/ratings[18*100+lv-1][0]
   spellhit=scalars[(cls-1)*32+8]/ratings[7*100+lv-1][0]
   ratios.append('{%d,%d,%s,%s,%s},'%(cls,lv,ff(hit),ff(haste),ff(spellhit)))
 (a.out/'local_ranged_ratios_generated.inc').write_text(prefix+'\n'.join(ratios)+'\n')
 info={'sourceCommit':REV,'dbcCommit':DBC_REV,'items':len(items),'ammo':len(ammo),'ratios':len(ratios),'sourceHashes':{x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in (a.sql,a.dbc/'gtCombatRatings.dbc',a.dbc/'gtOCTClassCombatRatingScalar.dbc')},'itemStatTypes':{'attackPower':[38,39],'hit':[17,31],'haste':[29,36],'spellHit':[18,31]},'ratingIndices':{'hit':6,'haste':18,'spellHit':7}}
 a.report.write_text(json.dumps(info,indent=2)+'\n');print(json.dumps(info))
if __name__=='__main__':main()
