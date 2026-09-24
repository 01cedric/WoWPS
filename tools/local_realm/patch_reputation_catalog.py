#!/usr/bin/env python3
"""Patch reputation metadata into an already compiled local world catalog.

This is intentionally narrower than import_world_catalog.py: it reuses the
existing bounded NPC/spawn selection and only refreshes item/quest reputation
fields from the pinned item_template/quest SQL snapshots.
"""
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path
from import_azerothcore import sql_rows, clean

MAGIC=b'WPCAT01\0'

def encode(v):
    return json.dumps(v, ensure_ascii=False, separators=(',', ':'), sort_keys=True).encode()

def read_pack(path: Path):
    data=path.read_bytes()
    magic,count,index=struct.unpack_from('<8sII',data,0)
    if magic!=MAGIC or index!=16: raise ValueError(f'{path}: unsupported pack')
    out={}
    for i in range(count):
        key,offset,length=struct.unpack_from('<IQI',data,16+i*16)
        out[key]=json.loads(data[offset:offset+length])
    return out

def write_pack(path: Path, values):
    records=[(int(k),encode(v)) for k,v in sorted(values.items())]
    offset=16+16*len(records)
    with path.open('wb') as f:
        f.write(struct.pack('<8sII',MAGIC,len(records),16))
        for key,blob in records:
            f.write(struct.pack('<IQI',key,offset,len(blob))); offset+=len(blob)
        for _,blob in records: f.write(blob)

