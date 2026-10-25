"""Owned consumer after bound teardown, losing histories and overwritten cycles."""
import json,os,subprocess,sys
from pathlib import Path
exe,root=map(lambda p:Path(p).resolve(),sys.argv[1:]);root.mkdir(parents=True,exist_ok=True)
cases={
 'losing-writes':('I b; I& r; r=b; b.x=1; r.x=2; b.x=3;',[3],2,3,1),
 'losing-cycle':('int a; int& p; int& q; p=q; q=p; p=a; q=a; a=7;',[7],2,1,4),
 'declaration-only':('I b;',[],0,0,0),
}
count=0
for name,(source,values,warnings,writes,bindings) in cases.items():
 d=root/name;d.mkdir(parents=True,exist_ok=True);(d/'types.h').write_text('struct I { int x; };')
 (d/'a.cpp').write_text(source);(d/'b.cpp').write_text('')
 project=d/'project.json';project.write_text(json.dumps(dict(version=1,name=name,project=[dict(name='h',type='header',path='types.h'),dict(name='a',type='source',path='a.cpp'),dict(name='b',type='source',path='b.cpp')],preprocessor=dict(predefines=[]))))
 results=[];histories=[]
 for own in (0,1):
  for reverse in (False,True):
   report=d/f'{own}-{int(reverse)}.json';ownership=report.with_suffix('.owner.json')
   env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN=str(own),PLS07_VERIFY='1',PLS07_OWNER_PATH=str(ownership))
   r=subprocess.run([str(exe),str(project),str(report)]+(['--reverse'] if reverse else []),capture_output=True,text=True,env=env);assert r.returncode==0,(name,own,reverse,r.stderr)
   data=json.loads(report.read_text());o=json.loads(ownership.read_text());assert o['verified'] and o['owned']==bool(own)
   if own:assert o['buffers_moved'] and o['bound_empty']
   assert sorted(v['value'] for v in data['values'])==values and len(data['warnings'])==warnings
   assert data['counts']['writes']==writes and data['counts']['bindings']==bindings
   results.append(data);histories.append(o['history_hash']);count+=1
 assert len(set(histories))==1,name
 for data in results[1:]:
  for key in ('values','warnings','semantic_fingerprint'):assert data[key]==results[0][key],(name,key)
print(f'PLS07: {count} ownership/history cases passed')
