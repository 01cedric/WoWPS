#!/usr/bin/env python3
"""Reproduce the 1.80 consolidated host gate; no commercial assets required."""
import concurrent.futures,json,os,pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[2]
out=pathlib.Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
names=['local_warning_regressions','local_quest_rewards','local_progression_0173','local_bank_slots_0174','local_auction_lan_0175','local_party_lifecycle_0176','local_travel_0177','framexml_pad_0178','local_services_framexml','quest_rewards_framexml','visual_state_0179','minimap_display','memory_performance','memory_recovery_0180']
jobs=[(n,['bash',str(root/'tools/tests'/('run_'+n+'_tests.sh'))]) for n in names]
jobs += [(n,['python3',str(root/'tools/tests'/(n+'_test.py'))]) for n in ['local_snapshot_reuse','package_staging_0178']]
previous=[]
if len(sys.argv)>2:
 selected=set(sys.argv[2:])
 if not selected <= {name for name,_ in jobs}:raise SystemExit('Unknown suite selection')
 previous=json.loads((out/'release-gate-0180.json').read_text())
 jobs=[job for job in jobs if job[0] in selected]
def run(job):
 name,cmd=job
 with (out/(name+'-0180.log')).open('w') as f:
  p=subprocess.run(cmd,cwd=root,env={**os.environ,'SANITIZE':'1','ASAN_OPTIONS':'detect_leaks=0'},stdout=f,stderr=subprocess.STDOUT)
 s=(out/(name+'-0180.log')).read_text()
 result={'suite':name,'exit':p.returncode,'pass_groups':sum(l.startswith('PASS ') for l in s.splitlines()),'sanitizer_error':'ERROR: AddressSanitizer' in s or 'runtime error:' in s}
 print(json.dumps(result),flush=True);return result
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:results=list(pool.map(run,jobs))
if previous:
 updated={r['suite']:r for r in results}
 results=[updated.get(r['suite'],r) for r in previous]
(out/'release-gate-0180.json').write_text(json.dumps(results,indent=2)+'\n')
sys.exit(any(r['exit'] or r['sanitizer_error'] for r in results))
