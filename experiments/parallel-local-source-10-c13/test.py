import hashlib,json,os,subprocess,sys
from pathlib import Path
native,audit,baseline,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
script=Path(__file__).resolve().parents[1]/'parallel-local-source-10-c7/test.py';program=script.read_text()
extra="""
 'memo-two-whos':('struct T { char text[4]; }; struct R { T& ref; };',['T a; T b; R r; r.ref=a; r.ref.text="abc"; r.ref=b; r.ref.text="xy";'],{'a.text[0]':97,'a.text[2]':99,'b.text[0]':120,'b.text[2]':0},[('r.ref','b')],1),
 'memo-rebound-subobjects':('struct T { char text[4]; }; struct R { T a; T b; T& ref; };',['R r; r.ref=r.a; r.ref.text="abc"; r.ref=r.b; r.ref.text="xy";'],{'r.a.text[0]':97,'r.a.text[2]':99,'r.b.text[0]':120,'r.b.text[2]':0},[('r.ref','r.b')],1),
 'memo-alternating-prefix':('struct T { char a[4]; char b[4]; };',['T t; t.a="abc"; t.b="xy"; t.a="k";'],{'t.a[0]':107,'t.a[1]':0,'t.b[0]':120,'t.b[2]':0},[],4),
 'memo-root-types':('struct T { char a[4]; }; struct U { char a[4]; int n; };',['T t; U u; t.a="abc"; u.a="xy";'],{'t.a[0]':97,'t.a[2]':99,'u.a[0]':120,'u.a[2]':0},[],0),
"""
needle='extra="""cases.update({';assert program.count(needle)==1
program=program.replace(needle,needle+extra).replace("print('384 native oracle runs:","print('432 native oracle runs:")
wrapper=root/'invoke.py';wrapper.write_text('__file__='+repr(str(script))+'\n'+program);signature=None
for mode in ('0','1'):
 env=dict(os.environ,PLS10_C10_REUSE='1',PLS10_C13_REUSE=mode)
 p=subprocess.run([sys.executable,str(wrapper),str(native),str(audit),str(root/'common')],env=env,capture_output=True,text=True,timeout=300)
 (root/f'{mode}.stdout').write_text(p.stdout);(root/f'{mode}.stderr').write_text(p.stderr);assert p.returncode==0,p.stderr[-3000:]
 shared=root/'common/shared';state=({str(p.relative_to(shared)):hashlib.sha256(p.read_bytes()).hexdigest() for p in shared.rglob('*.bin')},{str(p.relative_to(shared)):p.read_text() for p in shared.rglob('*.stderr') if p.read_text()},{str(p.relative_to(shared)):[s for s in p.read_text().splitlines() if s.startswith('ordered_warning=')] for p in shared.rglob('*.stdout')})
 if signature is None:signature=state
 else:assert signature==state,'memo changed fixture bytes/origins/errors'
 print(mode,p.stdout.strip(),flush=True)
# Cold generic storage hashing supports arrays beyond the resident JSON dumper's
# 128-element limit. Compare exact persisted images + all native bytes to C10.
cases={
 'bound-fallback':('struct T { char a[256]; char b[257]; };','T t; t.a="abc"; t.a="xy"; t.b="abc"; t.b="xy";'),
 'rehash-live-memo':('struct T { char text[4]; }; struct U { int x; };','T a; a.text="abc";'+''.join('U n'+str(k)+'['+str(k)+'];'+''.join('n'+str(k)+'['+str(i)+'].x='+str(i)+';' for i in range(k)) for k in range(1,33))+'T b; b.text="xy";')}
for name,(header,source) in cases.items():
 d=root/name;d.mkdir(exist_ok=True);(d/'types.h').write_text(header);(d/'s.cpp').write_text(source)
 (d/'project.json').write_text(json.dumps(dict(version=1,name=name,project=[dict(name='types',type='header',path='types.h'),dict(name='s',type='source',path='s.cpp')],preprocessor=dict(predefines=[]))))
 old=None
 for label,exe,mode in [('c10',baseline,'1'),('off',native,'0'),('reuse',native,'1'),('audit',audit,'1')]:
  env={k:v for k,v in os.environ.items() if not k.startswith('PLS')};env.update(PLS08_CANDIDATE='1',PLS10_C10_REUSE='1',PLS10_C7_FAST='1',PLS10_C13_REUSE=mode,PLS08_OBJECT_AUDIT='1')
  p=subprocess.run([str(exe),'publish',str(d/'project.json'),'0'],env=env,capture_output=True,text=True,timeout=120);(d/(label+'.stdout')).write_text(p.stdout);(d/(label+'.stderr')).write_text(p.stderr);assert p.returncode==0,p.stderr[-2000:]
  state=({f:hashlib.sha256((d/'.serverengine/project.json'/f).read_bytes()).hexdigest() for f in ('compiled.bin','runtime.bin')},next(s for s in p.stdout.splitlines() if s.startswith('pls08_object_storage_sha256=')))
  if old is None:old=state
  else:assert old==state,(name,label)
  if label=='audit':
   fields={k:float(v) for line in p.stdout.splitlines() for item in line.split(',') if '=' in item for k,v in [item.split('=',1)] if k.startswith('pls13_')}
   assert fields['pls13_hits']==fields['pls13_verified']>0
   if name=='bound-fallback':assert fields['pls13_large']==2
 print(name,'exact C10 parity PASS',flush=True)
print('864 oracle runs + 8 boundary/rehash cold runs PASS')
