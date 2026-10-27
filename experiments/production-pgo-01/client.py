"""Production TCP/JSON client; no replacement entry point or server timers."""
import json,os,socket,struct,subprocess,time,uuid
from pathlib import Path
class Server:
 def __init__(self,exe,root):
  root=Path(root);root.mkdir(parents=True,exist_ok=True)
  self.name='CW.ProductionPGO01.'+uuid.uuid4().hex
  with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
  config=dict(version=9,settings=dict(abi=dict(target='windows-x64',pack=8),shm=dict(mode='fixed_direct',name=self.name,fixed_base_address='0x0000010000000000'),files=dict(manifest='project.manifest',source_save='source.bin',database='database.bin',compiled='compiled.bin')),server_identity=dict(mode='none'),authentication=dict(mode='none'),communication=dict(endpoints=[dict(name='local-pgo',transport='tcp',protocol='json',address='127.0.0.1',port=port)]))
  (root/'server.json').write_text(json.dumps(config))
  self.stdout=(root/'stdout.log').open('w');self.stderr=(root/'stderr.log').open('w')
  self.process=subprocess.Popen([str(exe),str(root/'server.json')],cwd=root,stdin=subprocess.DEVNULL,stdout=self.stdout,stderr=self.stderr,creationflags=0x08000000 if os.name=='nt' else 0)
  deadline=time.monotonic()+30
  while True:
   if self.process.poll() is not None:
    self.close_files();raise RuntimeError('Server startup failed: '+(root/'stderr.log').read_text()[-2500:])
   try:self.socket=socket.create_connection(('127.0.0.1',port),timeout=1);break
   except OSError:
    if time.monotonic()>deadline:self.process.terminate();raise
    time.sleep(.05)
  self.socket.settimeout(180);self.socket.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);self.sequence=0
  self.request('LOGIN',name='PGO.Local')
 def __enter__(self):return self
 def __exit__(self,*args):self.stop()
 def read(self,n):
  data=b''
  while len(data)<n:
   chunk=self.socket.recv(n-len(data))
   if not chunk:raise EOFError('Server closed connection')
   data+=chunk
  return data
 def request(self,command,allow_failure=False,**arguments):
  self.sequence+=1;request=dict(request_id=self.sequence,command=command)
  if arguments:request['arguments']=arguments
  data=json.dumps(request).encode();at=time.perf_counter_ns();self.socket.sendall(struct.pack('!I',len(data))+data)
  while True:
   size=struct.unpack('!I',self.read(4))[0];assert 0<size<16*1024*1024
   response=json.loads(self.read(size))
   if response.get('request_id')==self.sequence:break
  elapsed=(time.perf_counter_ns()-at)/1e6
  if response.get('status') is not True and not allow_failure:raise RuntimeError((command,response))
  return response,elapsed
 def close_files(self):self.stdout.close();self.stderr.close()
 def stop(self):
  try:self.request('SHUTDOWN')
  finally:
   self.socket.close()
   try:code=self.process.wait(timeout=15)
   except subprocess.TimeoutExpired:self.process.terminate();self.process.wait();raise
   self.close_files()
  assert code==0,code
