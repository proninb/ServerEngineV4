import hashlib,json
from pathlib import Path
root=Path('build/heap-direct-publish');count=0
for out in sorted(root.glob('run-*')):
 samples=[json.loads(x) for x in (out/'samples.jsonl').read_text().splitlines()]
 assert len(samples)==15 and [r['index'] for r in samples]==list(range(15))
 for i,r in enumerate(samples):
  t=r['current_unload'];assert t['unload']==i+1 and t['selector_valid'] and t['before_ok'] and t['after_ok'] and t['timer_ok']
  assert not t['called'] or (t['success'] and t['error']==0)
  assert r['previous_unload']==(samples[i-1]['current_unload'] if i else None)
  if i and r['measured']:
   gap=(r['monotonic_start_ns']-samples[i-1]['monotonic_start_ns'])/1e9
   assert 0<gap<60,gap
  count+=int(r['measured'])
expected={
'build/tcp-pgo-release-02/ab-01/binaries/ordinary/ServerEngineV4.exe':'6cd312cbc030765718ad0e2fb847723d97efc0395455052cc8a8522843cc3429',
'build/tcp-pgo-release-02/ab-01/binaries/pgo/ServerEngineV4.exe':'509373908a081d913876445ab4c09f54dd072d4e23c16e6fc158b42d1720b554',
'build/tcp-pgo-release-02/compiler/production.pgd':'b3a6fe175e377900959fab72754ee8ffed109b942da09a6fbf93b3feb545a11a',
'build/heap-optimize-policy-15/compiler/Release/ServerEngineV4.exe':'87d8096a00658457ebafebc21477dabbf20cae3fe0084280ea2cbdc6932226af'}
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in expected.items())
result=dict(status='PASS',measured_PUBLISH=count,raw_timing_and_previous_UNLOAD_links_verified=True,frozen_reference_hashes_verified=expected)
with (root/'verification.json').open('x') as f:json.dump(result,f,indent=2)
print(json.dumps(result,indent=2))
