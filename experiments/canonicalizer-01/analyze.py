import csv,json,statistics as s,sys
from collections import defaultdict
from pathlib import Path
root=Path(sys.argv[1])
rows=list(csv.DictReader((root/'measurements.csv').open(encoding='utf-8-sig')))
groups=defaultdict(list);diagnostics={}
for row in rows:
    key=(row['dataset'],row['algorithm'],int(row['workers']))
    assert row['validation']=='passed' and row['producers']=='16'
    if row['diagnostic']=='1': diagnostics[key]=row
    else: groups[key].append(row)
summary=[]
for (dataset,algorithm,workers),sample in sorted(groups.items()):
    assert len(sample)==(7 if dataset=='unitproxl' else 5)
    row={'dataset':dataset,'algorithm':algorithm,'workers':workers,'samples':len(sample)}
    for key in ['canonical_ms','numbering_ms','kernel_ms','pool_start_ms','input_ms',
                'strings_canonical_ms','entities_canonical_ms']:
        values=[float(r[key]) for r in sample];median=s.median(values)
        row[key+'_median']=median
        row[key+'_mean']=s.mean(values)
        row[key+'_min']=min(values);row[key+'_max']=max(values)
        row[key+'_mad']=s.median(abs(v-median) for v in values)
    d=diagnostics[dataset,algorithm,workers]
    for key in ['tracked_peak_bytes','string_comparisons','comparison_span_bytes',
                'strings','identities','local_string_symbols','local_identity_symbols',
                'cross_worker_string_duplicates','cross_worker_identity_duplicates',
                'max_identity_depth','binding_queries','input_peak_ws_bytes','kernel_peak_ws_bytes']:
        row[key]=int(d[key])
    summary.append(row)
lookup={(r['dataset'],r['algorithm'],r['workers']):r for r in summary}
for r in summary:
    base=lookup[r['dataset'],r['algorithm'],1]
    r['kernel_speedup_vs_1']=base['kernel_ms_median']/r['kernel_ms_median']
for dataset in {r['dataset'] for r in summary}:
    same=[r for r in summary if r['dataset']==dataset]
    for key in ['strings','identities','local_string_symbols','local_identity_symbols',
                'cross_worker_string_duplicates','cross_worker_identity_duplicates','binding_queries','max_identity_depth']:
        assert len({r[key] for r in same})==1,(dataset,key)
with (root/'summary.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
(root/'summary.json').write_text(json.dumps(summary,indent=2))
tables=[]
for dataset in ['unitproxl','scopes','synthetic-flat','synthetic-nested']:
    tables += [f'## {dataset}', '', '| Algorithm | 1 | 2 | 4 | 8 | 16 |', '| --- | ---: | ---: | ---: | ---: | ---: |']
    for algorithm in ['sort','index','hierarchical']:
        tables.append('| '+algorithm+' | '+' | '.join(f"{lookup[dataset,algorithm,n]['kernel_ms_median']:.3f}" for n in [1,2,4,8,16])+' |')
    tables+=['','Times are median canonicalization + common numbering, ms; no diagnostic counters.','']
(root/'tables.md').write_text('\n'.join(tables))
print('\n'.join(tables))
print(f'measured_runs={sum(len(v) for v in groups.values())}, diagnostic_runs={len(diagnostics)}, all_validation=passed')
