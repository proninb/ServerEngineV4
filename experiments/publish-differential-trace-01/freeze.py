"""Freeze provenance for local traces/profile; raw build artifacts stay out of Git."""
import hashlib
import json
import pathlib
import shutil
import sys

repo=pathlib.Path(sys.argv[1]).resolve()
trace=repo/'build/publish-differential-trace-01'
profile=repo/'build/publish-end-to-end-profile-01'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
paths=[repo/'CMakeLists.txt',repo/'benchmarks/publish_benchmark.cpp']
for folder in ('server_engine','experiments'):
    paths += [p for p in (repo/folder).rglob('*') if p.is_file() and p.suffix in ('.hpp','.cpp','.py','.txt')]
manifest={'sources':{p.relative_to(repo).as_posix():sha(p) for p in sorted(paths)},
          'imported_objects':{p.name:sha(p) for p in (repo/'build/semantic-bind-03/SemanticBind03Engine.dir/Release').glob('*.obj')},
          'executables':{}}
frozen=trace/'frozen';frozen.mkdir(exist_ok=True)
for path in (trace/'Release/PublishDifferentialTrace01.exe',profile/'Release/ServerEngineV4PublishBenchmark.exe'):
    manifest['executables'][path.relative_to(repo).as_posix()]=sha(path)
    shutil.copy2(path,frozen/path.name)
shutil.copy2(profile/'CMakeCache.txt',frozen/'production-CMakeCache.txt')
manifest['fixture_sha256']=sha(repo/'build/publish-subsecond-01/fixture/project.json')
(trace/'manifest.json').write_text(json.dumps(manifest,indent=2))
print('Frozen sources:',len(manifest['sources']),'engine objects:',len(manifest['imported_objects']))
