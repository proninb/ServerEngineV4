"""First coverage slice: paired projection/constructor lifecycle, no tuning."""
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
folders = ('header-semantic-coverage-01', 'header-chunk-02', 'header-chunk-01', 'semantic-bind-04', 'semantic-bind-03', 'semantic-bind-02', 'semantic-bind-01', 'canonicalizer-01')
sources = [p for folder in folders for p in (repo / 'experiments' / folder).glob('*') if p.suffix in ('.cpp', '.hpp', '.py', '.txt')]
executables = [build / 'Release' / (x + '.exe') for x in ('HeaderSemanticCoverage01', 'HeaderSemanticCoverage01Allocations')]
bridge = repo / 'build/header-chunk-02/results-final/HeaderChunk02.exe'
manifest = {'workers': [1, 16], 'target': 32768, 'fresh_processes_per_workers': 3, 'pairs_per_process': 5, 'excluded_warmup_pairs_per_process': 1, 'project': str(project), 'project_sha256': sha(project), 'sources': {str(p.relative_to(repo)): sha(p) for p in sources}, 'executables': {p.name: sha(p) for p in executables + [bridge]}, 'engine_objects': {p.name: sha(p) for p in (repo / 'build/semantic-bind-03/SemanticBind03Engine.dir/Release').glob('*.obj')}}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
for p in sources:
    target = output / 'sources' / p.relative_to(repo)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, target)
for p in executables + [bridge]:
    shutil.copy2(p, output / p.name)
rows = []
checksums = {}
for fresh in range(3):
    for workers in ((16, 1) if fresh % 2 == 0 else (1, 16)):
        p = subprocess.run([str(executables[0]), str(project), str(workers), '32768', '5'], capture_output=True, text=True)
        stem = output / f'run-{fresh}-workers-{workers}'
        stem.with_suffix('.stdout.txt').write_text(p.stdout)
        stem.with_suffix('.stderr.txt').write_text(p.stderr)
        assert p.returncode == 0, p.stdout + p.stderr
        for line in p.stdout.splitlines():
            row = dict(field.split('=', 1) for field in line.split(','))
            assert row['validation'] == 'passed' and row['oracle_accepted'] == '1'
            variant = row['variant']
            if variant in checksums:
                assert checksums[variant] == row['checksum']
            checksums[variant] = row['checksum']
            row['fresh_process'] = fresh
            rows.append(row)
        with (output / 'measurements.csv').open('w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
        print(f'fresh={fresh}, workers={workers}, parity=passed', flush=True)
summary = []
for workers in (1, 16):
    for variant in ('projection', 'constructors'):
        group = [r for r in rows if r['workers'] == str(workers) and r['variant'] == variant and r['warmup'] == '0']
        assert len(group) == 15
        summary.append(dict(variant=variant, workers=workers, **{k: statistics.median(float(r[k]) for r in group) for k in group[0] if k.endswith(('_ms', '_bytes'))}, lifecycle_min_ms=min(float(r['lifecycle_ms']) for r in group), lifecycle_max_ms=max(float(r['lifecycle_ms']) for r in group)))
(output / 'summary.json').write_text(json.dumps(summary, indent=2))
for workers in (1, 16):
    p = subprocess.run([str(executables[1]), str(project), str(workers), '32768', '1'], capture_output=True, text=True)
    (output / f'allocations-{workers}.txt').write_text(p.stdout + p.stderr)
    assert p.returncode == 0, p.stdout + p.stderr
    for line in p.stdout.splitlines():
        row = dict(field.split('=', 1) for field in line.split(','))
        assert row['requests_at_consumer'] == '0' and row['final_live_bytes'] == '0'
p = subprocess.run([str(bridge), str(project), '16', '32768', '5'], capture_output=True, text=True)
(output / 'frozen-hc02-bridge.txt').write_text(p.stdout + p.stderr)
assert p.returncode == 0, p.stdout + p.stderr
print('coverage_matrix=passed', flush=True)
