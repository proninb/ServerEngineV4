"""Architecture contract checks, not a parallel Source parser or performance test.

Production PUBLISH supplies evidence of old V2 behavior. A small independent
final-cell reducer specifies the proposed new behavior and schedule invariance.
"""
import argparse,itertools,json,os,struct,subprocess
from dataclasses import dataclass
from pathlib import Path

repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path)
p.add_argument('--exe',type=Path,default=repo/'build/enum-v4/Release/ServerEngineV4PublishBenchmark.exe')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)

@dataclass(frozen=True)
class Event:
 file_id:int
 source_order:int
 local_order:int
 origin:str
 target:str
 value:int
 @property
 def order(self):return (self.source_order,self.local_order)

def reduce_events(events,final_targets):
 """Exact scalar cells only; targets supplied after final reference linking."""
 cells={}
 for e in events:cells.setdefault(final_targets[e.target],[]).append(e)
 values={};warnings=[]
 for cell,history in cells.items():
  history.sort(key=lambda e:e.order)
  assert len({e.order for e in history})==len(history)
  values[cell]=history[-1].value
  for previous,current in zip(history,history[1:]):
   warnings.append(dict(cell=cell,previous=previous.origin,current=current.origin,
                        order=current.order))
 warnings.sort(key=lambda w:w['order'])
 return dict(values=values,warnings=warnings)

events=[Event(900,0,0,'A.cpp:10','a.ref.x',1),Event(2,1,0,'B.cpp:20','b.x',2),
        Event(37,2,0,'C.cpp:30','a.ref.x',3)]
targets={'a.ref.x':'b.x','b.x':'b.x'}
expected=reduce_events(events,targets)
assert expected['values']=={'b.x':3} and len(expected['warnings'])==2
for permutation in itertools.permutations(events):assert reduce_events(permutation,targets)==expected
renaming={900:1,2:9999,37:19}
assert reduce_events(events,targets)==reduce_events([
 Event(renaming[e.file_id],e.source_order,e.local_order,e.origin,e.target,e.value) for e in events],targets)
local=[Event(7,0,1,'A.cpp:1','a.ref.x',1),Event(7,0,2,'A.cpp:2','b.x',2),
       Event(7,0,3,'A.cpp:3','a.ref.x',3)]
assert reduce_events(local,targets)['values']=={'b.x':3}
assert reduce_events(local,targets)['warnings'][-1]['previous']=='A.cpp:2'
# Changed final BIND directs every alias path to the final referent; this is
# final-state semantics, not execution of intermediate Source assignments.
rebound=reduce_events(local,{'a.ref.x':'c.x','b.x':'b.x'})
assert rebound['values']=={'c.x':3,'b.x':2}

root=out/'project-tree-order';(root/'nested').mkdir(parents=True,exist_ok=True)
(root/'types.h').write_text('struct Tag {};\n')
(root/'defs.cpp').write_text('int value;\n')
(root/'tail.cpp').write_text('value=2;\n')
(root/'nested'/'child.cpp').write_text('value=1;\n')
(root/'nested'/'project.json').write_text(json.dumps(dict(version=1,name='Nested',project=[
 dict(name='child.cpp',type='source',path='child.cpp')]),indent=2))
project=root/'project.json';project.write_text(json.dumps(dict(version=1,name='OrderAudit',project=[
 dict(name='types.h',type='header',path='types.h'),dict(name='defs.cpp',type='source',path='defs.cpp'),
 dict(name='Nested',type='project',path='nested/project.json'),dict(name='tail.cpp',type='source',path='tail.cpp')],
 preprocessor=dict(predefines=[])),indent=2))
r=subprocess.run([str(a.exe.resolve()),'publish',str(project),'1'],capture_output=True,text=True,
 env=dict(os.environ,CW_BULK_MODE='0',CW_SOURCE_RESOLUTION_MODE='0',CW_SOURCE_READY_CURSOR='0'))
(root/'publish.stdout').write_text(r.stdout);(root/'publish.stderr').write_text(r.stderr)
assert r.returncode==0,(r.stdout,r.stderr)
data=(root/'.serverengine'/project.name/'runtime.bin').read_bytes()
assert data[:8]==b'SERTV4R\0' and struct.unpack_from('<IIII',data,8)==(1,64,32,24)
initializations=[]
for i in range(32):
 kind,size,offset,count=struct.unpack_from('<IIQQ',data,64+i*24)
 if kind==46:
  assert size==32 and count==1
  record=data[offset:offset+size]
  assert struct.unpack_from('<I',record,24)[0]==0 and record[28]==4
  initializations.append(int.from_bytes(record[8:12],'little',signed=True))
assert initializations==[1],initializations
tree_expected=reduce_events([Event(100,1,0,'nested/child.cpp:1','value',1),
 Event(3,2,0,'tail.cpp:1','value',2)],{'value':'value'})
assert tree_expected['values']=={'value':2}
summary=dict(alias_contract=expected,schedule_permutations=6,local_order_checked=True,
 supplied_final_target_mapping_checked=True,file_id_independence_checked=True,
 nested_project=dict(v2_runtime_value=initializations[0],new_tree_contract=tree_expected))
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
