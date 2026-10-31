import json,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
for p in ('production-pgo-01','tcp-reliability-01','tcp-pgo-release-02'):sys.path.append(str(REPO/'experiments'/p))
from integration import PreloginServer,session
from cold import native_payload
from validate_helpers import images
def main():
 root=REPO/'build/heap-integration-candidate';bundle=REPO/'build/tcp-pgo-release-02'
 manifest=json.loads((bundle/'holdout/manifest.json').read_text());golds=json.loads((bundle/'holdout-results/summary.json').read_text());validation=json.loads((bundle/'validation.json').read_text())
 cases={name:{**data,'gold':golds['cases'][name]['gold']} for name,data in manifest.items()}
 cases['unit']=dict(project=str(bundle/'fixtures/unit/project.json'),expected={},gold=validation['ordinary']['unit'])
 result={}
 for mode in ('off','on'):
  out=root/('parity-'+mode);out.mkdir(exist_ok=False)
  with PreloginServer(root/mode/'Release/ServerEngineV4.exe',session(out,'server')) as server:
   server.request('LOGIN',name='CandidateParity');descriptor=server.socket.fileno();peer=server.socket.getpeername()
   for name,case in cases.items():
    p=Path(case['project']);runtime=p.parent/'.serverengine/project.json/runtime.bin'
    server.request('PUBLISH',path=str(p));assert images(p)==case['gold']['images'];assert native_payload(server.name,runtime)==case['gold']['native']
    for field,value in case['expected'].items():assert server.request('GET_VALUE',name=field)[0]['payload']['bits']==value
    server.request('RUN');server.request('FREEZE');response,_=server.request('UNLOAD');assert response['status'] is True
    assert server.request('GET_STATE')[0]['payload']['state']=='UNLOADED'
    server.request('LOAD',path=str(p));assert native_payload(server.name,runtime)==case['gold']['native'];server.request('UNLOAD')
    assert server.socket.fileno()==descriptor and server.socket.getpeername()==peer
   # Consecutive heavy publications exercise immediate reuse in integrated ON image.
   for i in range(3):
    server.request('PUBLISH',path=cases['unit']['project']);server.request('UNLOAD');assert server.request('GET_STATE')[0]['payload']['state']=='UNLOADED'
  result[mode]=dict(status='PASS',cases=list(cases),same_TCP=True)
 (root/'parity.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
if __name__=='__main__':main()
