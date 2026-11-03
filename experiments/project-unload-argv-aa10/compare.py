import json,statistics,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'project-unload-control-07'))
from control_stability import sha,validate
def summary(values):return dict(n=len(values),min=min(values),median=statistics.median(values),max=max(values),spread=max(values)-min(values))
def main():
 root=Path('build/project-unload-control-argv-aa-10/run-01');capture=json.loads((root/'capture.json').read_text());base=Path('build/project-unload-thread-06')
 if capture['status']!='CAPTURE COMPLETE / ALL 16 VALIDATED' or len(capture['runs'])!=16:raise ValueError('capture gate')
 reference=[json.loads(x) for x in (base/'single.jsonl').read_text().splitlines()][1:-1];boundaries=json.loads((base/'boundaries.json').read_text());results=[]
 for run in capture['runs']:
  file=root/('run-%02d-%s.jsonl'%(run['run'],run['arm']))
  if sha(file)!=run['stdout_sha256'] or run['before_hashes']!=capture['hashes'] or run['after_hashes']!=capture['hashes'] or run['environment_sha256']!=capture['environment_sha256']:raise ValueError('identity/hash gate')
  rows=[json.loads(x) for x in file.read_text().splitlines()];cps=validate(rows,reference);byid={cp['boundary']:cp for cp in cps}
  selected={str(boundaries[cp['boundary']]['cycle']):cp['drift_bytes'] for cp in cps if cp['stage_code']==2 and boundaries[cp['boundary']]['cycle'] in (70,80,90)}
  results.append(dict(run=run['run'],arm=run['arm'],pid=run['pid'],b01=byid[1]['private_bytes'],b02=byid[2]['private_bytes'],b03=byid[3]['private_bytes'],
   b02_to_b03=byid[3]['private_bytes']-byid[2]['private_bytes'],b03_drift=byid[3]['drift_bytes'],max_quiescent_drift=max(cp['drift_bytes'] for cp in cps if cp['stage_code']==2),selected_drift=selected))
 arms={arm:{key:summary([run[key] for run in results if run['arm']==arm]) for key in ('b01','b02','b03','b02_to_b03','b03_drift','max_quiescent_drift')} for arm in 'AB'}
 for arm in arms:arms[arm]['selected_drift']={c:summary([r['selected_drift'][c] for r in results if r['arm']==arm]) for c in ('70','80','90')}
 result=dict(status='COMPLETE / NO ADOPTION',release='BLOCKED',runs=results,arms=arms,all_16_signatures_and_counters_verified=True,
  argv_only_causal_effect_proven=False,within_arm_variability=any(v['spread'] for data in arms.values() for k,v in data.items() if k!='selected_drift'),
  both_arms_quiescent_max_spread_over_1MiB=all(data['max_quiescent_drift']['spread']>2**20 for data in arms.values()),
  path_spelling_alone_explains_variability=False,
  interpretation='Both fixed-argv groups retain large within-arm variability. Path spelling alone does not explain it; an argv contribution or group difference is not excluded. No allocator/concurrency mechanism identified.',
  limits=['Eight processes per arm; no claim of equivalence or causal mechanism from median differences alone.',
   'Environment values fixed in memory and passed explicitly; only digest/key names saved to avoid disclosing secrets.',
   'No hooks, ETW, VirtualQueryEx, private heap, rebuild or observer-on/off experiment.'])
 with (root/'comparison.json').open('x') as output:json.dump(result,output,indent=2)
 print(json.dumps(arms,indent=2))
if __name__=='__main__':main()
