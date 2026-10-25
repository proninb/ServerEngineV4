"""Audit evidence only: decode two production Runtime plans with aliased writes.

This is a deliberately limited plan interpreter for one direct static link.
It does not replace Runtime V2 or serve as a general semantic equivalence oracle.
"""
import argparse,hashlib,json,struct,subprocess
from pathlib import Path

repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path)
p.add_argument('--exe',type=Path,default=repo/'build/enum-v4/Release/ServerEngineV4PublishBenchmark.exe')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
(out/'executable.json').write_text(json.dumps(dict(path=str(a.exe.resolve()),
 sha256=hashlib.sha256(a.exe.read_bytes()).hexdigest()),indent=2))

def sections(data):
 assert data[:8]==b'SERTV4R\0'
 assert struct.unpack_from('<IIII',data,8)==(1,64,32,24)
 result={}
 for i in range(32):
  kind,size,offset,count=struct.unpack_from('<IIQQ',data,64+i*24)
  assert offset+size*count<=len(data)
  result[kind]=[data[offset+j*size:offset+(j+1)*size] for j in range(count)]
 return result

def decode(directory):
 ss=sections((directory/'runtime.bin').read_bytes())
 links=[struct.unpack('<QQIIII',r) for r in ss[45]]
 assert len(links)==1
 source,target,source_program,target_program,flags,reserved=links[0]
 assert source_program==target_program==0 and flags==1 and reserved==0
 references={target:source}
 programs=[struct.unpack('<QQII',r) for r in ss[47]]
 deltas=[struct.unpack('<Q',r)[0] for r in ss[48]]
 writes=[]
 for r in ss[46]:
  address=struct.unpack_from('<Q',r)[0]
  program=struct.unpack_from('<I',r,24)[0];size=r[28]
  assert size==4
  if program:
   root,tail,begin,count=programs[program-1]
   address=root
   for delta in deltas[begin:begin+count]:address=references[address+delta]
   address+=tail
  writes.append(dict(address=address,value=int.from_bytes(r[8:8+size],'little',signed=True)))
 assert len(writes)==2 and writes[0]['address']==writes[1]['address']
 return dict(link_source=source,link_target=target,writes=writes,final_value=writes[-1]['value'])

results={}
for name,statements in [('original','a.ref.x=1; b.x=2;'),('grouped_by_object','b.x=2; a.ref.x=1;'),
                        ('repeated_alias','a.ref.x=1; b.x=2; a.ref.x=3;')]:
 root=out/name;root.mkdir(exist_ok=True)
 (root/'types.h').write_text('struct I { int x; }; struct R { I& ref; };\n')
 (root/'source.cpp').write_text('I b; R a; a.ref=b; '+statements+'\n')
 project=root/'project.json'
 project.write_text(json.dumps(dict(version=1,name='DeferredObjectAudit',project=[
  dict(name='types.h',type='header',path='types.h'),dict(name='source.cpp',type='source',path='source.cpp')],
  preprocessor=dict(predefines=[])),indent=2))
 r=subprocess.run([str(a.exe.resolve()),'publish',str(project),'2'],capture_output=True,text=True)
 (root/'publish.stdout').write_text(r.stdout);(root/'publish.stderr').write_text(r.stderr)
 assert r.returncode==0,(name,r.stdout,r.stderr)
 results[name]=decode(root/'.serverengine'/project.name)
assert results['original']['final_value']==2 and results['grouped_by_object']['final_value']==1
assert results['repeated_alias']['final_value']==2 and results['repeated_alias']['writes'][0]['value']==3
(out/'summary.json').write_text(json.dumps(results,indent=2));print(json.dumps(results,indent=2))
