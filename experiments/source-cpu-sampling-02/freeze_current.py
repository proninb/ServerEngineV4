"""Freeze the freshly built production-code V2 binary and its source identity."""
import hashlib
import json
import pathlib
import shutil
import subprocess
from symbols import verify

repo = pathlib.Path(__file__).resolve().parents[2]
root = repo / 'build/source-cpu-sampling-02'
source = root / 'current-v2/Release/ServerEngineV4PublishBenchmark.exe'
destination = root / 'frozen-current-v2'
identity = verify(source, source.with_suffix('.pdb'))
assert not destination.exists(), 'Frozen pair already exists; preserve it for reproducibility'
files = [repo / 'CMakeLists.txt', repo / 'benchmarks/publish_benchmark.cpp']
files += sorted((repo / 'server_engine').rglob('*.cpp'))
files += sorted((repo / 'server_engine').rglob('*.hpp'))
snapshot = {
    'description': 'Production V2: lazy Header state, 4x member-name index growth; no A/B or audit macros',
    'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
    'source_sha256': {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
    'cache': (root / 'current-v2/CMakeCache.txt').read_text(),
    'symbols': identity,
}
destination.mkdir()
for path in (source, source.with_suffix('.pdb')):
    shutil.copy2(path, destination / path.name)
(destination / 'build-snapshot.json').write_text(json.dumps(snapshot, indent=2))
assert verify(destination / source.name, destination / source.with_suffix('.pdb').name) == identity
print(destination)
