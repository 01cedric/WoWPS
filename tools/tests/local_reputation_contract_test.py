#!/usr/bin/env python3
from __future__ import annotations
import hashlib, json, re, struct, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
CAT=ROOT/'assets/local_realm/catalog'
MAGIC=b'WPCAT01\0'

def read_pack(path: Path):
    data=path.read_bytes(); magic,count,index=struct.unpack_from('<8sII',data,0)
    assert magic==MAGIC and index==16
    out={}
    for i in range(count):
        key,offset,length=struct.unpack_from('<IQI',data,16+i*16)
        out[key]=json.loads(data[offset:offset+length])
    return out

realm=(ROOT/'src/game/local_realm.cpp').read_text()
lan=(ROOT/'include/game/lan_discovery.hpp').read_text()
save_version=int(re.search(r'constexpr uint8_t SaveVersion = (\d+)\b',realm).group(1))
gameplay_version=int(re.search(r'GameplayVersion = (\d+)\b',lan).group(1))
assert save_version >= 34, save_version
assert gameplay_version >= 89, gameplay_version
assert 'if(version>=34)' in realm and 'p.reputations.reserve(reputationCount)' in realm
assert 'validLocalReputations(record.player)' in realm
assert 'p.migrateLegacyReputation = version < 34' in realm

manifest=json.loads((CAT/'manifest.json').read_text())
for name,meta in manifest['files'].items():
    data=(CAT/name).read_bytes()
    assert len(data)==meta['bytes']
    assert hashlib.sha256(data).hexdigest()==meta['sha256']

items=read_pack(CAT/'items.pack')
quests=read_pack(CAT/'quests.pack')
npcs=read_pack(CAT/'npcs.pack')
contacts=read_pack(CAT/'contacts.pack')
item_gates=[x for x in items.values() if x.get('requiredReputationFaction',0)]
assert len(item_gates)==909, len(item_gates)
assert all(0<=x.get('requiredReputationRank',0)<=7 for x in item_gates)
assert all(x.get('requiredReputationRank',0)==0 for x in items.values() if not x.get('requiredReputationFaction',0))

quest_gates=[]; reward_quests=[]
for q in quests.values():
    reqs=q.get('reputationRequirements',[])
    if q.get('requiredMinRepFaction',0) or q.get('requiredMaxRepFaction',0) or reqs:
        quest_gates.append(q)
        assert all(r.get('factionId',0)>0 and -42000<=r.get('value',0)<=42999 for r in reqs)
        assert q['id'] in contacts.get(q['giverEntry'],[]) and q['id'] in contacts.get(q['turnInEntry'],[])
        assert npcs[q['giverEntry']].get('questGiver') and npcs[q['turnInEntry']].get('questGiver')
    if q.get('reputationRewards'): reward_quests.append(q)
assert len(quest_gates)==31, len(quest_gates)
assert len(reward_quests)==639, len(reward_quests)
for q in reward_quests:
    assert len(q['reputationRewards'])<=5
    for r in q['reputationRewards']:
        assert r['factionId']>0 and -9<=r['valueId']<=9

rows=[]
for line in (ROOT/'include/game/local_vendor_stock_generated.inc').read_text().splitlines():
    m=re.fullmatch(r'\{(\d+)u,(\d+)u,(\d+)u,(\d+)u,(\d+)u,(\d+)u\},',line)
    if m: rows.append(tuple(map(int,m.groups())))
assert len(rows)==24298, len(rows)
gated=[r for r in rows if r[4]]
assert len(gated)==971, len(gated)
assert all(0<=r[5]<=7 for r in gated)
report=json.loads((ROOT/'tools/local_realm/VENDOR_IMPORT_REPORT.json').read_text())
assert 'reputation purchase gate not implemented' not in report.get('excludedReasons',{})
assert report['runtimeOffers']==24298

application=(ROOT/'src/core/application_local_realm.cpp').read_text()
assert 'loadDBC("Faction.dbc")' in application and 'setFactionReputationBases' in application
assert 'loadDBC("QuestFactionReward.dbc")' in application

gameplay=(ROOT/'src/game/local_gameplay.cpp').read_text()
quest_eligibility=(ROOT/'include/game/local_quest_eligibility.hpp').read_text()
assert 'localFactionBaseReputation' in gameplay and 'migrateLegacyReputation' in gameplay
assert 'localVendorDiscountedBuyTotal' in gameplay
assert 'Your reputation is too low for this quest' in quest_eligibility

gh=(ROOT/'src/game/game_handler.cpp').read_text()
assert 'return localReputationStanding(*player, factionId);' in gh
assert 'if (localExploration_) return;' in gh

print(f'PASS 4.3 reputation contract: current Save{save_version}/LAN{gameplay_version}, migration floor Save34/LAN89, base migration, 909 item gates, 971 vendor gates, 31 quest gates, 639 reward quests, manifest hashes')
