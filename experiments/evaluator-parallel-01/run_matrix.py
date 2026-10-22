"""Fixed frontend pool 16; paired sequential LAZY/evaluator N workers."""
import csv
import hashlib
import json
import pathlib
import shutil
import statistics
import subprocess
import sys

build, project, output = (pathlib.Path(x).resolve() for x in sys.argv[1:4])
output.mkdir(parents=True, exist_ok=False)
repo = pathlib.Path(__file__).resolve().parents[2]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
folders = ('evaluator-parallel-01', 'state-lazy-01', 'evaluator-isolation-01', 'header-semantic-coverage-03', 'header-semantic-coverage-02', 'header-semantic-coverage-01', 'header-chunk-02', 'header-chunk-01', 'semantic-bind-04', 'semantic-bind-03', 'semantic-bind-02', 'semantic-bind-01', 'canonicalizer-01')
sources = [p for folder in folders for p in (repo / 'experiments' / folder).glob('*') if p.suffix in ('.cpp', '.hpp', '.py', '.txt')]
executables = [build / 'Release' / (x + '.exe') for x in ('EvaluatorParallel01', 'EvaluatorParallel01Allocations')]
bridge = repo / 'build/state-lazy-01/results-lazy/StateLazy01.exe'
manifest = {'frontend_workers': 16, 'evaluator_workers': [1, 2, 4, 8, 16], 'pool_reused': True, 'target': 32768, 'fresh_processes_per_count': 3, 'pairs_per_process': 5, 'excluded_warmup_pairs_per_process': 1, 'project': str(project), 'project_sha256': sha(project), 'sources': {str(p.relative_to(repo)): sha(p) for p in sources}, 'executables': {p.name: sha(p) for p in executables + [bridge]}, 'engine_objects': {p.name: sha(p) for p in (repo / 'build/semantic-bind-03/SemanticBind03Engine.dir/Release').glob('*.obj')}}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
for p in sources:
    target = output / 'sources' / p.relative_to(repo)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, target)
for p in executables + [bridge]:
    shutil.copy2(p, output / p.name)
rows = []
checksum = None
orders = ((8, 1, 16, 2, 4), (4, 2, 16, 1, 8), (16, 8, 4, 2, 1))
for fresh, order in enumerate(orders):
    for workers in order:
        p = subprocess.run([str(executables[0]), str(project), str(workers), '32768', '5'], capture_output=True, text=True)
        stem = output / f'run-{fresh}-evaluator-{workers}'
        stem.with_suffix('.stdout.txt').write_text(p.stdout)
        stem.with_suffix('.stderr.txt').write_text(p.stderr)
        assert p.returncode == 0, p.stdout + p.stderr
        for line in p.stdout.splitlines():
            row = dict(field.split('=', 1) for field in line.split(','))
            assert row['validation'] == 'passed' and row['oracle_accepted'] == '1'
            assert row['frontend_workers'] == '16' and row['evaluator_workers'] == str(workers)
            assert row['prepared_schema_indices'] == '160' and row['covered_definitions'] == '24027'
            assert row['gap_definitions'] == '0' and row['reference_gaps'] == '0'
            if row['variant'] == 'parallel':
                assert row['dispatched_records'] == '24027'
                assert 1 <= int(row['physical_evaluator_workers']) <= workers
                assert 1 <= int(row['peak_active_lanes']) <= workers
            if checksum is None:
                checksum = row['checksum']
            assert checksum == row['checksum']
            row['evaluator_release_residual_ms'] = float(row['constructor_resolution_ms']) - sum(float(row[k]) for k in ('actions_ms', 'catalog_ms', 'schema_prepare_ms', 'local_evaluate_ms', 'ordered_commit_ms'))
            row['fresh_process'] = fresh
            rows.append(row)
        with (output / 'measurements.csv').open('w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
        print(f'fresh={fresh},evaluator_workers={workers},parity=passed', flush=True)
summary, paired = [], []
for workers in (1, 2, 4, 8, 16):
    for variant in ('sequential', 'parallel'):
        group = [r for r in rows if r['evaluator_workers'] == str(workers) and r['variant'] == variant and r['warmup'] == '0']
        assert len(group) == 15
        summary.append(dict(variant=variant, evaluator_workers=workers, **{k: statistics.median(float(r[k]) for r in group) for k in group[0] if k.endswith(('_ms', '_bytes'))}, lifecycle_min_ms=min(float(r['lifecycle_ms']) for r in group), lifecycle_max_ms=max(float(r['lifecycle_ms']) for r in group)))
    for metric in ('local_evaluate_ms', 'constructor_resolution_ms', 'lifecycle_ms'):
        differences = []
        for fresh in range(3):
            for trial in range(1, 6):
                pair = {r['variant']: r for r in rows if r['evaluator_workers'] == str(workers) and r['fresh_process'] == fresh and r['trial'] == str(trial)}
                differences.append(float(pair['parallel'][metric]) - float(pair['sequential'][metric]))
        paired.append(dict(evaluator_workers=workers, metric=metric, median_delta_ms=statistics.median(differences), min_delta_ms=min(differences), max_delta_ms=max(differences), faster_pairs=sum(d < 0 for d in differences), pairs=len(differences)))
(output / 'summary.json').write_text(json.dumps(summary, indent=2))
(output / 'paired-analysis.json').write_text(json.dumps(paired, indent=2))
for workers in (1, 2, 4, 8, 16):
    p = subprocess.run([str(executables[1]), str(project), str(workers), '32768', '1'], capture_output=True, text=True)
    (output / f'allocations-{workers}.txt').write_text(p.stdout + p.stderr)
    assert p.returncode == 0, p.stdout + p.stderr
    for line in p.stdout.splitlines():
        row = dict(field.split('=', 1) for field in line.split(','))
        assert row['requests_at_consumer'] == '0' and row['final_live_bytes'] == '0'
p = subprocess.run([str(bridge), str(project), '16', '32768', '5'], capture_output=True, text=True)
(output / 'frozen-state-lazy-bridge.txt').write_text(p.stdout + p.stderr)
assert p.returncode == 0, p.stdout + p.stderr
print('evaluator_parallel_matrix=passed', flush=True)
