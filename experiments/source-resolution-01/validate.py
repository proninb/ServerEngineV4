"""Correctness for diagnostic oracles; no oracle is a production candidate."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda x:Path(x).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
(root/'types.h').write_text('namespace N { struct T { int value; int& ref; int arr[3]; char text[4]; }; }\n')
(root/'source.cpp').write_text('''namespace N {
extern T a; T a; T b; int extra[5];
a.value=1; a.value=2; a.ref=b.value; a.ref=a.value;
a.arr[2]=7; a.arr[2]=8; a.text="abc"; a.text="xyz";
extra[1]=9;
}
namespace N::Inner { T c; c.value=12; }
''')
(root/'second.cpp').write_text('namespace N { a.arr[1]=4; a.text="def"; a.ref=b.value; }\n')
project=root/'project.json';project.write_text(json.dumps(dict(version=1,name='SourceResolution01',project=[
 dict(name='types.h',type='header',path='types.h'),dict(name='source.cpp',type='source',path='source.cpp'),
 dict(name='second.cpp',type='source',path='second.cpp')],preprocessor=dict(predefines=[])),indent=2))
tape=root/'source.trace'
def digest():
 d=root/'.serverengine'/project.name
 return {n:hashlib.sha256((d/n).read_bytes()).hexdigest() for n in ('compiled.bin','runtime.bin')}
def run(mode):
 r=subprocess.run([str(exe),'publish-timeline',str(project),'1'],capture_output=True,text=True,
  env=dict(os.environ,CW_SOURCE_RESOLUTION_MODE=str(mode),CW_SOURCE_RESOLUTION_TAPE=str(tape)))
 (root/f'mode{mode}.stdout').write_text(r.stdout);(root/f'mode{mode}.stderr').write_text(r.stderr)
 assert r.returncode==0,(mode,r.stdout,r.stderr)
 return r
control=run(0);reference=digest()
for mode in (1,2,3,4,5,6):
 r=run(mode);assert r.stderr==control.stderr,(mode,r.stderr,control.stderr);assert digest()==reference,(mode,digest(),reference)
 assert any(l.startswith(f'source_resolution01,mode={mode},') for l in r.stdout.splitlines()),r.stdout
 if mode==4:
  row=dict(f.split('=',1) for f in next(l for l in r.stdout.splitlines() if l.startswith('source_resolution01,')).split(',')[1:])
  assert int(row['retained_new_resolve'])>0 and int(row['bypassed_existing_resolve'])>0,row
# A different source must not silently consume the old oracle.
(root/'second.cpp').write_text('namespace N { a.value=999; }\n')
r=subprocess.run([str(exe),'publish-timeline',str(project),'1'],capture_output=True,text=True,
 env=dict(os.environ,CW_SOURCE_RESOLUTION_MODE='5',CW_SOURCE_RESOLUTION_TAPE=str(tape)))
assert r.returncode!=0 and 'mismatch' in r.stderr,(r.stdout,r.stderr)
print('source_resolution01: all oracles preserve Graph/provenance artifacts, namespace/extern/overwrite cases, and reject stale input')
