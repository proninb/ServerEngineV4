"""Artifact/origin parity for bulk blocks, aliases, externs and rejection order."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
header='''struct I { int x; };
struct R { int value; int& ref; int arr[3]; char text[4]; I& alias; };
'''
cases={
 'positive':('''extern R a; R a; R b; I i;
a.value=1; a.value=2; a.ref=b.value; a.ref=a.value;
a.arr[2]=7; a.arr[2]=8; a.text="abc"; a.text="xyz";
a.alias=i; a.alias.x=1; i.x=2; a.alias.x=3;
''','a.value=4; a.ref=b.value; a.text="def";\n',True),
 'unbound_forward':('R a; a.value=1; a.ref=later.value; R later;','',False),
 'type_error':('R a; R b; a.value=1; a.ref=b; a.value=2;','',False),
 'invalid_index':('R a; a.arr[1]=2; a.arr[3]=8;','',False),
 'duplicate_object':('R a; a.value=1; R a;','',False),
 'unresolved_extern':('extern R a; a.value=1;','',False),
 'later_syntax':('R a; a.value=1; a.value=2; a.value=;','',False),
 'const_before_syntax':('const int a=1; a=2; broken','',False),
}
def digest(directory):
 return [hashlib.sha256((directory/n).read_bytes()).hexdigest() for n in ('compiled.bin','runtime.bin')]
count=0
for name,(first,second,success) in cases.items():
 d=root/name;d.mkdir(exist_ok=True);(d/'types.h').write_text(header)
 (d/'first.cpp').write_text(first);(d/'second.cpp').write_text(second)
 project=d/'project.json';project.write_text(json.dumps(dict(version=1,name='Bulk01',project=[
  dict(name='types.h',type='header',path='types.h'),dict(name='first.cpp',type='source',path='first.cpp'),
  dict(name='second.cpp',type='source',path='second.cpp')],preprocessor=dict(predefines=[]))))
 reference=None;error=None
 for mode,block in [(0,65536),(1,1),(1,7),(1,65536)]:
  r=subprocess.run([str(exe),'publish-timeline',str(project),'2'],capture_output=True,text=True,
   env=dict(os.environ,CW_BULK_MODE=str(mode),CW_BULK_BLOCK=str(block),CW_BULK_AUDIT='1',CW_BULK_VERIFY='1'))
  stem=d/f'mode{mode}-block{block}';stem.with_suffix('.stdout').write_text(r.stdout);stem.with_suffix('.stderr').write_text(r.stderr)
  assert (r.returncode==0)==success,(name,mode,block,r.stdout,r.stderr)
  if error is None:error=r.stderr
  assert r.stderr==error,(name,mode,block,r.stderr,error)
  if success:
   hashes=digest(d/'.serverengine'/project.name)
   if reference is None:reference=hashes
   assert hashes==reference,(name,mode,block,hashes,reference)
   if mode==1:
    row=dict(v.split('=',1) for v in next(l for l in r.stdout.splitlines() if l.startswith('bulk_materialization01,')).split(',')[1:])
    assert int(row['replacements'])>=4 and int(row['init_calls'])>0 and int(row['link_calls'])>0,row
    assert int(row['peak_pending'])<=block,row
  count+=1
print(f'bulk01: {count} controls/candidates preserve artifacts or exact rejection diagnostics')
