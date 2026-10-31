"""Sequential 600-second OFF/ON controls on one diagnostic executable."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path('build/heap-optimize-resources-14').resolve()
def main():
 exe=ROOT/'compiler/Release/ServerEngineV4.exe';digest=hashlib.sha256(exe.read_bytes()).hexdigest()
 fixed_env=dict(os.environ);fixed_env.pop('SE_HEAP_OPT14',None)
 result=dict(status='RUNNING',binary_sha256=digest,pgo='OFF',seconds_per_arm=600,arms={},production_changed=False)
 def save():(ROOT/'driver-status.json').write_text(json.dumps(result,indent=2))
 save()
 try:
  for mode in ('OFF_','ON__'):
   with (ROOT/('soak-'+mode+'-console.log')).open('w') as log:
    process=subprocess.Popen([sys.executable,'experiments/heap-optimize-resources-14/run_soak.py',str(ROOT),'--seconds','600','--arm',mode],env=fixed_env,stdout=log,stderr=subprocess.STDOUT,creationflags=0x08000000)
    code=process.wait()
   status=json.loads((ROOT/('soak-'+mode)/'status.json').read_text())
   if 'quiescent_last5_minus_first5_by_case' not in status:raise RuntimeError('Correctness/incomplete soak failure: '+mode)
   if hashlib.sha256(exe.read_bytes()).hexdigest()!=digest:raise RuntimeError('Diagnostic binary changed')
   result['arms'][mode]=dict(exit_code=code,status=status['status'],memory_gate=status['quiescent_last5_minus_first5_by_case']);save()
  result['status']='COMPLETE / DIAGNOSTIC ONLY'
 except Exception as error:result.update(status='FAIL',error=repr(error));raise
 finally:save()
if __name__=='__main__':main()
