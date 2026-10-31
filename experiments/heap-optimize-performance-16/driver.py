import hashlib,json,os,subprocess,sys,time
from pathlib import Path
ROOT=Path('build/heap-optimize-performance-16').resolve()
ORDER=('OFF_','POL_','POL_','OFF_')
def main():
 executable=Path('build/heap-optimize-policy-15/compiler/Release/ServerEngineV4.exe').resolve();digest=hashlib.sha256(executable.read_bytes()).hexdigest()
 environment=dict(os.environ);environment.pop('SE_HEAP_OPT15',None)
 result=dict(status='RUNNING',binary_sha256=digest,seconds_per_run=600,order=ORDER,calibration=json.loads((ROOT/'calibration.json').read_text()),runs=[],production_changed=False,pgo='OFF')
 def save():(ROOT/'driver-status.json').write_text(json.dumps(result,indent=2))
 save()
 try:
  for index,mode in enumerate(ORDER,1):
   if hashlib.sha256(executable.read_bytes()).hexdigest()!=digest:raise ValueError('before binary hash')
   run=f'run-{index:02}-{mode}'
   result['active_run']=run;save()
   with (ROOT/(run+'-console.log')).open('w') as log:
    process=subprocess.Popen([sys.executable,'experiments/heap-optimize-performance-16/run_soak.py',str(ROOT),'--seconds','600','--arm',mode,'--run-id',run],env=environment,stdout=log,stderr=subprocess.STDOUT,creationflags=0x08000000)
    code=process.wait()
   status=json.loads((ROOT/run/'status.json').read_text())
   if 'quiescent_last5_minus_first5_by_case' not in status:raise RuntimeError('Correctness/incomplete run: '+run)
   if hashlib.sha256(executable.read_bytes()).hexdigest()!=digest:raise ValueError('after binary hash')
   result['runs'].append(dict(run=run,mode=mode,exit_code=code,status=status['status'],gate=status['quiescent_last5_minus_first5_by_case']));save()
  result.update(status='COMPLETE / DIAGNOSTIC ONLY',active_run=None)
 except Exception as error:result.update(status='FAIL',error=repr(error));raise
 finally:save()
if __name__=='__main__':main()
