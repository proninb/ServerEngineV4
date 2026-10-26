"""Quality gates and uncertainty notes, computed only after timed execution."""
import json,statistics,sys
from pathlib import Path
p=Path(sys.argv[1]);summary=json.loads((p/'summary.json').read_text());rows=json.loads((p/'samples.json').read_text())
ratios=[];residual=[];rare=[];profiles=[]
for i in range(15):
 d=json.loads((p/f'{i}-sample.json').read_text());profiles.append(d);q=summary['edge_profiles'][i]
 ratios.append((q['root_regular_ms']+q['rare_exact_ms'])/q['apply_file_ms']);rare.append(q['rare_exact_ms'])
 row=next(r for r in rows if r['label']==str(i) and r['arm']=='sample')
 residual.append(row['pls08_linking_ms']-sum(d['tags'][k]['exact_ms'] for k in ('queue_wait','apply_file','model_cleanup','root_provenance')))
def interval(values):return dict(minimum=min(values),median=statistics.median(values),maximum=max(values))
quality=dict(
 observer_modes='baseline=C5; off=0; counters=1; independent sampling=3',
 raw_legacy_exclusive_field='unused in mode 3; use edge estimates, including negative/unresolved values',
 exact_partition=False,
 sample_root_budget_ratio=interval(ratios),
 ordered_unattributed_ms=interval(residual),
 exact_rare_growth_ms=interval(rare),
 clock_positive_step_ns=interval([d['clock_positive_step_ns'] for d in profiles]),
 clock_pair_mean_ns=interval([d['clock_pair_mean_ns'] for d in profiles]),
 negative_median_exclusive=[k for k,v in summary['breakdown'].items() if v['edge_exclusive_ms']['median']<0],
 limitations=['wall-clock scope estimates, not ETW CPU samples','observer overhead is non-negligible; do not subtract estimates from production time','parent-child differences near timer resolution are not identifiable','temporary target instances are source-level construction sites, not allocation counts','exact cleanup refers to consumed models and final committer, not all PUBLISH/SHM cleanup'])
(p/'quality.json').write_text(json.dumps(quality,indent=2));print(json.dumps(quality,indent=2))
