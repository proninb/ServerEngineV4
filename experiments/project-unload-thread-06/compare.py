import json
from pathlib import Path
root=Path('build/project-unload-thread-06')
boundaries=json.loads((root/'boundaries.json').read_text())
normal=json.loads((root/'normalization.json').read_text())
arms={}
for arm in ('single','thread-affinity'):
 rows=[json.loads(x) for x in (root/(arm+'.jsonl')).read_text().splitlines()]
 assert rows[-1]['stage']=='complete'
 end=rows[-1]
 assert (end['alloc'],end['free'],end['unmatched'],end['live'],end['workers_created'],end['workers_stopped'])==(9536484,9536471,159,13,8781,8781)
 cps=[{**x,**boundaries[x['boundary']]} for x in rows if 'boundary' in x]
 assert len(cps)==274
 unload=[x for x in cps if x['stage']=='after_unload_1']
 arms[arm]=dict(initial=rows[0],complete=end,max_unload_drift_bytes=max(x['drift_bytes'] for x in unload),
  max_unload_worker_stack_commit=max(x['worker_stack_commit'] for x in unload),
  max_unload_drift_excluding_worker_stacks=max(x['drift_bytes']-x['worker_stack_commit'] for x in unload),
  selected=[x for x in unload if x['cycle'] in (70,80,90)])
previous=json.loads(Path('build/project-unload-domain-04/comparison.json').read_text())['arms']['shared']
baseline_comparison=[]
for old,new in zip(previous['selected'],arms['single']['selected']):
 assert old['cycle']==new['cycle']
 baseline_comparison.append(dict(cycle=new['cycle'],domain04_shared_drift=old['drift_bytes'],single06_drift=new['drift_bytes'],delta=new['drift_bytes']-old['drift_bytes']))
result=dict(status='INCONCLUSIVE / NO ADOPTION',release='BLOCKED',production_changed=False,
 normalization=normal,arms=arms,domain04_control_comparison=baseline_comparison,
 control_comparability='Maximum similar but phase shifted: source-control high/low checkpoint alignment not reproduced. Do not causally attribute single/thread-affinity difference solely to threads.',
 reproduced_over_128MiB=arms['thread-affinity']['max_unload_drift_bytes']>128*2**20,
 new_replay_sourceID_measured=False,heap_commit_attribution_measured=False,
 decision='Thread identity/lifetime alone did not reproduce failure. No private-domain candidate. Further concurrency/startup-state investigation remains open.',
 limitations=['Heap operations serialized in original order; actual concurrency not reproduced.',
 'Five pre-attach threads created lazily at first observed event; earlier history unavailable.',
 'Source AllocSize, startup heap contents and replay metadata remain fidelity limits.',
 'One run per arm; new single control has checkpoint phase shift relative to historical control.',
 'Stack totals cover active replay workers; controller stack and other runtime allocations are not NT heap attribution.',
 'LFH enabled consistently but replay SourceID not measured; no path equality claim.'])
(root/'comparison.json').write_text(json.dumps(result,indent=2))
print(json.dumps({k:result[k] for k in ('status','reproduced_over_128MiB','control_comparability')}))
for arm in arms:print(arm,arms[arm]['max_unload_drift_bytes']/2**20,arms[arm]['max_unload_worker_stack_commit']/2**20)
