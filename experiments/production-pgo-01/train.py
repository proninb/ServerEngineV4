"""Exercise real production ServerEngineV4 over localhost; PGC on normal exits."""
import hashlib,json,shutil,sys
from pathlib import Path
from client import Server
repo=Path(__file__).resolve().parents[2];root=Path(sys.argv[1]).resolve();exe=Path(sys.argv[2]).resolve();rounds=int(sys.argv[3]) if len(sys.argv)>3 else 3
rows=[]
for i in range(rounds):
 session=root/'sessions'/('train'+str(i));session.mkdir(parents=True,exist_ok=True)
 for f in ('server.license','server.lease'):shutil.copy2(repo/f,session/f)
 with Server(exe,session) as server:
  for name in ('unit','simple','mixed','lifecycle'):
   project=root/'fixtures'/name/'project.json';response,elapsed=server.request('PUBLISH',path=str(project));print(i,name,round(elapsed,2),flush=True)
   assert server.request('GET_STATE')[0]['payload']['state']=='LOADED'
   if name=='lifecycle':
    for field,bits in [('b.x',7),('t.ref.x',7),('t.mode',2)]:assert server.request('GET_VALUE',name=field)[0]['payload']['bits']==bits
    server.request('RUN');assert server.request('GET_STATE')[0]['payload']['state']=='RUN';server.request('FREEZE')
   server.request('UNLOAD');server.request('LOAD',path=str(project));server.request('UNLOAD')
   rows.append(dict(round=i,project=name,publish_request_ms=elapsed))
  project=root/'fixtures/legacy/project.json';server.request('REBUILD',path=str(project));assert server.request('GET_VALUE',name='a.x')[0]['payload']['bits']==7;server.request('UNLOAD');server.request('BUILD',path=str(project));assert server.request('GET_VALUE',name='a.x')[0]['payload']['bits']==7;server.request('UNLOAD')
(root/'training.json').write_text(json.dumps(rows,indent=2))
