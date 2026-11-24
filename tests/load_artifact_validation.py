"""Real-server regression: damaged LOAD artifacts must fail without killing the server.
Run with a local license/lease available in the repository; uses isolated build data.
"""
import argparse
import json
import struct
import sys
from pathlib import Path

REPO=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(REPO/'experiments/tcp-reliability-01'))
from integration import PreloginServer,session

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--exe',type=Path,required=True)
 parser.add_argument('--output',type=Path,required=True)
 args=parser.parse_args()
 root=args.output.resolve()
 if not root.is_relative_to(REPO/'build') or root.exists():raise RuntimeError('Use a new directory inside build')
 root.mkdir(parents=True)
 (root/'types.h').write_text('struct T { int value; int& ref; char name[12]; };')
 (root/'source.cpp').write_text('T a; T b; a.value=7; a.ref=b.value; a.name="ok";')
 manifest=root/'project.json'
 manifest.write_text(json.dumps(dict(version=1,name='LoadValidation',project=[dict(name='types',type='header',path='types.h'),dict(name='source',type='source',path='source.cpp')],preprocessor=dict(predefines=[]))))
 artifacts=root/'.serverengine/project.json'
 results=[]
 with PreloginServer(args.exe.resolve(),session(root,'server')) as server:
  server.request('LOGIN',name='LoadValidation')
  server.request('PUBLISH',path=str(manifest));server.request('UNLOAD')
  golden={name:(artifacts/name).read_bytes() for name in ['compiled.bin','runtime.bin']}
  def directory_offset(data,kind):
   count=struct.unpack_from('<I',data,16)[0]
   return next(64+i*24 for i in range(count) if struct.unpack_from('<I',data,64+i*24)[0]==kind)
  # Keep known-good payloads; alter only metadata used before materialization.
  abi_kind=34;execution_kind=44
  # Obtain enum identifiers from the real ordered directory rather than infer tag gaps.
  abi_kind=struct.unpack_from('<I',golden['runtime.bin'],64)[0]
  # Execution header follows ABI + nine layout columns.
  execution_kind=abi_kind+10
  cases=[('compiled-magic','compiled.bin',0,b'BADMAGIC'),
   ('compiled-version','compiled.bin',8,struct.pack('<I',0xffffffff)),
   ('compiled-truncated','compiled.bin',None,b'x'),
   ('runtime-abi-header-missing','runtime.bin',directory_offset(golden['runtime.bin'],abi_kind)+16,struct.pack('<Q',0)),
   ('runtime-execution-header-missing','runtime.bin',directory_offset(golden['runtime.bin'],execution_kind)+16,struct.pack('<Q',0))]
  for label,kind in [('runtime-abi-magic',abi_kind),('runtime-execution-magic',execution_kind)]:
   entry=directory_offset(golden['runtime.bin'],kind);offset=struct.unpack_from('<Q',golden['runtime.bin'],entry+8)[0]
   cases.append((label,'runtime.bin',offset,b'BADMAGIC'))
  entry=directory_offset(golden['runtime.bin'],abi_kind);offset=struct.unpack_from('<Q',golden['runtime.bin'],entry+8)[0]
  cases.append(('runtime-unconnected-count','runtime.bin',offset+36,struct.pack('<I',0xffffffff)))
  for label,name,offset,replacement in cases:
   for file,data in golden.items():(artifacts/file).write_bytes(data)
   data=bytearray(golden[name]) if offset is not None else bytearray(replacement)
   if offset is not None:data[offset:offset+len(replacement)]=replacement
   (artifacts/name).write_bytes(data)
   response,_=server.request('LOAD',allow_failure=True,path=str(manifest))
   assert response['status'] is False,(label,'damaged artifact was accepted',response)
   state=server.request('GET_STATE')[0]['payload']['state'];assert state=='UNLOADED',(label,state)
   results.append({'case':label,'rejected':True,'server_alive':True});print(label,'PASS',flush=True)
  for file,data in golden.items():(artifacts/file).write_bytes(data)
  server.request('LOAD',path=str(manifest))
  assert server.request('GET_VALUE',name='a.value')[0]['payload']['bits']==7
  results.append({'case':'valid-after-rejections','loaded':True})
 (root/'results.json').write_text(json.dumps(results,indent=2)+'\n')
 print('LOAD artifact validation: PASS',flush=True)

if __name__=='__main__':main()
