"""Reconcile audit deltas and save normal-stage medians after a full series."""
import json,statistics,sys
from pathlib import Path
p=Path(sys.argv[1]);audit={}
for mode in (0,1):
 ds=[json.loads((p/f'audit-{mode}-{r}.memory.json').read_text()) for r in (0,1)]
 for key in ('heap_peak_live_requested','heap_live_requested_end'):
  assert ds[0][key]==ds[1][key],(mode,key)
 for phase in ds[0]['phases']:
  # Extraction order changes transient old/new local-buffer overlap; its
  # per-phase peak is not an equivalence invariant. Counts and traffic are.
  for key in ('allocations','requested_bytes','live_requested','frees'):
   assert ds[0]['phases'][phase][key]==ds[1]['phases'][phase][key],(mode,phase,key)
 audit[mode]=ds[0]
for phase in audit[0]['phases']:
 if phase!='aliases':
  for key in ('allocations','requested_bytes','live_requested','peak_live_requested','frees'):
   assert audit[0]['phases'][phase][key]==audit[1]['phases'][phase][key],(phase,key)
heads={i:json.loads((p/f'audit-{i}-0.heads.json').read_text()) for i in (0,1)}
rows=json.loads((p/'runs.json').read_text());stage={}
for mode in (0,1):
 profiles=[json.loads(f.read_text()) for f in p.glob(f'candidate-*-{mode}.profile.json')];assert len(profiles)>=5
 stage[mode]={key:statistics.median(d['stages'][key]['ms'] for d in profiles) for key in profiles[0]['stages']}
result=dict(audit={i:dict(allocations=sum(s['allocations'] for s in d['phases'].values()),requested_bytes=sum(s['requested_bytes'] for s in d['phases'].values()),peak_live_requested=d['heap_peak_live_requested'],live_end=d['heap_live_requested_end'],alias_allocations=d['phases']['aliases']['allocations'],alias_requested=d['phases']['aliases']['requested_bytes']) for i,d in audit.items()},heads=heads,normal_stage_medians_ms=stage)
(p/'analysis.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
