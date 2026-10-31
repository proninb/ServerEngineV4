import hashlib,json
from pathlib import Path

root=Path('build/heap-optimize-policy-15')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads(Path('build/project-unload-memory-01/source-manifest.json').read_text())
frozen=Path('build/project-unload-memory-01/source')
assert all(sha(frozen/p)==h for p,h in manifest.items())
changed=[p for p,h in manifest.items() if sha(root/'source'/p)!=h]
assert changed==['server_engine/server.cpp'],changed
expected={
'build/tcp-pgo-release-02/ab-01/binaries/ordinary/ServerEngineV4.exe':'6cd312cbc030765718ad0e2fb847723d97efc0395455052cc8a8522843cc3429',
'build/tcp-pgo-release-02/ab-01/binaries/pgo/ServerEngineV4.exe':'509373908a081d913876445ab4c09f54dd072d4e23c16e6fc158b42d1720b554',
'build/tcp-pgo-release-02/compiler/production.pgd':'b3a6fe175e377900959fab72754ee8ffed109b942da09a6fbf93b3feb545a11a',
'build/heap-optimize-policy-15/compiler/Release/ServerEngineV4.exe':'87d8096a00658457ebafebc21477dabbf20cae3fe0084280ea2cbdc6932226af'}
actual={p:sha(Path(p)) for p in expected}
assert actual==expected,actual
result=dict(status='PASS',frozen_source_files_verified=len(manifest),diagnostic_source_changed=changed,hashes=actual)
with (root/'verification.json').open('x') as f:json.dump(result,f,indent=2)
print(json.dumps(result,indent=2))
