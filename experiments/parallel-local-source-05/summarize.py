"""Export simultaneous ownership and baseline costs from a completed run."""
import csv,json,statistics,sys
from pathlib import Path
p=Path(sys.argv[1]);d=json.loads((p/'audit-oracle-0.ownership.json').read_text());reverse=json.loads((p/'audit-oracle-1.ownership.json').read_text())
for key in ('alias_peak_live_requested','tags','cell_role_masks','output_roots','unrooted_cells','path_steps','all_cell_path_steps','output_path_steps','live_write_records','live_binding_records'):
 assert d[key]==reverse[key],key
tags=list(d['tags'])
with (p/'peak-ownership.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['owner','bytes_at_same_alias_peak','allocations','cumulative_requested_bytes','owner_individual_peak'])
 for k,v in d['tags'].items():w.writerow([k,v['at_alias_peak'],v['allocations'],v['requested_bytes'],v['peak_live']])
with (p/'checkpoints.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['stage','live_payload','audit_working_set','cells','values',*tags])
 for c in d['checkpoints']:w.writerow([c['stage'],sum(c['live_by_tag']),c['working_set'],c['cells'],c['values'],*c['live_by_tag']])
profiles=[json.loads(f.read_text()) for f in p.glob('observer-*-0.profile.json')];assert len(profiles)>=5
baseline={k:statistics.median(d['stages'][k]['ms'] for d in profiles) for k in profiles[0]['stages']}
result=dict(simultaneous_alias_peak_bytes=d['alias_peak_live_requested'],groups_at_peak={prefix:sum(v['at_alias_peak'] for k,v in d['tags'].items() if k.startswith(prefix)) for prefix in ('bound.','scratch.','output.')},
 other_at_peak=d['tags']['other']['at_alias_peak'],normal_phase_medians_ms=baseline,output_roles=d['cell_role_masks'],unrooted_cells=d['unrooted_cells'],
 bound_logical_bytes=d['checkpoints'][-1]['bound_size'],bound_capacity=d['checkpoints'][-1]['bound_capacity'])
(p/'analysis.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
