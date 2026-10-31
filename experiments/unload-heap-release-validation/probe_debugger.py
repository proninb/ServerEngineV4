import json,subprocess,sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
for p in ('production-pgo-01','tcp-reliability-01'):sys.path.append(str(REPO/'experiments'/p))
from integration import PreloginServer,session
root=REPO/'build/unload-heap-release-validation/debugger-probe';root.mkdir(exist_ok=False)
tools=REPO/'build/project-unload-memory-01/tools/extracted/Windows Kits/10/Debuggers/x64'
symbolroot=REPO/'build/project-unload-memory-01/symbols'
symbols=';'.join(str(p.parent) for p in symbolroot.rglob('*.pdb') if p.name.lower() in ('ntdll.pdb','kernel32.pdb'))
exe=REPO/'build/heap-integration-candidate/on/Release/ServerEngineV4.exe'
with PreloginServer(exe,session(root,'server')) as server:
 server.request('LOGIN',name='FaultProbe')
 command='x kernel32!*GetProcessMemoryInfo*; x kernelbase!*GetProcessMemoryInfo*; x kernel32!*HeapSetInformation*; x kernelbase!*HeapSetInformation*; dt ntdll!_TEB LastErrorValue; qd'
 result=subprocess.run([str(tools/'cdb.exe'),'-pv','-p',str(server.process.pid),'-y',symbols,'-c',command],capture_output=True,text=True,timeout=45,creationflags=0x08000000)
 (root/'cdb.txt').write_text(result.stdout+'\n'+result.stderr)
 assert result.returncode==0,result.returncode
 assert server.request('GET_STATE')[0]['payload']['state']=='UNLOADED'
 print(result.stdout[-4500:])
