"""Six synchronized external VirtualQueryEx captures of an isolated replay copy."""
import csv,ctypes,json,queue,subprocess,sys,threading,time
from collections import Counter,defaultdict
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'project-unload-control-07'))
from control_stability import sha,validate
LABELS=['baseline','b00','b01','b02','b03']
class Region(ctypes.Structure):
 _fields_=[('base',ctypes.c_void_p),('allocation_base',ctypes.c_void_p),('allocation_protect',ctypes.c_ulong),('partition',ctypes.c_ushort),('size',ctypes.c_size_t),('state',ctypes.c_ulong),('protect',ctypes.c_ulong),('type',ctypes.c_ulong)]
class Memory(ctypes.Structure):
 _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(x,ctypes.c_size_t) for x in ('peak_ws','ws','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile','private')]
def identity(process):
 kernel=ctypes.WinDLL('kernel32',use_last_error=True);fn=kernel.GetProcessTimes;fn.argtypes=[ctypes.c_void_p]+[ctypes.POINTER(ctypes.c_ulonglong)]*4
 fields=[ctypes.c_ulonglong() for _ in range(4)]
 if not fn(int(process._handle),*map(ctypes.byref,fields)):raise ctypes.WinError(ctypes.get_last_error())
 return dict(pid=process.pid,creation_filetime=fields[0].value)
def memory(process):
 p=Memory();p.cb=ctypes.sizeof(p);fn=ctypes.WinDLL('psapi',use_last_error=True).GetProcessMemoryInfo;fn.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong]
 if not fn(int(process._handle),ctypes.byref(p),p.cb):raise ctypes.WinError(ctypes.get_last_error())
 handles=ctypes.c_ulong();fn=ctypes.WinDLL('kernel32',use_last_error=True).GetProcessHandleCount;fn.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_ulong)]
 if not fn(int(process._handle),ctypes.byref(handles)):raise ctypes.WinError(ctypes.get_last_error())
 return dict(private_bytes=p.private,working_set_bytes=p.ws,handles=handles.value)
def capture(process,out,label,expected_identity):
 if identity(process)!=expected_identity:raise ValueError('process identity changed')
 assert ctypes.sizeof(Region)==48
 query=ctypes.WinDLL('kernel32',use_last_error=True).VirtualQueryEx;query.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.POINTER(Region),ctypes.c_size_t];query.restype=ctypes.c_size_t
 before=memory(process);rows=[];address=0;start=time.monotonic()
 while address<1<<63:
  r=Region();size=query(int(process._handle),address,ctypes.byref(r),ctypes.sizeof(r))
  if not size:
   if ctypes.get_last_error()==87 and rows:break
   raise ctypes.WinError(ctypes.get_last_error())
  if size!=48 or not r.size or (r.base or 0)+r.size<=address:raise ValueError('invalid query result')
  rows.append(dict(base=r.base or 0,allocation_base=r.allocation_base or 0,size_bytes=r.size,state=r.state,type=r.type,protect=r.protect))
  address=(r.base or 0)+r.size
 after=memory(process)
 with (out/(label+'-regions.csv')).open('w',newline='') as stream:
  writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
 result=dict(label=label,identity=expected_identity,before=before,after=after,private_stable_during_map=before['private_bytes']==after['private_bytes'],query_seconds=time.monotonic()-start,region_count=len(rows))
 (out/(label+'-summary.json')).write_text(json.dumps(result,indent=2))
 return result
def main():
 root=Path('build/project-unload-address-09').resolve();out=root/'run-01';out.mkdir(exist_ok=False)
 base=Path('build/project-unload-thread-06').resolve();exe=root/'replay_address09.exe';inputs=[exe,base/'replay.exe',base/'strict-replay.bin',base/'stages.bin']
 hashes={str(p):sha(p) for p in inputs};reference=[json.loads(x) for x in (base/'single.jsonl').read_text().splitlines()][1:-1]
 result=dict(status='RUNNING',runs=[],hashes=hashes,production_changed=False,mode='single',release='BLOCKED')
 (out/'executed-harness.py').write_bytes(Path(__file__).read_bytes())
 def save():(out/'status.json').write_text(json.dumps(result,indent=2))
 save()
 try:
  for index in range(1,7):
   if {str(p):sha(p) for p in inputs}!=hashes:raise ValueError('before hash gate')
   target=out/f'process-{index:02}';target.mkdir();command=[str(exe),str(inputs[2]),str(inputs[3]),'single']
   with (target/'stderr.txt').open('w') as err:
    process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err,text=True,bufsize=1,creationflags=0x08000000)
    q=queue.Queue()
    def reader():
     try:
      for line in process.stdout:q.put(line)
     finally:q.put(None)
    thread=threading.Thread(target=reader,daemon=True);thread.start();pid=identity(process);snapshots=[];records=[];expected=0
    try:
     with (target/'protocol.jsonl').open('w') as log:
      while True:
       line=q.get(timeout=900)
       if line is None:break
       log.write(line);log.flush();row=json.loads(line)
       if 'checkpoint09' in row:
        if expected>=5 or row['checkpoint09']!=LABELS[expected] or row['pid']!=process.pid:raise ValueError('checkpoint sequence/PID')
        snapshots.append(capture(process,target,LABELS[expected],pid));expected+=1
        process.stdin.write('C');process.stdin.flush()
       else:records.append(row)
     code=process.wait(timeout=30)
     if code or expected!=5:raise ValueError(('process incomplete',code,expected))
     validate(records,reference)
     (target/'replay.jsonl').write_text(''.join(json.dumps(row)+'\n' for row in records))
    finally:
     if process.poll() is None:process.terminate();process.wait(timeout=15)
     process.stdin.close();process.stdout.close();thread.join(timeout=2)
   after={str(p):sha(p) for p in inputs}
   if after!=hashes:raise ValueError('after hash gate')
   result['runs'].append(dict(run=index,identity=pid,command=command,exit_code=code,signatures_verified=274,counters=records[-1],snapshots=snapshots,after_hashes=after));save()
   print(index,process.pid,[(s['label'],round(s['before']['private_bytes']/2**20,6)) for s in snapshots],flush=True)
  result['status']='CAPTURE COMPLETE'
 except Exception as error:result.update(status='FAIL',error=repr(error));raise
 finally:save()
if __name__=='__main__':main()
