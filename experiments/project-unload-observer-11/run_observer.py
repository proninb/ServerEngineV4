import csv,json,os,queue,subprocess,sys,threading,time
from pathlib import Path
sys.path.append(str(Path(__file__).resolve().parents[1]/'project-unload-control-07'))
sys.path.append(str(Path(__file__).resolve().parents[1]/'project-unload-argv-aa10'))
from control_stability import sha,validate
from run_argv import env_digest,EXE_SHA,INPUT_SHA
MODES=('original','off','marker','ack');LABELS=('baseline','b00','b01','b02','b03')
SELECTOR=dict(original='off_',off='off_',marker='mark',ack='ack_')
def schedule():return ['original','off','ack','marker','off','marker','original','ack','marker','ack','off','original','ack','original','marker','off']
def check_marker(row,mode,index,pid):
 if mode not in ('marker','ack') or index>=5 or row.get('observer11')!=LABELS[index] or row.get('pid')!=pid:raise ValueError('marker mode/order/PID')
def main():
 cwd=Path.cwd().resolve();base=cwd/'build/project-unload-thread-06';root=cwd/'build/project-unload-observer-11';out=root/'run-01';out.mkdir(exist_ok=False)
 original=base/'replay.exe';diagnostic=root/'replay.exe';input_file=base/'strict-replay.bin';stages=base/'stages.bin';paths=[original,diagnostic,input_file,stages,base/'boundaries.json']
 hashes={str(p):sha(p) for p in paths}
 if hashes[str(original)]!=EXE_SHA or hashes[str(input_file)]!=INPUT_SHA:raise ValueError('frozen hash gate')
 reference=[json.loads(x) for x in (base/'single.jsonl').read_text().splitlines()][1:-1];boundaries=json.loads((base/'boundaries.json').read_text())
 environment=dict(os.environ);environment.pop('SE_OBSERVER11',None);base_hash=env_digest(environment)
 # Fixed argv[0] prevents an extra CRT argv-string/layout contrast from image path length.
 argv=[str(original),str(input_file),str(stages),'single']
 result=dict(status='RUNNING',protocol='OBSERVER-FACTORS-11',schedule=schedule(),hashes=hashes,argv=argv,cwd=str(cwd),
  base_environment_sha256=base_hash,environment_values_saved=False,selector=SELECTOR,selector_location='SE_OBSERVER11 environment; four characters',
  stdin='PIPE',stdout='PIPE',creationflags=0x08000000,runs=[],production_changed=False,release='BLOCKED')
 (out/'executed-harness.py').write_bytes(Path(__file__).read_bytes())
 def save():(out/'capture.json').write_text(json.dumps(result,indent=2))
 save()
 try:
  for index,mode in enumerate(schedule(),1):
   before={str(p):sha(p) for p in paths}
   if before!=hashes or env_digest(environment)!=base_hash:raise ValueError('before hash/environment gate')
   env={**environment,'SE_OBSERVER11':SELECTOR[mode]};application=original if mode=='original' else diagnostic
   target=out/f'run-{index:02}-{mode}';target.mkdir();records=[];markers=[];started=time.monotonic()
   with (target/'stderr.txt').open('w') as err:
    process=subprocess.Popen(argv,executable=str(application),cwd=cwd,env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err,text=True,bufsize=1,creationflags=0x08000000)
    mailbox=queue.Queue()
    def reader():
     try:
      for line in process.stdout:mailbox.put(line)
     finally:mailbox.put(None)
    thread=threading.Thread(target=reader,daemon=True);thread.start()
    try:
     with (target/'protocol.jsonl').open('w') as log:
      while True:
       line=mailbox.get(timeout=900)
       if line is None:break
       log.write(line);row=json.loads(line)
       if 'observer11' in row:
        check_marker(row,mode,len(markers),process.pid);markers.append(row)
        if mode=='ack':process.stdin.write('C');process.stdin.flush()
       else:records.append(row)
     code=process.wait(timeout=30)
     if code or len(markers)!=(5 if mode in ('marker','ack') else 0):raise ValueError(('exit/markers',code,len(markers)))
     cps=validate(records,reference)
    finally:
     if process.poll() is None:process.terminate();process.wait(timeout=15)
     process.stdin.close();process.stdout.close();thread.join(timeout=2)
   (target/'replay.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in records))
   with (target/'boundaries.csv').open('w',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=['boundary','cycle','stage','private_bytes','drift_bytes','active_workers','handles','live']);writer.writeheader()
    for cp in cps:writer.writerow(dict(boundary=cp['boundary'],cycle=boundaries[cp['boundary']]['cycle'],stage=boundaries[cp['boundary']]['stage'],**{k:cp[k] for k in ('private_bytes','drift_bytes','active_workers','handles','live')}))
   after={str(p):sha(p) for p in paths}
   if after!=hashes:raise ValueError('after hash gate')
   result['runs'].append(dict(run=index,block=(index-1)//4+1,mode=mode,pid=process.pid,application_name=str(application),argv=argv,environment_sha256=env_digest(env),
    base_environment_sha256=base_hash,selector_value=SELECTOR[mode],before_hashes=before,after_hashes=after,exit_code=code,signatures_verified=274,
    counters=records[-1],markers=len(markers),replay_sha256=sha(target/'replay.jsonl'),protocol_sha256=sha(target/'protocol.jsonl'),elapsed_seconds=time.monotonic()-started));save()
   print(index,mode,process.pid,'PASS',flush=True)
  result['status']='CAPTURE COMPLETE / ALL 16 VALIDATED'
 except Exception as error:result.update(status='FAIL',error=repr(error));raise
 finally:save()
if __name__=='__main__':main()
