#!/usr/bin/env python3
"""Compile AC base SQL into bounded, indexed local world data. No SQL runtime.

NPC scripts, spells, events and encounter mechanics are NOT emulated by this
converter. Original source files can be retained in a reproducible source-only
archive; manifest/report distinguish selected runtime rows from source coverage.
"""
from __future__ import annotations
import argparse, collections, gzip, hashlib, io, json, math, struct, tarfile
from pathlib import Path
from import_azerothcore import sql_rows, clean, PINNED_COMMIT, REPOSITORY, TABLES

EXTRA_TABLES = ['areatrigger_teleport', 'instance_template']
ALL_TABLES = TABLES + EXTRA_TABLES
MAGIC = b'WPCAT01\0'
CELL = 256
# ObjectDefines.h:44 - the reach a creature gets when creature_model_info has no
# row for its display id, or its CombatReach column is not positive.
DEFAULT_WORLD_OBJECT_SIZE = 0.388999998569489

def encode(value): return json.dumps(value, ensure_ascii=False, separators=(',', ':'), sort_keys=True).encode()
def record_pack(path, values):
    records = [(int(key), encode(value)) for key, value in sorted(values.items())]
    if any(len(data) > 16384 for _, data in records): raise ValueError(f'{path}: record too big')
    offset = 16 + 16 * len(records)
    with path.open('wb') as f:
        f.write(struct.pack('<8sII', MAGIC, len(records), 16))
        for key, data in records:
            f.write(struct.pack('<IQI', key, offset, len(data))); offset += len(data)
        for _, data in records: f.write(data)

def quest_rewards(q):
    """Retain every supported guaranteed reward and every selectable option."""
    def entries(id_pattern, count_pattern, limit):
        out=[]
        for i in range(1,limit+1):
            item,count=q.get(id_pattern.format(i),0),q.get(count_pattern.format(i),0)
            if not item and not count:continue
            if not isinstance(item,int) or not isinstance(count,int) or not 0<item<=0xffffffff or not 0<count<=65535:
                raise ValueError('invalid reward item/count')
            out.append({'itemId':item,'count':count})
        return out
    fixed=entries('rewarditem{}','rewardamount{}',4)
    choices=entries('rewardchoiceitemid{}','rewardchoiceitemquantity{}',6)
    first=fixed[0] if fixed else {'itemId':0,'count':0}
    return {'rewardItem':first['itemId'],'rewardCount':first['count'],
            'additionalRewards':fixed[1:],'rewardChoices':choices}

def quest_reputation_rewards(q):
    out=[]
    for i in range(1,6):
        faction=q.get(f'rewardfactionid{i}',0)
        value=q.get(f'rewardfactionvalue{i}',0)
        override=q.get(f'rewardfactionoverride{i}',0)
        if not faction:
            continue
        if not 0 < faction <= 0xffffffff or not -9 <= value <= 9:
            raise ValueError('invalid quest reputation reward')
        out.append({'factionId':faction,'valueId':value,'overrideValue':override})
    return out

NEUTRAL_FACTIONS = {7,25,31,32,45,49,73,90,91}
AGGRESSIVE_FACTIONS = {14,16,18,21,22,26,28,36,38,40,41,44,48,67,168,189}


