"""Targeted unchanged-image UnitProXL PUBLISH immediately after UNLOAD."""
import hashlib,json,os,statistics,sys,time
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
for p in ('production-pgo-01','tcp-reliability-01','tcp-pgo-release-02','heap-optimize-policy-15'):
 sys.path.append(str(REPO/'experiments'/p))
from integration import PreloginServer,session
from cold import native_payload
from validate_helpers import images
from run_soak import memory

def stats(v):
 v=sorted(v)
 return dict(n=len(v),median=statistics.median(v),p95=v[min(len(v)-1,int(.95*len(v)))]) if v else dict(n=0)
def main():
 root=REPO/'build/heap-direct-publish';root.mkdir(exist_ok=False)
 exe=REPO/'build/heap-optimize-policy-15/compiler/Release/ServerEngineV4.exe'
 digest=hashlib.sha256(exe.read_bytes()).hexdigest();assert digest=='87d8096a00658457ebafebc21477dabbf20cae3fe0084280ea2cbdc6932226af'
 bundle=REPO/'build/tcp-pgo-release-02';project=bundle/'fixtures/unit/project.json';runtime=project.parent/'.serverengine/project.json/runtime.bin'
 gold=json.loads((bundle/'validation.json').read_text())['ordinary']['unit'];summary=[]
 protocol=dict(order=['OFF_','POL_','POL_','OFF_']*2,warmup=3,measured=12,executable_sha256=digest,threshold_bytes=120635392,performance_acceptance='NONE; measurement only',scope='Targeted latency; not replacement 600-second memory gate')
 (root/'protocol.json').write_text(json.dumps(protocol,indent=2))
 for n,mode in enumerate(protocol['order'],1):
  os.environ['SE_HEAP_OPT15']=mode;out=root/f'run-{n:02}-{mode}';out.mkdir();records=[]
  with PreloginServer(exe,session(out,'server')) as server:
   server.request('LOGIN',name='DirectPublish');descriptor=server.socket.fileno();peer=server.socket.getpeername();previous=None
   for i in range(15):
    before=memory(server.process);wall=time.time_ns();start=time.monotonic_ns()
    _,ms=server.request('PUBLISH',path=str(project));end=time.monotonic_ns();after=memory(server.process)
    assert images(project)==gold['images'];assert native_payload(server.name,runtime)==gold['native']
    # No LOAD, other fixture or reconnect between UNLOAD and next PUBLISH.
    _,unload_ms=server.request('UNLOAD')
    assert server.request('GET_STATE')[0]['payload']['state']=='UNLOADED'
    assert server.socket.fileno()==descriptor and server.socket.getpeername()==peer
    telemetry=[json.loads(x) for x in (out/'server/heap-optimize.jsonl').read_text().splitlines()]
    assert len(telemetry)==i+1;current=telemetry[-1]
    assert current['unload']==i+1 and current['mode']==mode and current['called']==(mode=='POL_' and current['before_private']>=120635392)
    assert not current['called'] or current['success']
    row=dict(index=i,measured=i>=3,publish_ms=ms,unload_ms=unload_ms,wall_start_ns=wall,monotonic_start_ns=start,monotonic_end_ns=end,publish_faults=(after['page_faults']-before['page_faults'])&0xffffffff,quiescent=memory(server.process),previous_unload=previous,current_unload=current)
    with (out/'samples.jsonl').open('a') as f:f.write(json.dumps(row)+'\n')
    records.append(row);previous=current
   server.request('GET_STATE')
  measured=[r for r in records if r['measured']]
  result=dict(run=out.name,mode=mode,publish_ms=stats([r['publish_ms'] for r in measured]),faults=stats([r['publish_faults'] for r in measured]),after_cleanup=stats([r['publish_ms'] for r in measured if r['previous_unload']['called']]),without_cleanup=stats([r['publish_ms'] for r in measured if not r['previous_unload']['called']]),api_ms_sum=sum(r['current_unload']['ticks']/r['current_unload']['frequency']*1000 for r in measured if r['current_unload']['called']))
  summary.append(result);(out/'status.json').write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
 assert hashlib.sha256(exe.read_bytes()).hexdigest()==digest
 (root/'comparison.json').write_text(json.dumps(dict(status='COMPLETE / MEASUREMENT ONLY / NO ADOPTION',runs=summary),indent=2))
if __name__=='__main__':main()
