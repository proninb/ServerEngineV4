"""Cold invariants/cache census + independent empty-scope floor after timing."""
import hashlib,json,os,statistics,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve();assert out.is_relative_to(repo/'build')
s=json.loads((out/'summary.json').read_text());frozen=json.loads((out/'audit.json').read_text())
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert all(sha(out/(n+'.exe'))==h for n,h in s['binary_hashes'].items())
env={k:v for k,v in os.environ.items() if not k.startswith(('PLS','CW_RUNTIME_SHM_AUDIT'))}
env.update(PLS08_CANDIDATE='1',PLS10_C7_FAST='1',PLS10_C10_REUSE='1',PLS10_WORKERS='4',PLS08_OBJECT_AUDIT='1')
exe=repo/'build/parallel-local-source-10-c14/Release/CommitAllocationAudit10C14.exe'
p=subprocess.run([str(exe),'publish',str(out/'fixture/project.json'),'0'],env=env,capture_output=True,text=True,timeout=180)
(out/'counter-audit.stdout').write_text(p.stdout);(out/'counter-audit.stderr').write_text(p.stderr);assert p.returncode==0,p.stderr[-2000:]
a={}
for line in p.stdout.splitlines():
 for item in line.split(','):
  if '=' in item:
   k,v=item.split('=',1)
   try:a[k]=float(v)
   except ValueError:pass
assert 'pls08_object_storage_sha256=cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in p.stdout
assert all(sha(out/'fixture/.serverengine/project.json'/n)==h for n,h in s['artifact_hashes'].items())
expected=dict(identity_find=1169037,identity_resolve=137584,object=137584,path=1216586,initialization=0,batch=0,batch_records=0,slot_apply=705238,rebind=367144,add_link=367144,init_lookup=705238,link_lookup=767104)
assert all(a['c9_audit_'+k]==v for k,v in expected.items())
expected_calls=dict(event=701197,declare=137584,endpoint=930757,name=2758075,symbol=1153138,type=137584,temporal=36083,follow=32816,encode=963574,path=707818,slot=196470,link=367144,object=137584,origin=563614,provenance=897668,warning=0)
assert all(a['pls14_'+k+'_calls']==v for k,v in expected_calls.items())
assert a['pls14_string_events']==15899 and a['ordered_conflicts']==0
assert a['pls14_name_hits']+a['pls14_name_misses']==a['pls14_name_calls']
assert a['pls14_type_hits']+a['pls14_type_misses']==a['pls14_type_calls']
assert a['c9_audit_allocations']==993193==sum(a[f'c9_audit_group{g}_calls'] for g in range(1,7))
assert all(a[k]==v for k,v in frozen.items() if k.startswith('pls14_') and (k.endswith('_calls') or k.endswith('_allocations'))),'cold audit work changed'
(out/'counter-audit.json').write_text(json.dumps(dict(binary_hash=sha(exe),fields=a),indent=2))
calibration=repo/'build/parallel-local-source-10-c14/Release/CommitClockCalibration10C14.exe';rows=[]
for seed in range(1,10):
 p=subprocess.run([str(calibration)],env=dict(env,PLS10_C14_PROFILE='1',PLS10_C14_SHIFT='6',PLS10_C14_SEED=str(seed)),capture_output=True,text=True,timeout=30);assert p.returncode==0,p.stderr
 rows.append({k:float(v) for k,v in (x.split('=') for x in p.stdout.strip().split(','))})
parts=('outside','event','declare','endpoint','name','symbol','type','temporal','follow','encode','path','slot','link','object','origin','provenance','warning')
result=dict(nonstring_allocations=sum(a['pls14_'+k+'_allocations'] for k in parts),nonstring_requested=sum(a['pls14_'+k+'_requested'] for k in parts),empty_scope_median_ns=statistics.median(r['mean_empty_sample_ns'] for r in rows),calibration_hash=sha(calibration),calibration=rows)
(out/'analysis.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2));print('C10 counters, scoped work, caches, artifact/native storage parity and frozen hashes PASS')
