"""Native diagnostic runs; keep timer perturbation separate from SC03 control."""
import csv
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

build, project, output = [pathlib.Path(x).resolve() for x in sys.argv[1:]]
output.mkdir(parents=True, exist_ok=False)
repo = pathlib.Path(__file__).resolve().parents[2]
executables = [build / 'Release' / (n + '.exe') for n in ('EvaluatorScaling01', 'EvaluatorScaling01Allocations')]
bridge = repo / 'build/header-semantic-coverage-03/results-nested/HeaderSemanticCoverage03.exe'
sources = [p for folder in ('evaluator-scaling-01', 'header-semantic-coverage-03', 'header-chunk-01', 'semantic-bind-01', 'semantic-bind-02', 'canonicalizer-01') for p in (repo / 'experiments' / folder).glob('*') if p.suffix in ('.hpp', '.cpp', '.py', '.txt')]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
manifest = {'project': str(project), 'project_sha256': sha(project), 'sources': {str(p.relative_to(repo)): sha(p) for p in sources}, 'executables': {p.name: sha(p) for p in executables + [bridge]}, 'engine_objects': {p.name: sha(p) for p in (repo / 'build/semantic-bind-03/SemanticBind03Engine.dir/Release').glob('*.obj')}, 'workers': [16, 1], 'fresh_processes_16': 3, 'fresh_processes_1': 1, 'measured_trials_per_mode': 3, 'warmup_trials_per_mode': 1}
(output / 'manifest.json').write_text(json.dumps(manifest, indent=2))
for p in sources:
    target = output / 'sources' / p.relative_to(repo)
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(p, target)
for p in executables + [bridge]:
    shutil.copy2(p, output / p.name)
for workers, fresh in ((16, 0), (16, 1), (16, 2), (1, 0)):
    folder = output / f'workers-{workers}-fresh-{fresh}'
    p = subprocess.run([str(executables[0]), '--audit', str(project), str(workers), '3', '32768', str(folder)], capture_output=True, text=True)
    (output / f'{folder.name}.log').write_text(p.stdout + p.stderr)
    assert p.returncode == 0, p.stdout + p.stderr
    print(f'workers={workers},fresh={fresh},audit=passed', flush=True)
p = subprocess.run([str(executables[1]), '--audit', str(project), '16', '1', '32768', str(output / 'allocations')], capture_output=True, text=True)
(output / 'allocations.log').write_text(p.stdout + p.stderr)
assert p.returncode == 0, p.stdout + p.stderr
p = subprocess.run([str(bridge), str(project), '16', '32768', '5'], capture_output=True, text=True)
(output / 'frozen-sc03-bridge.txt').write_text(p.stdout + p.stderr)
assert p.returncode == 0, p.stdout + p.stderr
print('evaluator_audit=passed', flush=True)
