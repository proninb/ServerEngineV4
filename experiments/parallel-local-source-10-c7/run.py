"""Frozen uninstrumented C5/off/fast/V2 series; audit is cold and separate."""
import hashlib,json,os,random,shutil,statistics,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
shutil.copytree(repo/'build/parallel-local-source-08/publish-c-final/fixture',out/'fixture',ignore=shutil.ignore_patterns('.serverengine','__pycache__'))
bins={}
for arm,experiment,name in [('c5','parallel-local-source-10-c5','PublishStreamingSource10C5'),('native','parallel-local-source-10-c7','PublishTypedView10C7'),('audit','parallel-local-source-10-c7','TypedViewAudit10C7')]:
 bins[arm]=out/(name+'.exe');shutil.copy2(repo/'build'/experiment/'Release'/(name+'.exe'),bins[arm])
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
binary={k:sha(p) for k,p in bins.items()};rows=[];gold={};cold={};audit_fields={}
environment={k:v for k,v in os.environ.items() if not k.startswith(('PLS','CW_RUNTIME_SHM_AUDIT'))}
def run(label,arm,verify=False):
 global audit_fields
 env=dict(environment,PLS08_CANDIDATE=str(int(arm!='v2')),PLS10_WORKERS='4',PLS10_C7_FAST=str(int(arm in ('fast','audit'))))
 if verify:env['PLS08_OBJECT_AUDIT']='1'
 exe=bins['c5' if arm=='c5' else 'audit' if arm=='audit' else 'native']
 r=subprocess.run([str(exe),'publish',str(out/'fixture/project.json'),'0'],env=env,capture_output=True,text=True,timeout=180)
 (out/f'{label}-{arm}.stdout').write_text(r.stdout);(out/f'{label}-{arm}.stderr').write_text(r.stderr);assert r.returncode==0,(label,arm,r.stderr[-2000:])
 fields={}
 for line in r.stdout.splitlines():
  for item in line.split(','):
   if '=' in item:
    k,v=item.split('=',1)
    try:fields[k]=float(v)
    except ValueError:pass
 artifacts=out/'fixture/.serverengine/project.json';domain='v2' if arm=='v2' else 'c5';digest={f:sha(artifacts/f) for f in ('compiled.bin','runtime.bin')}
 if domain not in gold:gold[domain]=digest
 assert gold[domain]==digest,(label,arm,'artifact parity failed')
 if verify:
  cold[arm]=next(line for line in r.stdout.splitlines() if line.startswith('pls08_object_storage_sha256='))
  assert 'cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in cold[arm]
 if arm=='audit':
  audit_fields=fields
  assert fields['c7_fast_calls']+fields['c7_fallback_calls']==1439526
  assert fields['c7_verified']==fields['c7_fast_calls']>0
 elif arm in ('fast','off'):assert fields['c7_fast_calls']==0 and fields['c7_verified']==0,'hot counters enabled in native executable'
 print(label,arm,round(fields['total_ms'],2),round(fields['pls08_linking_ms'],2),flush=True)
 return dict(label=label,arm=arm,**fields)
for arm in ('c5','off','fast','audit','v2'):run('cold',arm,True)
assert len(set(cold.values()))==1
arms=['c5','off','fast','v2']
for i in range(15):
 order=arms[i%4:]+arms[:i%4]
 if i%2:order=order[::-1]
 for arm in order:rows.append(run(str(i),arm))
 (out/'samples.json').write_text(json.dumps(rows,indent=2))
summary={}
for control,candidate in [('c5','off'),('off','fast'),('c5','fast'),('v2','fast')]:
 summary[control+'->'+candidate]={}
 for metric in ('total_ms','pls08_linking_ms','pls08_full_with_teardown_ms','peak_ws_bytes'):
  if control=='v2' and metric=='pls08_linking_ms':continue # V2 does not populate this experiment timer
  pairs=[(next(r[metric] for r in rows if r['label']==str(i) and r['arm']==control),next(r[metric] for r in rows if r['label']==str(i) and r['arm']==candidate)) for i in range(15)]
  gains=[a-b for a,b in pairs];rng=random.Random(107);boot=sorted(statistics.median(rng.choices(gains,k=15)) for _ in range(20000))
  summary[control+'->'+candidate][metric]=dict(control_median=statistics.median(a for a,b in pairs),candidate_median=statistics.median(b for a,b in pairs),paired_gain_ms=statistics.median(gains),bootstrap95=[boot[500],boot[19499]],candidate_wins=sum(g>0 for g in gains))
assert binary=={k:sha(p) for k,p in bins.items()}
(out/'summary.json').write_text(json.dumps(dict(binary_hashes=binary,cold=cold,digests=gold,audit=audit_fields,pairs=15,comparisons=summary),indent=2));print(json.dumps(summary,indent=2))
