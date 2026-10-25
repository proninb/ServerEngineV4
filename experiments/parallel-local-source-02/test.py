"""Real C++ extraction/linking fixtures; expected new-contract values/origins."""
import json,subprocess,sys
from pathlib import Path
exe,root=map(lambda x:Path(x).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
base='struct I { int x; int arr[3]; }; struct R { I& ref; }; struct W { R& link; };\n'
cases={
 'triple':(base,['I b; R a; a.ref=b; a.ref.x=1; b.x=2; a.ref.x=3;'],3,2,None),
 'final-rebind':(base,['I b; I c; R a; a.ref=b; a.ref.x=1; a.ref=c; c.x=7;'],7,2,None),
 'nested-target':(base,['I b; I c; R r; W w; w.link=r; r.ref=b; w.link.ref=c; w.link.ref.x=9; c.x=10;'],10,2,None),
 'reference-chain':(base,['I b; I& p; I& q; p=q; q=b; p.x=1; b.x=4;'],4,1,None),
 'array-alias':(base,['I b; R a; a.ref=b; a.ref.arr[1]=1; b.arr[1]=5;'],5,1,None),
 'native-default':('struct I { int x; }; struct R { I i; I& ref; R():ref(i){} };',
                    ['R r; r.ref.x=1; r.i.x=6;'],6,1,None),
 'cross-file-extern':(base,['extern I b; R a; a.ref=b; a.ref.x=1;','I b; b.x=8;'],8,1,None),
 'cycle':(base,['int& p; int& q; p=q; q=p;'],None,None,'Reference cycle'),
 'unbound':(base,['R r; r.ref.x=1;'],None,None,'Unbound reference'),
 'forward':(base,['R a; a.ref=b;','I b;'],None,None,'declared before use'),
 'mismatch':(base,['R a; int b; a.ref=b;'],None,None,'types differ'),
 'array-error':(base,['I b; b.arr[3]=1;'],None,None,'member/index'),
 'long-index-origin':(base,['I b; b.arr[123]=1;'],None,None,'member/index'),
 'literal-origin':('struct T { int x; };',['T t; t.x = -12   ;'],(1<<64)-12,0,None),
 'duplicate':(base,['I b; I b;'],None,None,'Duplicate object'),
 'extern-error':(base,['extern I b;'],None,None,'extern has no definition'),
 'const-priority':(base,['const int b=1; b=2; broken=;'],None,None,'const initialization'),
 'namespace':('namespace N { struct I { int x; }; struct R { I& ref; }; }',
              ['namespace N { I b; R a; a.ref=b; a.ref.x=1; }','namespace N { b.x=11; }'],11,1,None),
 'enum':('enum class E { A=3,B=7 }; struct T { E x; };',['T t; t.x=E::A; t.x=E::B;'],7,1,None),
 'enum-shadow':('enum E { A=3 };',['int A; int x; x=A;'],None,None,'not a scalar constant'),
 'type-object-shadow':('struct Signal { int x; };',['Signal Signal; Signal.x=13;'],13,0,None),
 'string':('struct T { char text[4]; };',['T t; t.text="abc"; t.text="xy";'],None,4,None),
}
count=0
def invoke(d,project,reverse,success=True,error=None):
 report=d/('reverse.json' if reverse else 'forward.json')
 r=subprocess.run([str(exe),str(project),str(report)]+(['--reverse'] if reverse else []),capture_output=True,text=True)
 report.with_suffix('.stdout').write_text(r.stdout);report.with_suffix('.stderr').write_text(r.stderr)
 assert (r.returncode==0)==success,(d.name,r.stdout,r.stderr)
 result=json.loads(report.read_text())
 if error:
  assert error in result['error'],(d.name,result)
  assert result['origin']['file_id']>0 and result['origin']['length']>0,(d.name,result)
 return result
for name,(header,sources,expected,warnings,error) in cases.items():
 d=root/name;d.mkdir(exist_ok=True);(d/'types.h').write_text(header)
 items=[dict(name='types.h',type='header',path='types.h')]
 for i,source in enumerate(sources):
  path=f'source{i}.cpp';(d/path).write_text(source);items.append(dict(name=path,type='source',path=path))
 project=d/'project.json';project.write_text(json.dumps(dict(version=1,name='PLS02',project=items,preprocessor=dict(predefines=[]))))
 results=[invoke(d,project,r,error is None,error) for r in (False,True)]
 if error:
  assert results[0]==results[1],name
  if name=='long-index-origin':assert results[0]['origin']['length']==3 and results[0]['origin']['offset']==sources[0].index('123')
 else:
  for result in results:
   assert result['counts']['linker_token_reads']==0
   assert result['counts']['warnings']==warnings,(name,result)
   if expected is not None:assert len(result['values'])==1 and result['values'][0]['value']==expected,(name,result)
   for w in result['warnings']:
    assert w['previous']['file_id'] and w['current']['file_id'] and w['previous']['length'] and w['current']['length'],(name,w)
   assert result['counts']['provenance_records']>=result['counts']['declarations']+result['counts']['writes']+result['counts']['bindings']
   if name=='triple':
    text=sources[0]
    assert result['warnings'][0]['previous']['offset']==text.index('a.ref.x=1')
    assert result['warnings'][0]['current']['offset']==text.index('b.x=2')
    assert result['warnings'][1]['previous']['offset']==text.index('b.x=2')
    assert result['warnings'][1]['current']['offset']==text.index('a.ref.x=3')
   if name=='string':assert sorted(v['value'] for v in result['values'])==[0,0,120,121]
   if name=='literal-origin':assert result['values'][0]['value_origin']['offset']==sources[0].index('-12') and result['values'][0]['value_origin']['length']==3
  assert results[0]['values']==results[1]['values'] and results[0]['warnings']==results[1]['warnings'],name
  assert results[0]['semantic_fingerprint']==results[1]['semantic_fingerprint'],name
 count+=2

# Nested declaration-order DFS must be independent of numeric file IDs.
d=root/'tree';(d/'nested').mkdir(parents=True,exist_ok=True);(d/'types.h').write_text('struct Tag {};')
(d/'defs.cpp').write_text('int value;');(d/'tail.cpp').write_text('value=2;');(d/'nested'/'child.cpp').write_text('value=1;')
(d/'nested'/'project.json').write_text(json.dumps(dict(version=1,name='Nested',project=[dict(name='child',type='source',path='child.cpp')])))
project=d/'project.json';project.write_text(json.dumps(dict(version=1,name='Root',project=[
 dict(name='types',type='header',path='types.h'),dict(name='defs',type='source',path='defs.cpp'),
 dict(name='nested',type='project',path='nested/project.json'),dict(name='tail',type='source',path='tail.cpp')],preprocessor=dict(predefines=[]))))
for reverse in (False,True):
 result=invoke(d,project,reverse);assert result['values'][0]['value']==2 and result['counts']['warnings']==1,result
 tasks=result['tasks'];assert tasks[1]['file_id']>tasks[2]['file_id'] and tasks[1]['source_order']<tasks[2]['source_order'],tasks
 count+=1
print(f'parallel_local_source02: {count} real extraction/linking cases passed; no Source syntax reaches linker')
