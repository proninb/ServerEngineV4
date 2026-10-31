import hashlib,json
from pathlib import Path
root=Path('build/unload-heap-release-validation')
expected=json.loads(Path('build/heap-direct-publish/verification.json').read_text())['frozen_reference_hashes_verified']
expected.update({'build/heap-integration-candidate/on/Release/ServerEngineV4.exe':'a4c3e005998e463b66aefd4e9c96ac6e67274f14657a46e293687af51a4282d9','build/heap-integration-candidate/off/Release/ServerEngineV4.exe':'111fbe96e7eefaad677e488667a448d2da75df5ab56bc2e185568b17669123b3'})
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in expected.items())
manifest=json.loads(Path('build/project-unload-memory-01/source-manifest.json').read_text())
assert all(hashlib.sha256((Path('build/project-unload-memory-01/source')/p).read_bytes()).hexdigest()==h for p,h in manifest.items())
for mode,setting in [('off','OFF'),('on','ON')]:
 cache=Path(f'build/heap-integration-candidate/{mode}/CMakeCache.txt').read_text()
 assert f'SERVER_ENGINE_UNLOAD_HEAP_OPTIMIZATION:BOOL={setting}' in cache
 assert 'SERVER_ENGINE_PRODUCTION_PGO:STRING=OFF' in cache
cmake=Path('CMakeLists.txt').read_text();assert '"Inactive candidate: optimize process LFH caches after high-commit UNLOAD" OFF)' in cmake
result=dict(status='PASS',hashes=expected,frozen_source_files_verified=len(manifest),default='OFF',pgo='OFF')
with (root/'verification.json').open('x') as f:json.dump(result,f,indent=2)
print('PASS: exact candidate, frozen references, 287 frozen source files, default OFF and PGO OFF')
