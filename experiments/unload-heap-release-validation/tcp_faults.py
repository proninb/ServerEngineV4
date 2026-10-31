"""Debug-boundary faults in owned processes only. Never patches an EXE file."""
import ctypes,hashlib,json,shutil,subprocess,sys,time
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
for p in ('production-pgo-01','tcp-reliability-01','tcp-pgo-release-02'):sys.path.append(str(REPO/'experiments'/p))
from integration import PreloginServer,session
from cold import native_payload
from validate_helpers import images

def main():
 root=REPO/'build/unload-heap-release-validation/tcp-faults';root.mkdir(exist_ok=False)
 exe=REPO/'build/heap-integration-candidate/on/Release/ServerEngineV4.exe'
 digest=hashlib.sha256(exe.read_bytes()).hexdigest();assert digest=='a4c3e005998e463b66aefd4e9c96ac6e67274f14657a46e293687af51a4282d9'
 tools=REPO/'build/project-unload-memory-01/tools/extracted/Windows Kits/10/Debuggers/x64'
 symbols=';'.join(str(p.parent) for p in (REPO/'build/project-unload-memory-01/symbols').rglob('*.pdb') if p.name.lower() in ('ntdll.pdb','kernel32.pdb'))
 probe=(REPO/'build/unload-heap-release-validation/debugger-probe/cdb.txt').read_text()
 assert '+0x068 LastErrorValue' in probe and 'KERNELBASE!K32GetProcessMemoryInfo' in probe and 'KERNELBASE!HeapSetInformation' in probe
 fixture=root/'fixture';shutil.copytree(REPO/'build/tcp-pgo-release-02/holdout/inheritance',fixture,ignore=shutil.ignore_patterns('.serverengine'))
 project=fixture/'project.json';runtime=fixture/'.serverengine/project.json/runtime.bin';results=[]
 # X64 offsets verified: TEB LastErrorValue by matching ntdll symbols; PrivateUsage
 # by ctypes structure layout, matching the Windows PROCESS_MEMORY_COUNTERS_EX.
 class Counters(ctypes.Structure):
  _fields_=[('cb',ctypes.c_ulong),('faults',ctypes.c_ulong)]+[(n,ctypes.c_size_t) for n in ('peak_ws','ws','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile','private')]
 assert Counters.private.offset==0x48 and ctypes.sizeof(Counters)==0x50
 def skip(value,error=0):
  return f'ed @$teb+0x68 {error:x}; r rax={value}; r rip=poi(@rsp); r rsp=@rsp+8; gc'
 for mode in ('read_fail','api_fail','insufficient','post_read_fail'):
  out=root/mode;out.mkdir();log=out/'cdb.txt';debugger=None
  with PreloginServer(exe,session(out,'server')) as server:
   server.request('LOGIN',name='FaultBoundary');descriptor=server.socket.fileno();peer=server.socket.getpeername()
   server.request('PUBLISH',path=str(project));gold_images=images(project);gold_native=native_payload(server.name,runtime)
   mem='r @$t0=@$t0+1; '
   if mode=='read_fail':mem+='.echo INJECT_READ_FAILURE; '+skip(0,5)
   elif mode=='post_read_fail':mem+='.if (@$t0 == 2) { .echo INJECT_POST_READ_FAILURE; '+skip(0,5)+' } .else { eq @rdx+0x48 8000000; '+skip(1)+' }'
   else:mem+='eq @rdx+0x48 8000000; '+skip(1)
   api='r @$t1=@$t1+1; .echo INJECT_OPTIMIZE_BOUNDARY; '+skip(0,87) if mode=='api_fail' else 'r @$t1=@$t1+1; .echo INJECT_OPTIMIZE_BOUNDARY; '+skip(1)
   script='r @$t0=0; r @$t1=0;\n'+f'bp KERNELBASE!K32GetProcessMemoryInfo "{mem}"\n'+f'bp KERNELBASE!HeapSetInformation ".if (@rdx == 3) {{ {api} }} .else {{ gc }}"\n'+'.echo FAULT_READY\ng\n'
   commands=out/'commands.txt';commands.write_text(script)
   try:
    with log.open('w') as stream:
     debugger=subprocess.Popen([str(tools/'cdb.exe'),'-p',str(server.process.pid),'-y',symbols,'-cf',str(commands)],stdin=subprocess.PIPE,stdout=stream,stderr=subprocess.STDOUT,text=True,creationflags=0x08000000)
     deadline=time.monotonic()+30
     while 'FAULT_READY' not in log.read_text(errors='replace'):
      assert debugger.poll() is None,'Debugger quit before ready'
      if time.monotonic()>deadline:raise TimeoutError('Debugger ready')
      time.sleep(.05)
     response,_=server.request('UNLOAD');assert response['status'] is True,response
     warning=[d for d in response.get('diagnostics',[]) if d['id']==1004]
     assert len(warning)==1 and warning[0]['severity']=='warning',response
     detail=warning[0]['detail']
     expected={'read_fail':'PrivateUsage read failed; optimization skipped','api_fail':'HeapOptimizeResources failed or is unsupported','insufficient':'PrivateUsage remains above candidate threshold','post_read_fail':'Post-optimization PrivateUsage read failed'}[mode]
     assert expected in detail,detail
     if mode in ('read_fail','post_read_fail'):assert 'win32_error=5;' in detail
     if mode=='api_fail':assert 'win32_error=87;' in detail
     assert server.request('GET_STATE')[0]['payload']['state']=='UNLOADED'
     assert server.socket.fileno()==descriptor and server.socket.getpeername()==peer
     # Stop injection by detaching from this owned process, without killing it.
     debugger.stdin.write('.printf "FAULT_COUNTS read=%d api=%d\\n", @$t0, @$t1\nbc *\n.detach\nq\n');debugger.stdin.flush()
     kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.DebugBreakProcess.argtypes=[ctypes.c_void_p]
     assert kernel.DebugBreakProcess(int(server.process._handle)),ctypes.get_last_error()
     assert debugger.wait(timeout=20)==0
    text=log.read_text(errors='replace')
    expected_counts={'read_fail':(1,0),'api_fail':(1,1),'insufficient':(2,1),'post_read_fail':(2,1)}[mode]
    assert f'FAULT_COUNTS read={expected_counts[0]} api={expected_counts[1]}' in text,text[-2000:]
    # Same live connection recovers to actual Windows API use, no restart.
    server.request('PUBLISH',path=str(project));assert images(project)==gold_images;assert native_payload(server.name,runtime)==gold_native
    assert server.request('GET_VALUE',name='item0.base')[0]['payload']['bits']==1
    server.request('UNLOAD');assert server.request('GET_STATE')[0]['payload']['state']=='UNLOADED'
    server.request('LOAD',path=str(project));assert native_payload(server.name,runtime)==gold_native;server.request('UNLOAD')
    assert server.socket.fileno()==descriptor and server.socket.getpeername()==peer
    results.append(dict(mode=mode,status='PASS',pid=server.process.pid,executable_sha256=digest,warning=response,same_TCP=True,recovered_without_restart=True,scope='Debugger-forced API returns/memory-query values in isolated owned process, not spontaneous OS failures or unmodified process execution'))
    (out/'result.json').write_text(json.dumps(results[-1],indent=2));print(mode,'PASS',flush=True)
   finally:
    if debugger is not None and debugger.poll() is None:
     # No orphan debugger; an unsuccessful diagnostic is not accepted as PASS.
     debugger.kill();debugger.wait(timeout=10)
 assert hashlib.sha256(exe.read_bytes()).hexdigest()==digest
 (root/'summary.json').write_text(json.dumps(dict(status='PASS',scenarios=results),indent=2))
if __name__=='__main__':main()
