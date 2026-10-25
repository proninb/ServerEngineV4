"""Reconcile move-only ownership, retained bytes and allocation invariants."""
import json,statistics,sys
from pathlib import Path
p=Path(sys.argv[1]);audit={};owners={};history=set()
for mode in (0,1):
 ds=[json.loads((p/f'audit-{mode}-{r}.memory.json').read_text()) for r in (0,1)]
 os=[json.loads((p/f'audit-{mode}-{r}.owner.json').read_text()) for r in (0,1)]
 for key in ('heap_peak_live_requested','heap_live_requested_end'):assert ds[0][key]==ds[1][key]
 for phase in ds[0]['phases']:
  for key in ('allocations','requested_bytes','live_requested','frees'):assert ds[0]['phases'][phase][key]==ds[1]['phases'][phase][key],(mode,phase,key)
 assert os[0]['retained_bytes']==os[1]['retained_bytes'];owners[mode]=os[0]
 for o in os:
  assert o['verified'];history.add(o['history_hash'])
  if mode:assert o['buffers_moved'] and o['bound_empty']
 audit[mode]=dict(allocations=sum(s['allocations'] for s in ds[0]['phases'].values()),requested_bytes=sum(s['requested_bytes'] for s in ds[0]['phases'].values()),peak=ds[0]['heap_peak_live_requested'],live_end=ds[0]['heap_live_requested_end'])
assert len(history)==1 and audit[0]==audit[1]
rows=json.loads((p/'runs.json').read_text());timings={}
for mode in (0,1):
 rr=[r for r in rows if r['tag'].startswith('candidate-') and r['own']==mode];assert len(rr)>=5
 timings[mode]={key:statistics.median(r[key] for r in rr) for key in ('transfer_ms','endpoint_release_ms','retained_bytes')}
result=dict(audit=audit,ownership=owners,normal_transfer=timings,history_hash=list(history)[0],retained_saving=owners[0]['retained_bytes']-owners[1]['retained_bytes'])
(p/'analysis.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
