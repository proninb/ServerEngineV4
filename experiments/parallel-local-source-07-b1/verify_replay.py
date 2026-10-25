"""Replay-policy evidence via full existing PLS07 parsing, not incremental BUILD."""
import json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
results=[]
def project(d,sources):
 p=d/'project.json';p.write_text(json.dumps(dict(version=1,name=d.name,project=[dict(name='types',type='header',path='types.h')]+[dict(name=Path(s).stem,type='source',path=s) for s in sources],preprocessor=dict(predefines=[]))));return p
def run(d,tag,sources,expected,warnings):
 p=project(d,sources);report=d/(tag+'.json');o=d/(tag+'.owner.json')
 env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1',PLS07_VERIFY='1',PLS07_OWNER_PATH=str(o))
 r=subprocess.run([str(exe),str(p),str(report)],capture_output=True,text=True,env=env)
 (d/(tag+'.stdout')).write_text(r.stdout);(d/(tag+'.stderr')).write_text(r.stderr);assert r.returncode==0,(tag,r.stderr)
 data=json.loads(report.read_text());owner=json.loads(o.read_text());assert owner['buffers_moved'] and owner['bound_empty'] and owner['verified']
 got=sorted((v['object'],v['value']) for v in data['values']);assert got==sorted(expected),(tag,got,expected)
 assert len(data['warnings'])==warnings and data['counts']['linker_token_reads']==0
 results.append(dict(case=tag,values=got,warnings=warnings,tasks=data['tasks'],fingerprint=data['semantic_fingerprint']))
def reject(d,tag,sources):
 p=project(d,sources);report=d/(tag+'.json')
 env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN='1',PLS07_VERIFY='1')
 r=subprocess.run([str(exe),str(p),str(report)],capture_output=True,text=True,env=env)
 assert r.returncode!=0 and report.exists(),(tag,r.stdout,r.stderr)
 data=json.loads(report.read_text());assert data.get('error') and 'values' not in data,(tag,data)
 results.append(dict(case=tag,expected_failure=True,error=data['error']))
header='struct I { int x; }; struct R { I& ref; };'
d=root/'remove-winner';d.mkdir(exist_ok=True);(d/'types.h').write_text(header)
(d/'a.cpp').write_text('I b; b.x=1;');(d/'b.cpp').write_text('b.x=2;')
run(d,'before',['a.cpp','b.cpp'],[('b',2)],1)
(d/'b.cpp').unlink();run(d,'after-delete',['a.cpp'],[('b',1)],0)
reject(d,'missing-but-still-listed',['a.cpp','b.cpp'])
d=root/'rebind';d.mkdir(exist_ok=True);(d/'types.h').write_text(header)
(d/'a.cpp').write_text('I b; I c; R r; r.ref=b; r.ref.x=1;');(d/'b.cpp').write_text('b.x=2;');(d/'c.cpp').write_text('r.ref=c;')
run(d,'with-rebind',['a.cpp','b.cpp','c.cpp'],[('b',2),('c',1)],1)
(d/'c.cpp').unlink();run(d,'without-rebind',['a.cpp','b.cpp'],[('b',2)],1)
d=root/'reorder';d.mkdir(exist_ok=True);(d/'types.h').write_text(header)
(d/'defs.cpp').write_text('I b;');(d/'a.cpp').write_text('b.x=1;');(d/'b.cpp').write_text('b.x=2;')
run(d,'ab',['defs.cpp','a.cpp','b.cpp'],[('b',2)],1)
run(d,'ba',['defs.cpp','b.cpp','a.cpp'],[('b',1)],1)
d=root/'header-context';d.mkdir(exist_ok=True);(d/'types.h').write_text('enum E { V=1 }; struct I { int x; };');(d/'a.cpp').write_text('I b; b.x=V;')
run(d,'old-header',['a.cpp'],[('b',1)],0)
(d/'types.h').write_text('enum E { V=5 }; struct I { int x; };')
run(d,'new-header',['a.cpp'],[('b',5)],0)
d=root/'missing-definition';d.mkdir(exist_ok=True);(d/'types.h').write_text(header);(d/'writer.cpp').write_text('b.x=2;')
reject(d,'declaration-no-longer-present',['writer.cpp'])
(root/'summary.json').write_text(json.dumps(results,indent=2));print('B1: 10 full-replay policy scenarios passed; incremental BUILD not implemented')
