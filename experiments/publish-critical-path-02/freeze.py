"""Freeze a complete current production build; never overwrite historical binaries."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

repo = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'experiments/source-cpu-sampling-02'))
from symbols import verify

root = repo / 'build/publish-critical-path-02'
build = root / 'symbols'
exe = build / 'Release/ServerEngineV4PublishBenchmark.exe'
destination = root / 'frozen'
cache_text = (build / 'CMakeCache.txt').read_text()
cache = {}
for line in cache_text.splitlines():
    if line and not line.startswith(('#', '//')) and ':' in line and '=' in line:
        name, value = line.split('=', 1)
        cache[name.split(':', 1)[0]] = value
required = {
    'SERVER_ENGINE_ENABLE_IPO': 'ON',
    'SERVER_ENGINE_PUBLISH_NO_SHA256_AB': 'OFF',
    'SERVER_ENGINE_HEADER_STATE_LAZY_AB': 'OFF',
    'SERVER_ENGINE_CANONICALIZER_TRACE': 'OFF',
    'SERVER_ENGINE_HEADER_SOURCE_OVERLAP_V2': 'OFF',
    'SERVER_ENGINE_SOURCE_LEXER_TOKENS_V2': 'ON',
    'SERVER_ENGINE_SOURCE_PREHASH_PARALLEL_V2': 'ON',
    'SERVER_ENGINE_SOURCE_STRING_BATCH_V2': 'ON',
    'SERVER_ENGINE_SOURCE_STRING_PATH_REUSE_V2': 'ON',
}
for name, value in required.items():
    assert cache.get(name) == value, (name, cache.get(name), value)
assert '/Zi' in cache['CMAKE_CXX_FLAGS_RELEASE'] and '/O2' in cache['CMAKE_CXX_FLAGS_RELEASE']
assert '/DEBUG:FULL' in cache['CMAKE_EXE_LINKER_FLAGS_RELEASE']
tree = ET.parse(build / 'ServerEngineV4PublishBenchmark.vcxproj')
ns = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
release = [n for n in tree.findall('m:ItemDefinitionGroup', ns) if "'Release|x64'" in n.get('Condition', '')]
assert len(release) == 1
definitions = release[0].find('m:ClCompile/m:PreprocessorDefinitions', ns).text
assert definitions is not None
for forbidden in ('_AUDIT', '_AB=', 'CW_CANONICALIZER_TRACE', 'CW_HEADER_MODEL', 'CW_SOURCE_MODEL'):
    assert forbidden not in definitions, (forbidden, definitions)
identity = verify(exe, exe.with_suffix('.pdb'))
binary = exe.read_bytes()
for marker in ('CW_PUBLISH_SHA256_REFERENCE', 'CW_RUNTIME_RANGES_REFERENCE',
               'CW_EARLY_RELEASE_REFERENCE', 'CW_GRAPH_LINK_INDEX_REFERENCE',
               'CW_STRING_INTERN_SLOT_REFERENCE', 'CW_HEADER_STATE_EAGER'):
    assert (marker + '\0').encode() not in binary, marker
sources = [repo / 'CMakeLists.txt', repo / 'benchmarks/publish_benchmark.cpp']
sources += sorted((repo / 'server_engine').rglob('*.cpp'))
sources += sorted((repo / 'server_engine').rglob('*.hpp'))
snapshot = {
    'experiment': 'PUBLISH-CRITICAL-PATH-02',
    'description': 'Production V2: no acquisition SHA-256; lazy Header state; member index 4x; runtime memo and early context release disabled',
    'git_head': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
    'source_sha256': {str(p.relative_to(repo)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
    'cache': cache_text, 'release_definitions': definitions, 'symbols': identity,
}
destination.mkdir(exist_ok=False)
for path in (exe, exe.with_suffix('.pdb')):
    shutil.copy2(path, destination / path.name)
(destination / 'build-snapshot.json').write_text(json.dumps(snapshot, indent=2))
assert verify(destination / exe.name, destination / exe.with_suffix('.pdb').name) == identity
print(destination)
