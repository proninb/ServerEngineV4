"""Serial fresh-process measurements; native execution required for timing."""
import csv
import hashlib
import json
import pathlib
import shutil
import statistics
import subprocess
import sys

build, project, output = (pathlib.Path(p).resolve() for p in sys.argv[1:4])
repeats = int(sys.argv[4]) if len(sys.argv) > 4 else 5
assert 3 <= repeats <= 30
output.mkdir(parents=True, exist_ok=False)
repo = pathlib.Path(__file__).resolve().parents[2]
configs = [(tag, workers) for workers in (1, 16) for tag in ('owning', 'shared03', 'arena04')]
executables = {tag: build / 'Release' / ('SemanticBind04.exe' if tag == 'arena04' else 'SemanticBind04Legacy.exe') for tag in ('owning', 'shared03', 'arena04')}
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
files = [p for directory in ('semantic-bind-04', 'semantic-bind-03', 'semantic-bind-01', 'semantic-bind-02', 'canonicalizer-01') for p in (repo / 'experiments' / directory).glob('*') if p.suffix in ('.cpp', '.hpp', '.py', '.txt')]
manifest = {'repeats': repeats, 'warmup_per_configuration': 1, 'project': str(project), 'project_sha256': sha(project), 'sources': {str(p.relative_to(repo)): sha(p) for p in files}, 'executables': {p.name: sha(p) for p in set(executables.values())}, 'engine_objects': {p.name: sha(p) for p in (repo / 'build/semantic-bind-03/SemanticBind03Engine.dir/Release').glob('*.obj')}}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
for p in files:
    target = output / 'sources' / p.relative_to(repo)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, target)
for p in set(executables.values()):
    shutil.copy2(p, output / p.name)
rows = []
checksums = None
for trial in range(-1, repeats):
    order = configs[trial % len(configs):] + configs[:trial % len(configs)]
    if trial % 2:
        order = order[::-1]
    for tag, workers in order:
        proc = subprocess.run([str(executables[tag]), str(project), 'baseline' if tag == 'owning' else 'combined', str(workers)], capture_output=True, text=True)
        name = f'{trial}-{tag}-{workers}'
        (output / f'{name}.stdout.txt').write_text(proc.stdout)
        (output / f'{name}.stderr.txt').write_text(proc.stderr)
        assert proc.returncode == 0, (name, proc.stdout, proc.stderr)
        row = dict(f.split('=', 1) for f in proc.stdout.strip().split(','))
        assert row['validation'] == 'passed' and row['oracle_accepted'] == '1'
        checksum = (row['requests'], row['linear_checksum'], row['shuffled_checksum'])
        if checksums is None:
            checksums = checksum
        assert checksum == checksums
        row.update(configuration=tag, trial=trial)
        rows.append(row)
        with (output / 'measurements.csv').open('w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
        print(f'{name}: lifecycle={row["lifecycle_ms"]} ms, model={row["model_ms"]} ms', flush=True)
summary = []
for tag, workers in configs:
    group = [r for r in rows if r['configuration'] == tag and int(r['workers']) == workers and int(r['trial']) >= 0]
    values = {key: statistics.median(float(r[key]) for r in group) for key in group[0] if key.endswith(('_ms', '_bytes'))}
    values.update(configuration=tag, workers=workers, lifecycle_min_ms=min(float(r['lifecycle_ms']) for r in group), lifecycle_max_ms=max(float(r['lifecycle_ms']) for r in group))
    summary.append(values)
(output / 'summary.json').write_text(json.dumps(summary, indent=2))
for workers in (1, 16):
    proc = subprocess.run([str(build / 'Release/SemanticBind04Allocations.exe'), '--audit', str(project), str(workers)], capture_output=True, text=True)
    (output / f'allocations-{workers}.txt').write_text(proc.stdout + proc.stderr)
    assert proc.returncode == 0, proc.stdout + proc.stderr
print(json.dumps(summary, indent=2))