def npc_record(e, t, stats, models, model_info, immunities, resistances, respawn_seconds, hostile, aggressive_faction, quest_giver, loot):
    """One catalog NPC definition from its creature_template row (the shared
    formulas of the compiler and of patch_summon_catalog.py)."""
    level=max(1,min(83,t['minlevel']))
    st=stats.get((level,t['unit_class']),stats.get((level,1)))
    if not st:raise ValueError(f'Missing level stats: {e}/{level}')
    expansion=min(2,max(0,t['exp']))
    npc={'id':e,'name':clean(t['name']) or 'Unnamed creature','displayId':models[e]['creaturedisplayid'],'level':level,'faction':t['faction'],
      'unitFlags':t['unit_flags'], 'health':min(1000000000,max(1,round(st[f'basehp{expansion}']*t['healthmodifier']))),
      'damage':max(1,round((st['damage_base']+st['attackpower']/14)*2*t['damagemodifier'])),
      'armor':max(0,round(st['basearmor']*t['armormodifier']/20)),'hostile':hostile,
      'questGiver':quest_giver,'respawnSeconds':respawn_seconds,
      'aggroRadius':min(20,max(0,t['detection_range'])) if hostile and aggressive_faction else 0,
      'loot':loot, 'xp':max(10,round((level*5+45)*t['experiencemodifier'])),
      'money':max(0,round((t['mingold']+t['maxgold'])/2)),
      'upstreamAI':t['ainame'],'upstreamScript':t['scriptname']}
    if t['creatureimmunitiesid']:
        immunity=immunities.get(t['creatureimmunitiesid'])
        if immunity is None:raise ValueError(f"creature {e} names a missing creature_immunities set {t['creatureimmunitiesid']}")
        if not 0<=immunity['schoolmask']<=127 or not 0<=immunity['mechanicsmask']<2**64:raise ValueError(f'invalid immunity set {immunity}')
        if immunity['schoolmask']:npc['immuneSchoolMask']=immunity['schoolmask']
        if immunity['mechanicsmask']:npc['immuneMechanicsMask']=immunity['mechanicsmask']
    if e in resistances and any(resistances[e]):npc['resistances']=list(resistances[e])
    # P05 combat reach and bounding radius, exactly as Creature::SetObjectScale
    # derives UNIT_FIELD_COMBATREACH and UNIT_FIELD_BOUNDINGRADIUS
    # (Creature.cpp:3536-3550): creature_model_info for the chosen display id,
    # DEFAULT_WORLD_OBJECT_SIZE 0.389 when the row is missing or its
    # CombatReach is not positive (ObjectDefines.h:44), the whole multiplied
    # by that model row's DisplayScale (GetNativeObjectScale, Creature.cpp:3528).
    # The model chosen is the same first-by-idx row `displayId` already uses.
    info=model_info.get(models[e]['creaturedisplayid'])
    scale=models[e]['displayscale']
    if not 0<scale<=100:raise ValueError(f'creature {e} has an unusable DisplayScale {scale}')
    reach=(info['combatreach'] if info and info['combatreach']>0 else DEFAULT_WORLD_OBJECT_SIZE)*scale
    radius=(info['boundingradius'] if info and info['boundingradius']>0 else 0.0)*scale
    if not 0<reach<=1000 or not 0<=radius<=1000:raise ValueError(f'creature {e} has an unusable reach/radius {reach}/{radius}')
    npc['combatReach']=round(reach,4)
    if radius:npc['boundingRadius']=round(radius,4)
    return npc


