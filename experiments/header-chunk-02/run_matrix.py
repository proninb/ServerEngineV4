"""Paired whole-root/chunk lifecycle, including discovery and ordered assembly."""
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
sources = [p for folder in ('header-chunk-02', 'header-chunk-01', 'semantic-bind-04', 'semantic-bind-03', 'semantic-bind-02', 'semantic-bind-01', 'canonicalizer-01') for p in (repo / 'experiments' / folder).glob('*') if p.suffix in ('.cpp', '.hpp', '.py', '.txt')]
executables = [build / 'Release' / (name + '.exe') for name in ('HeaderChunk02', 'HeaderChunk02Allocations')]
manifest = {'workers': [1, 2, 4, 8, 16], 'targets': [1024, 8192, 32768], 'repeats': 5, 'warmup_per_configuration': 1, 'project': str(project), 'project_sha256': sha(project), 'sources': {str(p.relative_to(repo)): sha(p) for p in sources}, 'executables': {p.name: sha(p) for p in executables}, 'engine_objects': {p.name: sha(p) for p in (repo / 'build/semantic-bind-03/SemanticBind03Engine.dir/Release').glob('*.obj')}}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
for p in sources:
    target = output / 'sources' / p.relative_to(repo)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, target)
for p in executables:
    shutil.copy2(p, output / p.name)
rows = []
checksum = None
for index, workers in enumerate((1, 2, 4, 8, 16)):
    targets = (8192, 1024, 32768) if index % 2 == 0 else (32768, 1024, 8192)
    for target in targets:
        proc = subprocess.run([str(executables[0]), str(project), str(workers), str(target), '5'], capture_output=True, text=True)
        stem = output / f'workers-{workers}-target-{target}'
        stem.with_suffix('.stdout.txt').write_text(proc.stdout)
        stem.with_suffix('.stderr.txt').write_text(proc.stderr)
        assert proc.returncode == 0, (stem, proc.stdout, proc.stderr)
        for line in proc.stdout.splitlines():
            row = dict(field.split('=', 1) for field in line.split(','))
            assert row['validation'] == 'passed' and row['fallback_roots'] == '0' and row['oracle_accepted'] == '1'
            if checksum is None:
                checksum = row['checksum']
            assert row['checksum'] == checksum
            rows.append(row)
        with (output / 'measurements.csv').open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        print(f'workers={workers}, target={target}, validation=passed', flush=True)
summary = []
for workers in (1, 2, 4, 8, 16):
    for target in (1024, 8192, 32768):
        for variant in ('sb04', 'chunk'):
            group = [r for r in rows if r['workers'] == str(workers) and r['target'] == str(target) and r['variant'] == variant and r['warmup'] == '0']
            assert len(group) == 5
            values = {key: statistics.median(float(r[key]) for r in group) for key in group[0] if key not in ('validation', 'checksum', 'variant')}
            values.update(variant=variant, lifecycle_min_ms=min(float(r['lifecycle_ms']) for r in group), lifecycle_max_ms=max(float(r['lifecycle_ms']) for r in group))
            summary.append(values)
(output / 'summary.json').write_text(json.dumps(summary, indent=2))
for workers in (1, 16):
    for target in (1024, 8192, 32768):
        proc = subprocess.run([str(executables[1]), str(project), str(workers), str(target), '1'], capture_output=True, text=True)
        (output / f'allocations-{workers}-{target}.txt').write_text(proc.stdout + proc.stderr)
        assert proc.returncode == 0, proc.stdout + proc.stderr
        for line in proc.stdout.splitlines():
            row = dict(field.split('=', 1) for field in line.split(','))
            assert row['requests_at_consumer'] == '0' and row['final_live_bytes'] == '0'
print('matrix_and_allocations=passed', flush=True)
