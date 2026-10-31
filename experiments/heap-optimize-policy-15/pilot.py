import json,os,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
for directory in ('production-pgo-01','tcp-reliability-01','tcp-pgo-release-02'):sys.path.append(str(REPO/'experiments'/directory))
from integration import PreloginServer,session
from cold import native_payload
from validate_helpers import images
def main():
 root=REPO/'build/heap-optimize-policy-15';bundle=REPO/'build/tcp-pgo-release-02';exe=root/'compiler/Release/ServerEngineV4.exe'
 manifest=json.loads((bundle/'holdout/manifest.json').read_text());golds=json.loads((bundle/'holdout-results/summary.json').read_text());validation=json.loads((bundle/'validation.json').read_text())
 cases={name:{**data,'gold':golds['cases'][name]['gold']} for name,data in manifest.items()}
 cases['unit']=dict(project=str(bundle/'fixtures/unit/project.json'),expected={},gold={k:validation['ordinary']['unit'][k] for k in ('images','native')})
 result={}
 for mode in ('OFF_','EACH','POL_'):
  os.environ['SE_HEAP_OPT15']=mode;out=root/('pilot-'+mode);out.mkdir(exist_ok=False)
  with PreloginServer(exe,session(out,'server')) as server:
   server.request('LOGIN',name='HeapOptimizePilot');descriptor=server.socket.fileno()
   for name,case in cases.items():
    project=Path(case['project']);runtime=project.parent/'.serverengine/project.json/runtime.bin'
    server.request('PUBLISH',path=str(project));assert images(project)==case['gold']['images'];assert native_payload(server.name,runtime)==case['gold']['native']
    for field,value in case['expected'].items():assert server.request('GET_VALUE',name=field)[0]['payload']['bits']==value
    server.request('RUN');server.request('FREEZE');server.request('UNLOAD');assert server.request('GET_STATE')[0]['payload']['state']=='UNLOADED'
    server.request('LOAD',path=str(project));assert native_payload(server.name,runtime)==case['gold']['native'];server.request('UNLOAD')
    assert server.socket.fileno()==descriptor
  rows=[json.loads(x) for x in (out/'server/heap-optimize.jsonl').read_text().splitlines()]
  assert len(rows)==2*len(cases)
  assert all(r['selector_valid'] and r['before_ok'] and r['after_ok'] and r['timer_ok'] and r['called']==(mode=='EACH' or (mode=='POL_' and r['before_private']>=120635392)) for r in rows)
  assert all(r['success'] and r['error']==0 for r in rows if r['called'])
  result[mode]=dict(cases=list(cases),unloads=len(rows),same_TCP=True,api_successes=sum(r['success'] for r in rows),private_reduction_bytes=[r['before_private']-r['after_private'] for r in rows])
  print(mode,result[mode],flush=True)
 (root/'pilot.json').write_text(json.dumps(result,indent=2))
if __name__=='__main__':main()