def compile_catalog(sql_dir, output, baseline=None):
    output.mkdir(parents=True, exist_ok=True)
    rows, hashes = {}, {}
    for table in ALL_TABLES:
        path = sql_dir / f'{table}.sql'
        hashes[table] = hashlib.sha256(path.read_bytes()).hexdigest()
        rows[table] = list(sql_rows(path, table))
        print(table, len(rows[table]), flush=True)
    report = {'sourceCommit': PINNED_COMMIT, 'sourceHashes': hashes,
              'sourceRows': {k: len(v) for k,v in rows.items()},
              'excludedSpawns': collections.Counter(), 'excludedQuestReasons': collections.Counter(),
              'excludedQuests': [], 'scriptsExecuted': 0}
    templates = {r['entry']: r for r in rows['creature_template']}
    stats = {(r['level'],r['class']): r for r in rows['creature_classlevelstats']}
    models = {}
    for r in sorted(rows['creature_template_model'], key=lambda r: r['idx']):
        if r['creaturedisplayid']: models.setdefault(r['creatureid'],r)
    model_info = {r['displayid']: r for r in rows['creature_model_info']}
    events = {r['guid'] for r in rows['game_event_creature'] if r['evententry'] > 0}
    pool_first, pool = {}, {}
    for r in sorted(rows['pool_creature'],key=lambda r:r['guid']):
        if r['guid'] not in events: pool_first.setdefault(r['pool_entry'],r['guid'])
        pool[r['guid']] = r['pool_entry']
    spawns = []
    for r in rows['creature']:
        t = templates.get(r['id1']); why = None
        if not t or not models.get(r['id1']): why = 'missing template/model'
        elif not r['spawnmask'] & 1: why = 'non-normal difficulty'
        elif not r['phasemask'] & 1: why = 'non-baseline phase'
        elif r['guid'] in events: why = 'inactive positive event'
        elif r['guid'] in pool and pool_first.get(pool[r['guid']]) != r['guid']: why = 'pool alternative'
        elif t['type'] in (0,8,10) or t['flags_extra'] & 128: why = 'trigger/totem/critter'
        elif t['vehicleid']: why = 'unsupported vehicle'
        if why: report['excludedSpawns'][why] += 1
        else: spawns.append(r)
    entries = {r['id1'] for r in spawns}
    starters, enders = {}, {}
    for r in rows['creature_queststarter']:
        if r['id'] in entries: starters.setdefault(r['quest'],r['id'])
    for r in rows['creature_questender']:
        if r['id'] in entries: enders.setdefault(r['quest'],r['id'])
    neutral = NEUTRAL_FACTIONS
    aggressive = AGGRESSIVE_FACTIONS
    attackable = {e for e in entries if templates[e]['faction'] in neutral | aggressive and not templates[e]['npcflag']}
    direct = collections.defaultdict(list)
    for r in rows['creature_loot_template']:
        if r['item'] > 0 and not r['reference'] and r['lootmode'] & 1:
            direct[r['entry']].append(r)
    available_items = {r['item'] for e in attackable for r in direct[templates[e]['lootid']]}
    addons = {r['id']:r for r in rows['quest_template_addon']}
    quests = {}
    for q in rows['quest_template']:
        qid = q['id']; a = addons.get(qid,{})
        why, objectives = None, []
        if qid not in starters or qid not in enders: why = 'no baseline creature starter/ender'
        elif q['timeallowed'] or q['requiredplayerkills']: why = 'timed/PvP objective'
        elif q['rewardspell'] or q['startitem'] or q['rewardtitle']: why = 'spell/start-item/title requirement'
        elif a.get('specialflags',0) & ~4: why = 'repeat/script/exploration'
        elif a.get('exclusivegroup',0) or a.get('prevquestid',0)<0: why = 'exclusive/group prerequisite'
        elif any(a.get(k,0) for k in ('sourcespellid','requiredskillid','rewardmailtemplateid')): why = 'skill/spell/mail condition'
        elif any(q.get(k,0) for k in ('requireditemid5','requireditemid6')): why = 'unsupported extra objective'
        elif q['rewardmoney'] < 0: why = 'money payment'
        for i in range(1,5):
            entry,count=q[f'requirednpcorgo{i}'],q[f'requirednpcorgocount{i}']
            if entry<0: why=why or 'gameobject objective'
            elif entry and count:
                if entry not in attackable: why=why or 'unsupported/nonattackable kill target'
                else: objectives.append({'type':'kill','entry':entry,'count':count})
            item,count=q[f'requireditemid{i}'],q[f'requireditemcount{i}']
            if item and count:
                if item not in available_items: why=why or 'item lacks direct baseline drop'
                else: objectives.append({'type':'collect','entry':item,'count':count})
        if len(objectives)>4: why=why or 'more than four objectives'
        try:
            rewards=quest_rewards(q); reputation_rewards=quest_reputation_rewards(q)
        except ValueError:
            why=why or 'invalid reward item/count or reputation';rewards={};reputation_rewards=[]
        if not why and not objectives:
            if not q['logdescription'] and not q['questdescription']: why='empty definition'
            else: objectives=[{'type':'talk','entry':enders[qid],'count':1}]
        if why:
            report['excludedQuestReasons'][why]+=1
            report['excludedQuests'].append({'id':qid,'reason':why});continue
        quests[qid]={'id':qid,'title':clean(q['logtitle']),'description':clean(q['logdescription'] or q['questdescription'],1024),
          'giverEntry':starters[qid],'turnInEntry':enders[qid],'minLevel':max(1,min(80,q['minlevel'])),
          'allowableRaces':max(0,q['allowableraces']), 'allowableClasses':max(0,a.get('allowableclasses',0)),
          'prerequisite':max(0,a.get('prevquestid',0)),'xp':max(50,q['questlevel']*80),
          'requiredMinRepFaction':max(0,a.get('requiredminrepfaction',0)),
          'requiredMinRepValue':a.get('requiredminrepvalue',0),
          'requiredMaxRepFaction':max(0,a.get('requiredmaxrepfaction',0)),
          'requiredMaxRepValue':a.get('requiredmaxrepvalue',0),
          'reputationRequirements':[{'factionId':q[f'requiredfactionid{i}'],'value':q[f'requiredfactionvalue{i}']} for i in (1,2) if q[f'requiredfactionid{i}']],
          'money':max(0,q['rewardmoney']),**rewards,'reputationRewards':reputation_rewards,'objectives':objectives}
    def remove_prerequisites():
        while True:
            removed=[qid for qid,q in quests.items() if q['prerequisite'] and q['prerequisite'] not in quests]
            if not removed:break
            for qid in removed:
                del quests[qid];report['excludedQuestReasons']['prerequisite excluded']+=1
                report['excludedQuests'].append({'id':qid,'reason':'prerequisite excluded'})
    remove_prerequisites()
    questdrops = {o['entry'] for q in quests.values() for o in q['objectives'] if o['type']=='collect'}
    npc_loot={}
    for e in entries:
        selected=[]; ordinary=0
        for r in sorted(direct[templates[e]['lootid']],key=lambda r:(r['item'] not in questdrops,-r['chance'],r['item'])):
            if r['questrequired'] and r['item'] not in questdrops:continue
            if r['item'] not in questdrops:
                if ordinary>=2:continue
                ordinary+=1
            if len(selected)<8:selected.append({'itemId':r['item'],'count':max(1,min(65535,r['mincount']))})
        npc_loot[e]=selected
    emitted={s['itemId'] for values in npc_loot.values() for s in values}
    for qid in list(quests):
        if any(o['type']=='collect' and o['entry'] not in emitted for o in quests[qid]['objectives']):
            del quests[qid];report['excludedQuestReasons']['drop truncated']+=1
            report['excludedQuests'].append({'id':qid,'reason':'drop truncated'})
    remove_prerequisites()
    slots={13:1,17:1,21:1,5:2,20:2,7:3,8:4}
    items={}
    for i in rows['item_template']:
        stamina=sum(i[f'stat_value{n}'] for n in range(1,11) if i[f'stat_type{n}']==7)
        slot=slots.get(i['inventorytype'],0)
        items[i['entry']]={'id':i['entry'],'name':clean(i['name']) or 'Unnamed item','displayId':i['displayid'],
          'inventoryType':i['inventorytype'],'slot':slot,'stack':max(1,min(1000,i['stackable'] or 1)),
          'maxHealth':max(0,stamina*10),'attack':max(0,round((i['dmg_min1']+i['dmg_max1'])/2)) if slot==1 else 0,
          'armor':max(0,round(i['armor']/10)) if slot in (2,3,4) else 0,'heal':{117:30,118:70}.get(i['entry'],0),
          'mana':40 if i['entry']==159 else 0,'value':max(0,i['sellprice']),
          'requiredReputationFaction':max(0,i['requiredreputationfaction']),
          'requiredReputationRank':max(0,min(7,i['requiredreputationrank'])) if i['requiredreputationfaction'] else 0}
    for e in npc_loot: npc_loot[e]=[s for s in npc_loot[e] if s['itemId'] in items]
    questgivers={q[k] for q in quests.values() for k in ['giverEntry','turnInEntry']}
    # P04 creature immunity sets (SpellMgr::LoadCreatureImmunities reads all
    # seven columns; only SchoolMask and MechanicsMask intersect any spell this
    # realm casts - docs/CURRENT_IMPLEMENTATION.md section 6.5 - so
    # only those two are emitted, by name, and only on a creature that points
    # at a set; a dangling CreatureImmunitiesId is an error, not a zero) and
    # creature_template_resistance rows in UNIT_FIELD_RESISTANCES order 1..6,
    # negative values clamped to zero as the runtime's std::max does.
    immunities={r['id']:r for r in rows['creature_immunities']}
    resistances=collections.defaultdict(lambda:[0]*6)
    for r in rows['creature_template_resistance']:
        if not 1<=r['school']<=6:raise ValueError(f"creature_template_resistance school out of range: {r}")
        resistances[r['creatureid']][r['school']-1]=max(0,min(65535,r['resistance'] or 0))
    respawn={}
    for s in spawns:respawn[s['id1']]=min(respawn.get(s['id1'],300),max(15,s['spawntimesecs']))
    npcs={}
    for e in sorted(entries):
        npcs[e]=npc_record(e,templates[e],stats,models,model_info,immunities,resistances,min(300,respawn[e]),
                           hostile=e in attackable,aggressive_faction=templates[e]['faction'] in aggressive,quest_giver=e in questgivers,loot=npc_loot[e])
    # Preserve the shipped starter's previously validated adaptation verbatim.
    if baseline:
        starter=json.loads(baseline.read_text())
        for key,target in [('items',items),('npcs',npcs),('quests',quests)]:
            for r in starter[key]:
                prior=target.get(r['id'],{})
                if key=='quests':
                    prior.pop('additionalRewards',None);prior.pop('rewardChoices',None)
                prior.update(r);target[r['id']]=prior
    contacts=collections.defaultdict(set)
    for qid,q in quests.items():
        for key in ['giverEntry','turnInEntry']:contacts[q[key]].add(qid)
    for e in contacts:
        if e in npcs:npcs[e]['questGiver']=True
    for name,values in [('npcs',npcs),('items',items),('quests',quests),('contacts',{k:sorted(v) for k,v in contacts.items()})]:
        record_pack(output/f'{name}.pack',values)
    cells=collections.defaultdict(list)
    mapcounts=collections.Counter()
    for s in spawns:
        cells[(s['map'],math.floor(s['position_x']/CELL),math.floor(s['position_y']/CELL))].append(s)
        mapcounts[s['map']]+=1
    with (output/'cells.idx').open('wb') as idx,(output/'spawns.pack').open('wb') as data:
        idx.write(struct.pack('<8sII',MAGIC,len(cells),24))
        for (mapid,x,y),values in sorted(cells.items()):
            idx.write(struct.pack('<IiiQI',mapid,x,y,data.tell(),len(values)))
            for s in sorted(values,key=lambda r:r['guid']):
                data.write(struct.pack('<IIIffff',s['guid'],s['id1'],mapid,s['position_x'],s['position_y'],s['position_z'],s['orientation']))
    instances={r['map']:r for r in rows['instance_template']}
    allmaps={r['map'] for r in rows['creature']}|set(instances)|{r['map'] for r in rows['playercreateinfo']}|{r['target_map'] for r in rows['areatrigger_teleport']}
    manifest={'schemaVersion':1,'cellSize':CELL,'sourceCommit':PINNED_COMMIT,'repository':REPOSITORY,
      'maps':[{'id':i,'spawnCount':mapcounts[i],'instanceMap':i in instances,'upstreamScript':instances.get(i,{}).get('script','')} for i in sorted(allmaps)],
      'starts':[{'race':r['race'],'classId':r['class'],'level':55 if r['class']==6 else 1,'mapId':r['map'],'x':r['position_x'],'y':r['position_y'],'z':r['position_z'],'orientation':r['orientation']} for r in rows['playercreateinfo']],
      'destinations':[{'id':r['id'],'name':clean(r['name'],128),'mapId':r['target_map'],'x':r['target_position_x'],'y':r['target_position_y'],'z':r['target_position_z'],'orientation':r['target_orientation'],'instanceMap':r['target_map'] in instances} for r in rows['areatrigger_teleport']]}
    manifest['files']={p.name:{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(output.iterdir()) if p.suffix in ('.pack','.idx')}
    # Includes positions/starts/destinations, so changed profile content also
    # invalidates LAN joins even if the NPC/quest files are identical.
    manifest['fingerprint']=int.from_bytes(hashlib.sha256(encode(manifest)).digest()[:4],'little') or 1
    (output/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    report['coverage']={'npcsWithCombatReachAboveDefault':sum(1 for n in npcs.values() if n.get('combatReach',0)>1.5),
      'npcsWithImmunitySet':sum(1 for n in npcs.values() if 'immuneSchoolMask' in n or 'immuneMechanicsMask' in n),
      'npcsWithResistance':sum(1 for n in npcs.values() if 'resistances' in n),'maps':len(allmaps),'mapsWithBaselineCreatures':len(mapcounts),'spawns':len(spawns),'cells':len(cells),'npcs':len(npcs),'items':len(items),'quests':len(quests),'questsWithChoices':sum(bool(q.get('rewardChoices')) for q in quests.values()),'questsWithMultipleFixedRewards':sum(bool(q.get('additionalRewards')) for q in quests.values()),'profiles':len(manifest['starts']),'destinations':len(manifest['destinations']),'instanceTemplates':len(instances),'runtimeBytes':sum(v['bytes'] for v in manifest['files'].values())}
    report['limits']=['Normal difficulty + baseline phase only; positive events and alternative pool members excluded.',
      'Each spawn uses id1 and first source model; randomized id2/id3 and appearance probabilities are not simulated.',
      'SQL source IDs, names, positions, item displays and objective IDs/counts retained; combat stats use local formulas.',
      'Unknown factions remain friendly. Known hostile/neutral starter factions retain the B2 mapping; no complete FactionTemplate.dbc resolver.',
      'Maximum 128 active NPCs; cell reads and individual definition loads are bounded. World data is paged from disk.',
      'Item metadata does not implement all item effects. Four equipment slots; local abilities only.',
      'Only supported kill/collect/talk quests with available baseline actors are emitted. Up to four guaranteed and six choice rewards retained; drop count/probability simplified.',
      'Creature SmartAI and C++ instance scripts are source metadata only, never claimed executed. Boss phases, gameobjects, paths and encounter events remain unsupported.',
      'Teleport SQL provides validated destination coordinates only. Automatic area triggering requires client DBC geometry.',
      'Base dump snapshot only; incremental upstream database updates are not executed.']
    (output/'IMPORT_REPORT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report['coverage'],sort_keys=True),flush=True)
    return manifest,report

def archive_source(directory,path):
    # Exact upstream SQL source, compressed without nondeterministic timestamps.
    raw=io.BytesIO()
    with tarfile.open(fileobj=raw,mode='w') as tar:
        for table in sorted(ALL_TABLES):
            data=(directory/f'{table}.sql').read_bytes();info=tarfile.TarInfo(f'{table}.sql');info.size=len(data);info.mtime=0;info.mode=0o644
            tar.addfile(info,io.BytesIO(data))
    path.write_bytes(gzip.compress(raw.getvalue(),mtime=0))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sql-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--baseline',type=Path);p.add_argument('--archive-source',type=Path)
    a=p.parse_args();compile_catalog(a.sql_dir,a.output,a.baseline)
    if a.archive_source:archive_source(a.sql_dir,a.archive_source)
if __name__=='__main__':main()
