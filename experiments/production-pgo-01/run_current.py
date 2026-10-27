"""Real production request-latency A/B; no benchmark main or hot observers."""
import ctypes,hashlib,json,random,shutil,statistics,sys,time
from pathlib import Path
from client import Server
from validate_helpers import images
from cold import native_payload
repo=Path(__file__).resolve().parents[2];root=Path(sys.argv[1]).resolve();assert root.is_relative_to(repo/'build')
out=root/(sys.argv[2] if len(sys.argv)>2 else 'ab-02');assert not out.exists();out.mkdir()
bins={}
for arm in ('ordinary','pgo'):
 target=out/'binaries'/arm/'ServerEngineV4.exe';target.parent.mkdir(parents=True);shutil.copy2(root/('compiler/Release/ServerEngineV4.exe' if arm=='pgo' else 'bin/ordinary/ServerEngineV4.exe'),target);bins[arm]=target
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
hashes={a:sha(p) for a,p in bins.items()};freeze=json.loads((root/'freeze.json').read_text());gold=json.loads((root/'validation.json').read_text())['ordinary']['unit']['images']
(out/'binary-manifest.json').write_text(json.dumps(dict(binary_hashes=hashes,pgd_hash=sha(root/'compiler/production.pgd')),indent=2))
for name,digest in freeze['source'].items():assert sha(root/'source'/name)==digest,('Source changed',name)
def verify_fixture():
 for fixture,files in freeze['fixtures'].items():
  base=root/'fixtures'/fixture
  actual={p.relative_to(base).as_posix() for p in base.rglob('*') if p.is_file() and '.serverengine' not in p.relative_to(base).parts}
  assert actual==set(files),('Fixture file set changed',fixture)
  for name,digest in files.items():assert sha(base/name)==digest,('Fixture changed',fixture,name)
verify_fixture()

kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p];kernel.CreateFileW.restype=ctypes.c_void_p
# Pin frozen images read-only: concurrent relink/cleanup cannot replace them.
pinned=[kernel.CreateFileW(str(p),0x80000000,1,None,3,0,None) for p in bins.values()]
assert all(p and p!=ctypes.c_void_p(-1).value for p in pinned)
kernel.GetProcessTimes.argtypes=[ctypes.c_void_p]+[ctypes.c_void_p]*4
def cpu(p):
 t=[ctypes.c_ulonglong() for _ in range(4)];assert kernel.GetProcessTimes(int(p._handle),*[ctypes.byref(x) for x in t]);return (t[2].value+t[3].value)/1e4
class Memory(ctypes.Structure):_fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong),('peak_ws',ctypes.c_size_t),('ws',ctypes.c_size_t),('peak_paged',ctypes.c_size_t),('paged',ctypes.c_size_t),('peak_nonpaged',ctypes.c_size_t),('nonpaged',ctypes.c_size_t),('pagefile',ctypes.c_size_t),('peak_pagefile',ctypes.c_size_t)]
psapi=ctypes.WinDLL('psapi');psapi.GetProcessMemoryInfo.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong]
def run(label,arm):
 session=out/(str(label)+'-'+arm);session.mkdir()
 for f in ('server.license','server.lease'):shutil.copy2(repo/f,session/f)
 at=time.perf_counter();server=Server(bins[arm],session);startup=(time.perf_counter()-at)*1000
 try:
  _,idle=server.request('GET_STATE');before=cpu(server.process)
  response,wall=server.request('PUBLISH',path=str(root/'fixtures/unit/project.json'));work=cpu(server.process)-before
  m=Memory();m.cb=ctypes.sizeof(m);assert psapi.GetProcessMemoryInfo(int(server.process._handle),ctypes.byref(m),m.cb)
  assert server.request('GET_STATE')[0]['payload']['state']=='LOADED'
  # Both checks and all hashes happen after the request timing stopped.
  assert images(root/'fixtures/unit/project.json')==gold,'persisted artifact parity'
  # Native payload checks are outside the timed request, on every arm.
  runtime=root/'fixtures/unit/.serverengine/project.json/runtime.bin'
  expected= json.loads((root/'validation.json').read_text())['ordinary']['unit']['native']
  assert native_payload(server.name,runtime)==expected,'live native SHM parity'
  server.request('UNLOAD')
 finally:server.stop()
 row=dict(block=label,arm=arm,publish_request_ms=wall,publish_process_cpu_ms=work,startup_ms=startup,idle_request_ms=idle,peak_ws_bytes=m.peak_ws,page_faults=m.faults)
 print(label,arm,round(wall,2),round(work,2),flush=True);return row
for arm in ('ordinary','pgo'):run('warmup',arm)
rows=[]
for block in range(15):
 order=['ordinary','pgo','pgo','ordinary'] if block%2==0 else ['pgo','ordinary','ordinary','pgo']
 for i,arm in enumerate(order):
  row=run(f'{block}-{i}',arm);row['pair_block']=block;rows.append(row)
  (out/'samples.json').write_text(json.dumps(rows,indent=2))
summary={}
for metric in ('publish_request_ms','publish_process_cpu_ms','startup_ms','idle_request_ms','peak_ws_bytes'):
 a=[statistics.mean(r[metric] for r in rows if r['pair_block']==i and r['arm']=='ordinary') for i in range(15)]
 b=[statistics.mean(r[metric] for r in rows if r['pair_block']==i and r['arm']=='pgo') for i in range(15)]
 gains=[x-y for x,y in zip(a,b)];rng=random.Random(101);boot=sorted(statistics.mean(rng.choices(gains,k=15)) for _ in range(20000))
 summary[metric]=dict(ordinary_median=statistics.median(r[metric] for r in rows if r['arm']=='ordinary'),pgo_median=statistics.median(r[metric] for r in rows if r['arm']=='pgo'),mean_block_gain=statistics.mean(gains),median_block_gain=statistics.median(gains),bootstrap_mean95=[boot[500],boot[19499]],winning_blocks=sum(g>0 for g in gains),ordinary_mean=statistics.mean(a),pgo_mean=statistics.mean(b))
verify_fixture()
assert hashes=={a:sha(p) for a,p in bins.items()}
assert not any((root/'bin').rglob('*.pgc')),'instrumented executable in timed series'
(out/'summary.json').write_text(json.dumps(dict(blocks=15,calls=len(rows),binary_hashes=hashes,artifact_hashes=gold,metrics=summary),indent=2));print(json.dumps(summary,indent=2))
