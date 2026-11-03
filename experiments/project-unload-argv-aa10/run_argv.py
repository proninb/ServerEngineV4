"""Balanced unchanged shared-only replay; only input/stages path spelling varies."""
import csv,hashlib,json,os,subprocess,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'project-unload-control-07'))
from control_stability import sha,validate
EXE_SHA='e29c95e4d4e4bea399fa532683656f8a39f1e5e737a9b09428056feb35b1c77b'
INPUT_SHA='7571f8b57e92fefbf75954794d6d193b6c4e16c781d21528cc61988ed3e0ab8e'
def schedule():return list('ABBA'*4)
def env_digest(environment):return hashlib.sha256(json.dumps(environment,sort_keys=True,ensure_ascii=True,separators=(',',':')).encode()).hexdigest()
def command(arm,exe,input_file,stages,cwd):
 if arm not in ('A','B'):raise ValueError('arm')
 args=[str(input_file),str(stages)] if arm=='A' else [os.path.relpath(input_file,cwd),os.path.relpath(stages,cwd)]
 return [str(exe),*args,'single']
def main():
 cwd=Path.cwd().resolve();base=(cwd/'build/project-unload-thread-06');out=cwd/'build/project-unload-control-argv-aa-10/run-01';out.mkdir(parents=True,exist_ok=False)
 exe=base/'replay.exe';input_file=base/'strict-replay.bin';stages=base/'stages.bin';boundary_file=base/'boundaries.json';paths=[exe,input_file,stages,boundary_file]
 hashes={str(p):sha(p) for p in paths}
 if hashes[str(exe)]!=EXE_SHA or hashes[str(input_file)]!=INPUT_SHA:raise ValueError('frozen executable/input mismatch')
 reference=[json.loads(x) for x in (base/'single.jsonl').read_text().splitlines()][1:-1];boundaries=json.loads(boundary_file.read_text())
 environment=dict(os.environ);environment_hash=env_digest(environment)
 result=dict(status='RUNNING',protocol='CONTROL-ARGV-AA-10',schedule=schedule(),arm_A='absolute input/stages',arm_B='relative input/stages',
  cwd=str(cwd),stdin_mode='inherit (Popen stdin=None, same as actual CONTROL-07)',creationflags=0x08000000,
  environment_sha256=environment_hash,environment_keys=sorted(environment),environment_values_saved=False,
  hashes=hashes,runs=[],production_changed=False,release='BLOCKED')
 def save():(out/'capture.json').write_text(json.dumps(result,indent=2))
 (out/'executed-harness.py').write_bytes(Path(__file__).read_bytes());save()
 try:
  for index,arm in enumerate(schedule(),1):
   before={str(p):sha(p) for p in paths}
   if before!=hashes or env_digest(environment)!=environment_hash:raise ValueError('before hash/environment gate')
   argv=command(arm,exe,input_file,stages,cwd)
   if (cwd/argv[1]).resolve()!=input_file or (cwd/argv[2]).resolve()!=stages:raise ValueError('argv resolution mismatch')
   stdout=out/f'run-{index:02}-{arm}.jsonl';stderr=out/f'run-{index:02}-{arm}.stderr.txt';started=time.monotonic()
   with stdout.open('w') as log,stderr.open('w') as err:
    process=subprocess.Popen(argv,cwd=cwd,env=environment,stdin=None,stdout=log,stderr=err,creationflags=0x08000000)
    try:code=process.wait(timeout=900)
    except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=15);raise
   if code:raise ValueError(f'exit {index}: {code}')
   rows=[json.loads(line) for line in stdout.read_text().splitlines()];cps=validate(rows,reference)
   after={str(p):sha(p) for p in paths}
   if after!=hashes:raise ValueError('after hash gate')
   with (out/f'run-{index:02}-{arm}.csv').open('w',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=['boundary','cycle','stage','private_bytes','drift_bytes','active_workers','handles','live']);writer.writeheader()
    for cp in cps:writer.writerow(dict(boundary=cp['boundary'],cycle=boundaries[cp['boundary']]['cycle'],stage=boundaries[cp['boundary']]['stage'],**{k:cp[k] for k in ('private_bytes','drift_bytes','active_workers','handles','live')}))
   result['runs'].append(dict(run=index,arm=arm,pid=process.pid,argv=argv,cwd=str(cwd),stdin_mode='inherit',environment_sha256=environment_hash,
    before_hashes=before,after_hashes=after,stdout_sha256=sha(stdout),exit_code=code,signatures_verified=274,counters=rows[-1],elapsed_seconds=time.monotonic()-started))
   print(index,arm,process.pid,'PASS',flush=True);save()
  result['status']='CAPTURE COMPLETE / ALL 16 VALIDATED'
 except Exception as error:result.update(status='FAIL',error=repr(error));raise
 finally:save()
if __name__=='__main__':main()