def refresh_manifest(catalog: Path):
    manifest=json.loads((catalog/'manifest.json').read_text())
    manifest['files']={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
                       for p in sorted(catalog.iterdir()) if p.suffix in ('.pack','.idx')}
    base=dict(manifest); base.pop('fingerprint',None)
    manifest['fingerprint']=int.from_bytes(hashlib.sha256(encode(base)).digest()[:4],'little') or 1
    (catalog/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    return manifest['fingerprint']

def main(sql: Path, catalog: Path):
    items=read_pack(catalog/'items.pack')
    item_changed=item_gated=0
    for row in sql_rows(sql/'item_template.sql','item_template'):
        dst=items.get(row['entry'])
        if dst is None: continue
        faction=max(0,row['requiredreputationfaction'])
        rank=max(0,min(7,row['requiredreputationrank'])) if faction else 0
        if faction: item_gated+=1
        if dst.get('requiredReputationFaction',0)!=faction or dst.get('requiredReputationRank',0)!=rank:
            dst['requiredReputationFaction']=faction; dst['requiredReputationRank']=rank; item_changed+=1
    write_pack(catalog/'items.pack',items)

    quests=read_pack(catalog/'quests.pack')
    npcs=read_pack(catalog/'npcs.pack')
    contacts=read_pack(catalog/'contacts.pack')
    addons={r['id']:r for r in sql_rows(sql/'quest_template_addon.sql','quest_template_addon')}
    quest_rows={r['id']:r for r in sql_rows(sql/'quest_template.sql','quest_template')}
    starters={}
    for r in sql_rows(sql/'creature_queststarter.sql','creature_queststarter'):
        if r['id'] in npcs: starters.setdefault(r['quest'],r['id'])
    enders={}
    for r in sql_rows(sql/'creature_questender.sql','creature_questender'):
        if r['id'] in npcs: enders.setdefault(r['quest'],r['id'])
    available_items={stack['itemId'] for npc in npcs.values() for stack in npc.get('loot',[])}

    def reputation_requirements(q):
        return [{'factionId':q[f'requiredfactionid{i}'],'value':q[f'requiredfactionvalue{i}']}
                for i in (1,2) if q.get(f'requiredfactionid{i}',0)]

    def rep_rewards(q):
        reps=[]
        for i in range(1,6):
            faction=q.get(f'rewardfactionid{i}',0)
            if not faction: continue
            value=q.get(f'rewardfactionvalue{i}',0); override=q.get(f'rewardfactionoverride{i}',0)
            if not -9<=value<=9: raise ValueError(f'quest {q["id"]}: reward faction value id out of range')
            reps.append({'factionId':faction,'valueId':value,'overrideValue':override})
        return reps

    def item_rewards(q):
        def entries(id_pattern,count_pattern,limit):
            out=[]
            for i in range(1,limit+1):
                item,count=q.get(id_pattern.format(i),0),q.get(count_pattern.format(i),0)
                if not item and not count: continue
                if item not in items or not 0<count<=65535: raise ValueError('invalid reward item/count')
                out.append({'itemId':item,'count':count})
            return out
        fixed=entries('rewarditem{}','rewardamount{}',4); choices=entries('rewardchoiceitemid{}','rewardchoiceitemquantity{}',6)
        first=fixed[0] if fixed else {'itemId':0,'count':0}
        return first,fixed[1:],choices

    gate_changed=reward_changed=requirement_changed=gated=rewarded=0
    for qid,dst in quests.items():
        a=addons.get(qid,{})
        gate={'requiredMinRepFaction':max(0,a.get('requiredminrepfaction',0)),
              'requiredMinRepValue':a.get('requiredminrepvalue',0),
              'requiredMaxRepFaction':max(0,a.get('requiredmaxrepfaction',0)),
              'requiredMaxRepValue':a.get('requiredmaxrepvalue',0)}
        q=quest_rows.get(qid,{})
        reqs=reputation_requirements(q) if q else []
        if gate['requiredMinRepFaction'] or gate['requiredMaxRepFaction'] or reqs: gated+=1
        if any(dst.get(k,0)!=v for k,v in gate.items()): dst.update(gate); gate_changed+=1
        if dst.get('reputationRequirements',[])!=reqs: dst['reputationRequirements']=reqs; requirement_changed+=1
        reps=rep_rewards(q) if q else []
        if reps: rewarded+=1
        if dst.get('reputationRewards',[])!=reps: dst['reputationRewards']=reps; reward_changed+=1

    # Pull only reputation-gated quests that the already compiled world can
    # execute. This avoids a full creature/spawn re-import while preserving the
    # same objective safety rules as import_world_catalog.py.
    added={}
    for qid,q in quest_rows.items():
        if qid in quests: continue
        a=addons.get(qid,{})
        reqs=reputation_requirements(q)
        if not (a.get('requiredminrepfaction',0) or a.get('requiredmaxrepfaction',0) or reqs): continue
        if qid not in starters or qid not in enders: continue
        if q['timeallowed'] or q['requiredplayerkills'] or q['rewardspell'] or q['startitem'] or q['rewardtitle']: continue
        if a.get('specialflags',0)&~4 or a.get('exclusivegroup',0) or a.get('prevquestid',0)<0: continue
        if any(a.get(k,0) for k in ('sourcespellid','requiredskillid','rewardmailtemplateid')): continue
        if q.get('requireditemid5',0) or q.get('requireditemid6',0) or q['rewardmoney']<0: continue
        objectives=[]; bad=False
        for i in range(1,5):
            entry,count=q[f'requirednpcorgo{i}'],q[f'requirednpcorgocount{i}']
            if entry<0: bad=True; break
            if entry and count:
                if entry not in npcs or not npcs[entry].get('hostile',False): bad=True; break
                objectives.append({'type':'kill','entry':entry,'count':count})
            item,count=q[f'requireditemid{i}'],q[f'requireditemcount{i}']
            if item and count:
                if item not in available_items: bad=True; break
                objectives.append({'type':'collect','entry':item,'count':count})
        if bad or len(objectives)>4: continue
        if not objectives:
            if not q['logdescription'] and not q['questdescription']: continue
            objectives=[{'type':'talk','entry':enders[qid],'count':1}]
        try: first,extra,choices=item_rewards(q)
        except ValueError: continue
        added[qid]={'id':qid,'title':clean(q['logtitle']) or f'Quest {qid}',
          'description':clean(q['logdescription'] or q['questdescription'],1024),
          'giverEntry':starters[qid],'turnInEntry':enders[qid],'minLevel':max(1,min(80,q['minlevel'])),
          'allowableRaces':max(0,q['allowableraces']),'allowableClasses':max(0,a.get('allowableclasses',0)),
          'requiredSkill':0,'prerequisite':max(0,a.get('prevquestid',0)),
          'requiredMinRepFaction':max(0,a.get('requiredminrepfaction',0)),
          'requiredMinRepValue':a.get('requiredminrepvalue',0),
          'requiredMaxRepFaction':max(0,a.get('requiredmaxrepfaction',0)),
          'requiredMaxRepValue':a.get('requiredmaxrepvalue',0),'reputationRequirements':reqs,
          'xp':max(50,q['questlevel']*80),'money':max(0,q['rewardmoney']),
          'rewardItem':first['itemId'],'rewardCount':first['count'],'additionalRewards':extra,
          'rewardChoices':choices,'reputationRewards':rep_rewards(q),'objectives':objectives}
    # A quest whose positive prerequisite is still unavailable is not newly
    # exposed. Iterate because a removed prerequisite can orphan another row.
    while True:
        remove=[qid for qid,q in added.items() if q['prerequisite'] and q['prerequisite'] not in quests and q['prerequisite'] not in added]
        if not remove: break
        for qid in remove: del added[qid]
    for qid,q in added.items():
        quests[qid]=q
        for entry in {q['giverEntry'],q['turnInEntry']}:
            rows=contacts.setdefault(entry,[])
            if qid not in rows: rows.append(qid); rows.sort()
            npcs[entry]['questGiver']=True
    write_pack(catalog/'items.pack',items)
    write_pack(catalog/'quests.pack',quests)
    write_pack(catalog/'contacts.pack',contacts)
    write_pack(catalog/'npcs.pack',npcs)
    fingerprint=refresh_manifest(catalog)
    result={'items':len(items),'reputationGatedItems':item_gated,'itemRecordsChanged':item_changed,
            'quests':len(quests),'newReputationGatedQuests':len(added),'existingReputationGatedQuests':gated,
            'questGateRecordsChanged':gate_changed,'questRequirementRecordsChanged':requirement_changed,
            'reputationRewardQuests':sum(bool(q.get('reputationRewards')) for q in quests.values()),
            'questRewardRecordsChanged':reward_changed,'catalogFingerprint':fingerprint}
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sql-dir',type=Path,required=True);ap.add_argument('--catalog',type=Path,required=True)
    a=ap.parse_args();main(a.sql_dir,a.catalog)
