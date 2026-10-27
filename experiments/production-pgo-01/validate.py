"""Cold lifecycle, native SHM and semantic/artifact parity in real servers."""
import hashlib,json,shutil,sys
from pathlib import Path
from client import Server
from cold import native_payload
repo=Path(__file__).resolve().parents[2];root=Path(sys.argv[1]).resolve();results={}
def images(project):
 result={}
 for name in ('compiled.bin','runtime.bin'):
  with (project.parent/'.serverengine'/project.name/name).open('rb') as f:result[name]=hashlib.file_digest(f,'sha256').hexdigest()
 return result
for arm in ('ordinary','pgo'):
 session=root/'sessions'/('validate-'+arm);session.mkdir(parents=True,exist_ok=True)
 for f in ('server.license','server.lease'):shutil.copy2(repo/f,session/f)
 records={}
 with Server(root/'bin'/arm/'ServerEngineV4.exe',session) as server:
  for name in ('unit','simple','mixed','lifecycle'):
   project=root/'fixtures'/name/'project.json';server.request('PUBLISH',path=str(project))
   record=dict(images=images(project),native=native_payload(server.name,project.parent/'.serverengine/project.json/runtime.bin'))
   if name=='lifecycle':
    record['values']={field:server.request('GET_VALUE',name=field)[0]['payload'] for field in ('b.x','t.ref.x','t.mode')}
    assert [v['bits'] for v in record['values'].values()]==[7,7,2]
    record['links']=server.request('GET_LINK',objects=['t'])[0]['payload']
   server.request('UNLOAD');server.request('LOAD',path=str(project));assert server.request('GET_STATE')[0]['payload']['state']=='LOADED'
   assert native_payload(server.name,project.parent/'.serverengine/project.json/runtime.bin')==record['native']
   server.request('RUN');assert server.request('GET_STATE')[0]['payload']['state']=='RUN';server.request('FREEZE');assert server.request('GET_STATE')[0]['payload']['state']=='FREEZE';server.request('UNLOAD')
   records[name]=record;print(arm,name,'PUBLISH/LOAD/RUN/FREEZE/native PASS',flush=True)
  project=root/'fixtures/legacy/project.json';source=project.parent/'source.cpp';source.write_text('I a; a.x=7;')
  try:
   server.request('REBUILD',path=str(project));assert server.request('GET_VALUE',name='a.x')[0]['payload']['bits']==7
   records['rebuild']=dict(images=images(project),native=native_payload(server.name,project.parent/'.serverengine/project.json/runtime.bin'));server.request('UNLOAD')
   server.request('BUILD',path=str(project));assert server.request('GET_VALUE',name='a.x')[0]['payload']['bits']==7
   records['build-nochange']=dict(images=images(project),native=native_payload(server.name,project.parent/'.serverengine/project.json/runtime.bin'));server.request('UNLOAD')
   source.write_text('I a; a.x=9;')
   response,_=server.request('BUILD',path=str(project),allow_failure=True)
   assert response['status'] is False and any(d['id']==2007 and 'semantic-changing BUILD is deferred' in d['detail'] for d in response['diagnostics'])
   server.request('LOAD',path=str(project));assert server.request('GET_VALUE',name='a.x')[0]['payload']['bits']==7
   records['build-change']=dict(expected_failure=response['diagnostics'],images=images(project),native=native_payload(server.name,project.parent/'.serverengine/project.json/runtime.bin'));server.request('UNLOAD')
  finally:source.write_text('I a; a.x=7;')
  print(arm,'REBUILD/nochange BUILD/expected deferred-change rejection + preserved LOAD PASS',flush=True)
 results[arm]=records
 (root/'validation.json').write_text(json.dumps(results,indent=2))
assert results['ordinary']==results['pgo'],'Production semantic/artifact/native parity failed'
print('All production lifecycle and native payload parity PASS')
