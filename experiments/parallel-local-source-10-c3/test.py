"""New temporal contract: independent resident values/pointers + scheduling parity."""
import hashlib,json,os,re,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
header='struct I { int x=5; int a[3]; }; struct R { I& ref; }; struct W { R& link; };'
cases={
 'write-before-rebind':(header,['I b; I c; R a; a.ref=b; a.ref.x=10; a.ref=c;'],{'b.x':10,'c.x':5},[('a.ref','c')],1),
 'write-after-rebind':(header,['I b; I c; R a; a.ref=b; a.ref=c; a.ref.x=10;'],{'b.x':5,'c.x':10},[('a.ref','c')],1),
 'triple':(header,['I c; R a; c.x=1; a.ref=c; a.ref.x=2; c.x=3;'],{'c.x':3},[('a.ref','c')],2),
 'cross-file':(header,['I b; b.x=1;','extern I b; b.x=2;','extern I b; b.x=3;'],{'b.x':3},[],2),
 'extern-before-definition':(header,['extern I b; R r; r.ref=b; r.ref.x=1;','I b; b.x=8;'],{'b.x':8},[('r.ref','b')],1),
 'nested':(header,['I b; I c; R r; W w; w.link=r; r.ref=b; w.link.ref.x=9; w.link.ref=c; w.link.ref.x=10;'],{'b.x':9,'c.x':10},[('r.ref','c'),('w.link','r')],1),
 'native-default':('struct I { int x=5; }; struct R { I i; I& ref; R():ref(i){} };',['R r; r.ref.x=9;'],{'r.i.x':9},[('r.ref','r.i')],0),
 'nested-default':('struct I { int x=5; }; struct R { I local; I& ref; R():ref(local){} }; struct T { R nested; };',['T t; t.nested.ref.x=11;'],{'t.nested.local.x':11},[('t.nested.ref','t.nested.local')],0),
 'array-alias':(header,['I b[2]; R r; r.ref=b[1]; r.ref.a[2]=17; b[1].a[2]=19;'],{'b[0].x':5,'b[1].a[2]':19},[('r.ref','b[1]')],1),
 'strings':('struct T { char text[4]; };',['T t; t.text="abc"; t.text="xy";'],{'t.text[0]':120,'t.text[1]':121,'t.text[2]':0,'t.text[3]':0},[],4),
 'const-enum':('enum class E { A=3,B=7 }; struct T { E e; };',['T t; t.e=E::A; t.e=E::B; const int n=4;'],{'t.e':7,'n':4},[],1),
 'inheritance':('struct A { int x=1; }; struct B:A { int y=2; };',['B b; b.x=9;'],{'b.<base0>.x':9,'b.y':2},[],0),
 'queue-wrap':(header,['I b; b.x=0;']+['extern I b; b.x='+str(i)+';' for i in range(1,21)],{'b.x':20},[],20),
 'empty':(header,['','',''],{},[],0),
 'no-future-binding':(header,['R r; r.ref.x=1; I b; r.ref=b;'],'unbound',[],None),
 'forward-reference-source':(header,['I& p; I& q; p=q; I b; q=b;'],'unbound',[],None),
 'missing-file-declaration':(header,['I b;','b.x=7;'],'declaration or extern',[],None),
 'duplicate':(header,['I b; I b;'],'Duplicate object',[],None),
 'missing-definition':(header,['extern I b;'],'extern has no definition',[],None),
 'bounds':(header,['I b; b.a[123]=2;'],'member/index',[],None),
 'earliest-file-error':(header,['I b; b.a[123]=1;','broken = ;'],'member/index',[],None),
 'union':('union U { int x; int y; };',['U u; u.x=1;'],'union storage',[],None),
}
samples=[]
for name,(h,sources,expected,bindings,warnings) in cases.items():
 d=root/name;d.mkdir(exist_ok=True);(d/'types.h').write_text(h)
 entries=[dict(name='types',type='header',path='types.h')]
 for i,text in enumerate(sources):
  (d/f's{i}.cpp').write_text(text);entries.append(dict(name=f's{i}',type='source',path=f's{i}.cpp'))
 # Exercise declaration-order DFS through a nested Project, too.
 if name=='cross-file':
  (d/'child.json').write_text(json.dumps(dict(version=1,name='child',project=entries[2:])))
  entries=entries[:2]+[dict(name='child',type='project',path='child.json')]
 project=d/'project.json';project.write_text(json.dumps(dict(version=1,name=name,project=entries,preprocessor=dict(predefines=[]))))
 baseline=None
 for workers,reverse in ((1,False),(2,True),(4,False),(4,True)):
  env=dict(os.environ,PLS08_CANDIDATE='1',PLS08_COLD_CHECK='1',PLS10_TRACE='1',PLS10_WORKERS=str(workers))
  env.pop('PLS10_REVERSE_TASKS',None)
  if reverse:env['PLS10_REVERSE_TASKS']='1'
  r=subprocess.run([str(exe),'publish',str(project),'0'],capture_output=True,text=True,env=env,timeout=90)
  label=f'{workers}-{int(reverse)}';(d/f'{label}.stdout').write_text(r.stdout);(d/f'{label}.stderr').write_text(r.stderr)
  if isinstance(expected,str):
   assert r.returncode!=0 and expected in r.stderr,(name,workers,reverse,r.stdout,r.stderr)
   signature=r.stderr
  else:
   assert r.returncode==0,(name,workers,reverse,r.stdout,r.stderr)
   line=next(x for x in r.stdout.splitlines() if x.startswith('pls08_resident='));actual=json.loads(line.split('=',1)[1])
   for field,value in expected.items():assert actual['fields'][field]==value,(name,field,actual)
   for target,source in bindings:assert actual['pointers'][target]==actual['addresses'][source],(name,target,source,actual)
   assert f'ordered_conflicts={warnings}' in r.stdout,(name,warnings,r.stdout)
   maximum=int(re.search(r'ordered_window=(\d+)',r.stdout)[1]);ready=int(re.search(r'ordered_peak_ready=(\d+)',r.stdout)[1]);assert ready<=maximum
   traces=[s for s in r.stdout.splitlines() if s.startswith('ordered_warning=')];assert len(traces)==warnings
   assert all(re.search(r'previous file=\d+ offset=\d+ length=[1-9]\d*; current file=\d+ offset=\d+ length=[1-9]\d*',t) for t in traces)
   if name=='triple':
    text=sources[0];offsets=[text.index(x) for x in ('c.x=1','a.ref.x=2','c.x=3')]
    assert f'offset={offsets[0]} ' in traces[0] and f'offset={offsets[1]} ' in traces[0]
    assert f'offset={offsets[1]} ' in traces[1] and f'offset={offsets[2]} ' in traces[1]
   artifacts=d/'.serverengine'/'project.json';digests={f:hashlib.sha256((artifacts/f).read_bytes()).hexdigest() for f in ('compiled.bin','runtime.bin')}
   signature=(digests,traces) # Includes persisted WHO, SourceMap and final records.
  if baseline is None:baseline=signature
  else:assert baseline==signature,(name,'scheduling changed artifacts/origins',workers,reverse)
  samples.append(dict(case=name,workers=workers,reverse=reverse,success=r.returncode==0))
(root/'summary.json').write_text(json.dumps(samples,indent=2));print(f'{len(samples)} ordered temporal PUBLISH checks PASS')
