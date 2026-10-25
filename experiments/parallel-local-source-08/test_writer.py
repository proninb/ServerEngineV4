"""Independent expected Runtime values/defaults/pointers and physical-order stress."""
import json,os,struct,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
common='struct I { int x=5; int a[3]; }; struct R { I& ref; };'
cases={
 'alias-final':(common,['I b; R r; r.ref=b; r.ref.x=1; b.x=2; r.ref.x=3;'],{'b.x':3},[('r.ref','b')],2),
 'declaration-initializer':('struct Tag {};',['int n=1; n=2;'],{'n':2},[],1),
 'header-default':(common,['I b; R r; r.ref=b;'],{'b.x':5,'b.a[0]':0},[('r.ref','b')],0),
 'native-override':('struct I { int x=5; }; struct R { I i; I& ref; R():ref(i){} };',['R r; I b; r.ref=b; r.ref.x=7;'],{'r.i.x':5,'b.x':7},[('r.ref','b')],0),
 'native-default':('struct I { int x=5; }; struct R { I i; I& ref; R():ref(i){} };',['R r; r.ref.x=9;'],{'r.i.x':9},[('r.ref','r.i')],0),
 'strings':('struct T { char text[4]; };',['T t; t.text="abc"; t.text="xy";'],{'t.text[0]':120,'t.text[1]':121,'t.text[2]':0,'t.text[3]':0},[],4),
 'enum-float-const':('enum class E { A=3,B=7 }; struct T { E e; float f; };',['T t; t.e=E::B; t.f=1.25; const int n=4;'],{'t.e':7,'t.f':struct.unpack('<I',struct.pack('<f',1.25))[0],'n':4},[],0),
 'inheritance':('struct A { int x=1; }; struct B:A { int y=2; };',['B b; b.x=9;'],{'b.<base0>.x':9,'b.y':2},[],0),
 'cross-root-winner':(common,['I b; b.x=1;','b.x=2;'],{'b.x':2},[],1),
 'cross-root-rebind':(common,['I b; I c; R r; r.ref=b; r.ref.x=1;','r.ref=c; c.x=7;'],{'b.x':5,'c.x':7},[('r.ref','c')],2),
 'empty-source':('struct I { int x=8; };',[''],{},[],0),
 'nested-arrays':('struct I { int x=5; int y=-2; }; struct T { I rows[2]; int grid[2][3]; };',
   ['T t; t.rows[1].x=9; t.grid[1][2]=7;'],
   {'t.rows[0].x':5,'t.rows[0].y':(1<<32)-2,'t.rows[1].x':9,'t.rows[1].y':(1<<32)-2,
    't.grid[0][0]':0,'t.grid[0][1]':0,'t.grid[0][2]':0,'t.grid[1][0]':0,'t.grid[1][1]':0,'t.grid[1][2]':7},[],0),
 'nested-default-reference':('struct I { int x=5; }; struct R { I local; I& ref; R():ref(local){} }; struct T { R nested; };',
   ['T t; t.nested.ref.x=11;'],{'t.nested.local.x':11},[('t.nested.ref','t.nested.local')],0),
 'array-reference-target':(common,['I b[2]; R r; r.ref=b[1]; r.ref.a[2]=17;'],
   {'b[0].x':5,'b[1].x':5,'b[0].a[0]':0,'b[1].a[2]':17},[('r.ref','b[1]')],0),
 'base-reference-target':('struct A { int x=4; }; struct B:A { int y=6; }; struct R { int& ref; };',
   ['B b; R r; r.ref=b.x; b.x=8;'],{'b.<base0>.x':8,'b.y':6},[('r.ref','b.<base0>.x')],0),
 'header-const-defaults':('enum class E { A=-2,B=7 }; struct T { const int n=3; E e=E::A; double f=1.5; bool ok=true; };',
   ['T t;'],{'t.n':3,'t.e':(1<<32)-2,'t.f':struct.unpack('<Q',struct.pack('<d',1.5))[0],'t.ok':1},[],0),
 'three-root-history':(common,['I b; b.x=1;','b.x=2;','b.x=3;'],{'b.x':3},[],2),
}
summary=[]
for name,(header,sources,expected,bindings,warnings) in cases.items():
 d=root/name;d.mkdir(parents=True,exist_ok=True);(d/'types.h').write_text(header)
 entries=[dict(name='types',type='header',path='types.h')]
 for i,text in enumerate(sources):(d/f's{i}.cpp').write_text(text);entries.append(dict(name=f's{i}',type='source',path=f's{i}.cpp'))
 project=d/'project.json';project.write_text(json.dumps(dict(version=1,name=name,project=entries,preprocessor=dict(predefines=[]))))
 reports=[]
 for reverse_extraction in (False,True):
  for reverse_writer in (False,True):
   report=d/f'{int(reverse_extraction)}-{int(reverse_writer)}.json'
   env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1',PLS07_VERIFY='1',PLS08_REVERSE=str(int(reverse_writer)))
   r=subprocess.run([str(exe),str(project),str(report)]+(['--reverse'] if reverse_extraction else []),capture_output=True,text=True,env=env,timeout=30)
   report.with_suffix('.stderr').write_text(r.stderr);assert r.returncode==0,(name,reverse_extraction,reverse_writer,r.stderr)
   data=json.loads(report.read_text());g=data['graph_writer'];assert len(data['warnings'])==warnings
   for field,value in expected.items():assert g['runtime_fields'][field]==value,(name,field,g['runtime_fields'])
   for target,source in bindings:assert g['runtime_pointers'][target]==g['runtime_addresses'][source],(name,target,source,g)
   assert g['query_values']==g['runtime_values']==data['counts']['cells']
   assert g['query_links']==g['runtime_links'] and g['provenance_checks']>=data['counts']['declarations']
   assert g['source_roots_checked']==g['source_files_checked']>=len(sources)+1
   assert report.with_suffix('.compiled.bin').is_file() and data['counts']['linker_token_reads']==0
   reports.append(data)
 for data in reports[1:]:
  for key in ('semantic_fingerprint','values','warnings'):assert data[key]==reports[0][key],(name,key)
  assert data['graph_writer']['runtime_fields']==reports[0]['graph_writer']['runtime_fields'],name
 summary.append(dict(case=name,runs=4,values=expected,bindings=bindings,warnings=warnings))
(root/'summary.json').write_text(json.dumps(summary,indent=2));print(f'PLS08: {len(cases)*4} independent writer/query/SHM physical-order cases passed')
