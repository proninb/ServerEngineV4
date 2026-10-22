"""Summarize observed costs; sampled times are estimates, never corrected truth."""
import csv
import hashlib
import json
import math
import pathlib
import statistics as st
import sys
from collections import defaultdict

root = pathlib.Path(sys.argv[1])
read = lambda p: list(csv.DictReader(p.open()))
quantile = lambda values, p: sorted(values)[max(0, math.ceil(len(values) * p) - 1)]
summary, record_times, dependency_runs = [], defaultdict(list), []
record_run_stats = []
for folder in sorted(root.glob('workers-*-fresh-*')):
    if not folder.is_dir():
        continue
    workers = int(folder.name.split('-')[1])
    rows = [r for r in read(folder / 'summary.csv') if r['warmup'] == '0']
    for r in rows:
        r['workers'] = workers
        summary.append(r)
    if workers != 16:
        continue
    run_costs = defaultdict(list)
    for r in read(folder / 'records.csv'):
        if r['trial'] != '0' and r['mode'] == '2':
            record_times[int(r['definition'])].append(float(r['inclusive_ms']))
            run_costs[r['trial']].append(float(r['inclusive_ms']))
    for costs in run_costs.values():
        record_run_stats.append(dict(sum_ms=sum(costs), median_ms=st.median(costs), p95_ms=quantile(costs, .95), p99_ms=quantile(costs, .99), max_ms=max(costs)))
    by_trial = defaultdict(list)
    for r in read(folder / 'dependencies.csv'):
        if r['trial'] != '0':
            by_trial[r['trial']].append(r)
    for rows in by_trial.values():
        # Mode 3 and 4 record the same users; de-duplicate observations.
        schemas = defaultdict(set)
        builds = {}
        accesses = {}
        for r in rows:
            key = int(r['schema'])
            schemas[key].add(int(r['definition']))
            builds[key] = int(r['builds'])
            accesses[key] = int(r['accesses'])
            assert builds[key] <= 1
        dependency_runs.append(dict(schemas=len(schemas), builds=sum(builds.values()), accesses=sum(accesses.values()), max_record_users=max(map(len, schemas.values()), default=0), multi_record_schemas=sum(len(users) > 1 for users in schemas.values())))

report = {'modes': [], 'coarse': {}, 'detailed_raw': {}, 'sampled_estimates': {}, 'records': {}, 'dependencies': dependency_runs}
for workers in (1, 16):
    for mode in range(5):
        group = [r for r in summary if r['workers'] == workers and r['mode'] == str(mode) and r['category'] == 'other']
        report['modes'].append(dict(workers=workers, mode=mode, n=len(group), evaluator_ms=st.median(float(r['evaluator_ms']) for r in group), lifecycle_ms=st.median(float(r['lifecycle_ms']) for r in group)))
for mode, key in ((1, 'coarse'), (3, 'detailed_raw'), (4, 'sampled_estimates')):
    for category in {r['category'] for r in summary}:
        group = [r for r in summary if r['workers'] == 16 and r['mode'] == str(mode) and r['category'] == category]
        scale = 32 if mode == 4 and category not in ('other', 'action_preparation', 'catalog_preparation', 'finalize') else 1
        costs = [scale * float(r['exclusive_ms']) for r in group]
        report[key][category] = dict(median_ms=st.median(costs), min_ms=min(costs), max_ms=max(costs), median_observed_hits=st.median(int(r['hits']) for r in group))

medians = {di: st.median(values) for di, values in record_times.items()}
values = list(medians.values())
report['records'] = dict(count=len(values), median_ms=st.median(values), p95_ms=quantile(values, .95), p99_ms=quantile(values, .99), max_ms=max(values), sum_of_record_medians_ms=sum(values), largest_fraction=max(values) / sum(values), granularity_lower_bounds_ms={str(n): max(sum(values) / n, max(values)) for n in (1, 2, 4, 8, 16)})
report['records']['median_of_run_statistics'] = {key: st.median(r[key] for r in record_run_stats) for key in record_run_stats[0]}
metadata = {int(r['definition']): r for r in read(root / 'workers-16-fresh-0/definitions.csv')}
owners = defaultdict(list)
for r in metadata.values():
    if int(r['complete']) != 2**64 - 1:
        owners[r['canonical_owner']].append(r['definition'])
report['duplicate_complete_owner_groups'] = sum(len(v) > 1 for v in owners.values())
report['complete_definitions'] = sum(map(len, owners.values()))
with (root / 'top-records.csv').open('w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=['definition', 'name', 'median_ms', 'file', 'offset', 'canonical_owner'])
    w.writeheader()
    for di, cost in sorted(medians.items(), key=lambda item: item[1], reverse=True)[:30]:
        r = metadata[di]
        w.writerow(dict(definition=di, name=r['name'], median_ms=cost, file=r['file'], offset=r['offset'], canonical_owner=r['canonical_owner']))
allocations = [r for r in read(root / 'allocations/summary.csv') if r['mode'] == '3' and r['warmup'] == '0']
report['allocations'] = {r['category']: dict(calls=int(r['allocation_calls']), bytes=int(r['allocation_bytes'])) for r in allocations}
bridge = [dict(field.split('=', 1) for field in line.split(',')) for line in (root / 'frozen-sc03-bridge.txt').read_text().splitlines()]
bridge = [r for r in bridge if r['variant'] == 'nested' and r['warmup'] == '0']
report['frozen_sc03_bridge'] = {k: st.median(float(r[k]) for r in bridge) for k in ('constructor_resolution_ms', 'lifecycle_ms')}
report['analysis_tool_sha256'] = hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()
(root / 'analysis.json').write_text(json.dumps(report, indent=2))
print(json.dumps(report['modes'], indent=2))
print(json.dumps(report['records'], indent=2))
