"""Validate attribution invariants and independently measure an empty scope."""
import hashlib,json,os,statistics,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
s=json.loads((out/'summary.json').read_text());a=json.loads((out/'audit.json').read_text())
for name,digest in s['binary_hashes'].items():
 with (out/(name+'.exe')).open('rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==digest
assert a['pls11_path0_calls']+a['pls11_path1_calls']==a['c9_audit_path']==1216586
for i in (0,1):assert a[f'pls11_path{i}_hits']+a[f'pls11_path{i}_new']+a[f'pls11_path{i}_failed']==a[f'pls11_path{i}_calls']
assert a['pls11_elements']==a['pls11_string_slot_calls']==a['pls11_string_slot_applied']==508768
assert a['pls11_string_slot_existing']==0 and a['pls11_path1_failed']==0
assert a['c9_audit_allocations']==sum(a[f'c9_audit_group{g}_calls'] for g in range(1,7))==993193
assert a['c9_audit_requested']==sum(a[f'c9_audit_group{g}_requested'] for g in range(1,7))
expected=dict(identity_find=1169037,identity_resolve=137584,object=137584,path=1216586,initialization=0,batch=0,batch_records=0,slot_apply=705238,rebind=367144,add_link=367144,init_lookup=705238,link_lookup=767104)
assert all(a['c9_audit_'+k]==v for k,v in expected.items()),'C10 semantic/Graph counters changed'
parts=('other','element','path','slot','provenance')
calls=sum(a[f'pls11_{p}_allocations'] for p in parts);requested=sum(a[f'pls11_{p}_requested'] for p in parts)
assert calls==31924
calibration=repo/'build/parallel-local-source-10-c11/Release/StringClockCalibration10C11.exe';samples=[]
for seed in range(1,10):
 env=dict(os.environ,PLS10_C11_PROFILE='1',PLS10_C11_SHIFT='6',PLS10_C11_SEED=str(seed))
 p=subprocess.run([str(calibration)],env=env,capture_output=True,text=True,timeout=30);assert p.returncode==0,p.stderr
 f={k:float(v) for k,v in (x.split('=') for x in p.stdout.strip().split(','))};samples.append(f)
with calibration.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
result=dict(string_allocation_calls=calls,string_requested_bytes=requested,allocation_share_percent=100*calls/a['c9_audit_allocations'],path_hit_percent=100*a['pls11_path1_hits']/a['pls11_path1_calls'],slots_past_literal_length=a['pls11_elements']-a['pls11_literal_bytes'],calibration_hash=digest,calibration=samples,empty_scope_median_ns=statistics.median(x['mean_empty_sample_ns'] for x in samples))
(out/'analysis.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
