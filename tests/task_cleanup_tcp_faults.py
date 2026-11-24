"""Optional Windows/CDB regression: repeated failed UNLOAD must retain Project.

Only creates/debugs its own server and Task processes. Never patches an EXE file.
Requires built task_cleanup_fault_fixture and local Microsoft Debugging Tools.
"""
import argparse,ctypes,hashlib,json,os,subprocess,sys,time
from pathlib import Path
REPO=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(REPO/'experiments/production-pgo-01'))
sys.path.insert(0,str(REPO/'experiments/tcp-reliability-01'))
from integration import PreloginServer,session
import task_timing_server_e2e as e2e

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('server','fixture','project','cdb','results'):p.add_argument('--'+name,type=Path,required=True)
 args=p.parse_args();args.results=args.results.resolve();args.results.mkdir(exist_ok=False)
 for name in ('server','fixture','project','cdb'):
  setattr(args,name,getattr(args,name).resolve());assert getattr(args,name).is_file()
 project,tasks=e2e.prepare_disposable_project(args.project,args.results)
 tasks.write_text(json.dumps({'version':1,'timing':{'mode':'NORMAL','period_ms':2},'tasks':[{'id':'Task0','executable':str(args.fixture),'settings':{'calls':1,'cycles':1,'starting_from_cycle':1,'run_in_freeze':False}}]}))
 k=ctypes.WinDLL('kernel32',use_last_error=True)
 def api(name,result,types):
  fn=getattr(k,name);fn.restype=result;fn.argtypes=types;return fn
 create=api('CreateFileMappingW',ctypes.c_void_p,[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_wchar_p])
 view=api('MapViewOfFile',ctypes.c_void_p,[ctypes.c_void_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_size_t])
 close=api('CloseHandle',ctypes.c_int,[ctypes.c_void_p]);unmap=api('UnmapViewOfFile',ctypes.c_int,[ctypes.c_void_p])
 open_process=api('OpenProcess',ctypes.c_void_p,[ctypes.c_ulong,ctypes.c_int,ctypes.c_ulong])
 wait=api('WaitForSingleObject',ctypes.c_ulong,[ctypes.c_void_p,ctypes.c_ulong])
 debug_break=api('DebugBreakProcess',ctypes.c_int,[ctypes.c_void_p])
 class BasicLimits(ctypes.Structure):
  _fields_=[('process_time',ctypes.c_longlong),('job_time',ctypes.c_longlong),('flags',ctypes.c_ulong),('min_ws',ctypes.c_size_t),('max_ws',ctypes.c_size_t),('active',ctypes.c_ulong),('affinity',ctypes.c_size_t),('priority',ctypes.c_ulong),('scheduling',ctypes.c_ulong)]
 class ExtendedLimits(ctypes.Structure):
  _fields_=[('basic',BasicLimits),('io',ctypes.c_ulonglong*6),('process_memory',ctypes.c_size_t),('job_memory',ctypes.c_size_t),('peak_process',ctypes.c_size_t),('peak_job',ctypes.c_size_t)]
 job=api('CreateJobObjectW',ctypes.c_void_p,[ctypes.c_void_p,ctypes.c_wchar_p])(None,None);assert job
 limits=ExtendedLimits();limits.basic.flags=0x2000
 assert api('SetInformationJobObject',ctypes.c_int,[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_ulong])(job,9,ctypes.byref(limits),ctypes.sizeof(limits))
 assign=api('AssignProcessToJobObject',ctypes.c_int,[ctypes.c_void_p,ctypes.c_void_p])
 mapping=create(ctypes.c_void_p(-1),None,4,0,16,'Local\\TaskCleanupTCP_'+str(os.getpid()));assert mapping
 address=view(mapping,0xf001f,0,0,16);assert address
 state=(ctypes.c_long*4).from_address(address);state[3]=1
 env='SEV4_CLEANUP_TEST_MAPPING';previous=os.environ.get(env);os.environ[env]='Local\\TaskCleanupTCP_'+str(os.getpid())
 debugger=None;child=None;result={}
 try:
  with PreloginServer(args.server,session(args.results,'server')) as server:
   assert assign(job,int(server.process._handle))
   server.request('LOGIN',name='TaskCleanupRegression');descriptor=server.socket.fileno();peer=server.socket.getpeername()
   server.request('LOAD',path=str(project));server.request('RUN');server.request('FREEZE')
   assert state[0] and state[1]>0
   child=open_process(0x100000,False,state[0]);assert child
   expected=server.request('GET_VALUE',name='item0.base')[0]['payload']['bits']
   # Only exported API symbols are needed: never assume a private TEB layout.
   symbols=str(args.results/'symbols');Path(symbols).mkdir()
   commands=args.results/'commands.txt'
   commands.write_text('bp KERNELBASE!TerminateProcess "r rax=0; r rip=poi(@rsp); r rsp=@rsp+8; .echo DENY_TERMINATE; gc"\n.echo FAULT_READY\ng\n')
   log=args.results/'cdb.log'
   try:
    with log.open('w') as stream:
     debugger=subprocess.Popen([str(args.cdb),'-p',str(server.process.pid),'-y',symbols,'-cf',str(commands)],stdin=subprocess.PIPE,stdout=stream,stderr=subprocess.STDOUT,text=True,creationflags=0x08000000)
     deadline=time.monotonic()+30
     while 'FAULT_READY' not in log.read_text(errors='replace'):
      assert debugger.poll() is None
      if time.monotonic()>deadline:raise TimeoutError('CDB attach')
      time.sleep(.05)
     failures=[]
     for i in range(2):
      response,_=server.request('UNLOAD',allow_failure=True);assert response['status'] is False,response
      assert server.request('GET_STATE')[0]['payload']['state']=='FREEZE'
      assert server.request('GET_VALUE',name='item0.base')[0]['payload']['bits']==expected
      assert wait(child,0)==258
      failures.append(response)
     assert log.read_text(errors='replace').count('DENY_TERMINATE\n')>=2
     debugger.stdin.write('bc *\n.detach\nq\n');debugger.stdin.flush()
     assert debug_break(int(server.process._handle))
     assert debugger.wait(timeout=20)==0
    response,_=server.request('UNLOAD');assert response['status'] is True
    assert wait(child,0)==0
    assert server.request('GET_STATE')[0]['payload']['state']=='UNLOADED'
    # Prove cleanup permits reusing the same server and TCP connection.
    state[3]=0
    server.request('LOAD',path=str(project));server.request('RUN');server.request('FREEZE');server.request('UNLOAD')
    assert server.socket.fileno()==descriptor and server.socket.getpeername()==peer
    result=dict(status='PASS',server_sha256=hashlib.sha256(args.server.read_bytes()).hexdigest(),failed_unloads=failures,child_alive_and_Project_queryable_after_both_failures=True,retry_cleanup_and_reload=True,same_TCP=True,scope='CDB forces FALSE from TerminateProcess only in owned server; LastError unspecified in this wire test (ERROR_ACCESS_DENIED covered by unit backend); no private TEB access or EXE-file modifications')
   finally:
    if debugger and debugger.poll() is None:debugger.kill();debugger.wait(timeout=10)
 finally:
  if child:close(child)
  close(job)
  unmap(address);close(mapping)
  if previous is None:os.environ.pop(env,None)
  else:os.environ[env]=previous
 (args.results/'summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
