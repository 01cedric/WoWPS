#!/usr/bin/env python3
import argparse,hashlib,json,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/local_realm'))
from import_world_catalog import quest_rewards

def records(path):
    data=path.read_bytes();assert data[:8]==b'WPCAT01\0';count,width=struct.unpack_from('<II',data,8);assert width==16
    out={}
    for i in range(count):
        key,offset,size=struct.unpack_from('<IQI',data,16+16*i)
        assert key not in out and offset>=16+16*count and 0<size<=16384 and offset+size<=len(data)
        out[key]=json.loads(data[offset:offset+size])
    return out

def main():
    a=argparse.ArgumentParser();a.add_argument('baseline',type=Path);a.add_argument('candidate',type=Path);args=a.parse_args()
    row={}
    for i in range(1,5):row[f'rewarditem{i}']=100+i;row[f'rewardamount{i}']=i
    for i in range(1,7):row[f'rewardchoiceitemid{i}']=200+i;row[f'rewardchoiceitemquantity{i}']=i
    r=quest_rewards(row);assert r['rewardItem']==101 and len(r['additionalRewards'])==3 and len(r['rewardChoices'])==6
    assert r['rewardChoices'][-1]=={'itemId':206,'count':6}
    for invalid in [{'rewarditem1':1},{'rewarditem1':-1,'rewardamount1':1},{'rewardchoiceitemid6':1,'rewardchoiceitemquantity6':65536}]:
        try:quest_rewards(invalid)
        except ValueError:pass
        else:raise AssertionError('accepted malformed reward')
    assert quest_rewards({})['rewardChoices']==[]
    old=records(args.baseline/'quests.pack');new=records(args.candidate/'quests.pack');items=records(args.candidate/'items.pack')
    assert set(old)<=set(new)
    for id,q in old.items():assert q['objectives']==new[id]['objectives'],id
    for q in new.values():
        rewards=([{'itemId':q['rewardItem'],'count':q['rewardCount']}] if q['rewardItem'] else [])+q.get('additionalRewards',[])+q.get('rewardChoices',[])
        for r in rewards:assert r['itemId'] in items and 0<r['count']<=65535
    m=json.loads((args.candidate/'manifest.json').read_text())
    for name,meta in m['files'].items():
        data=(args.candidate/name).read_bytes();assert len(data)==meta['bytes'] and hashlib.sha256(data).hexdigest()==meta['sha256']
    for name in ['items.pack','spawns.pack','cells.idx']:assert (args.baseline/name).read_bytes()==(args.candidate/name).read_bytes()
    (args.candidate/'quest-test-ids.txt').write_text(''.join(f'{id}\n' for id in sorted(new)))
    report={'baseline_quests':len(old),'current_quests':len(new),'added_quests':sorted(set(new)-set(old)),
        'choice_quests':sum(bool(q.get('rewardChoices')) for q in new.values()),'multiple_fixed_quests':sum(bool(q.get('additionalRewards')) for q in new.values()),
        'removed_existing_quests':[],'existing_objectives_changed':[],'fingerprint':m['fingerprint']}
    print('PASS importer: four guaranteed/six selectable rewards; malformed counts rejected; no implicit first-choice conversion')
    print('PASS migration: all existing quests/objectives retained; every reward item resolves; item/spawn/cell bytes unchanged; manifest hashes verified')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
