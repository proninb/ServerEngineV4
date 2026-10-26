"""Frozen original C7, same-binary scratch off/on, and V2. No hot observers."""
import hashlib,json,os,random,shutil,statistics,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
shutil.copytree(repo/'build/parallel-local-source-08/publish-c-final/fixture',out/'fixture',ignore=shutil.ignore_patterns('.serverengine','__pycache__'))
bins={}
for name,relative in [('c7','parallel-local-source-10-c7/Release/PublishTypedView10C7.exe'),('native','parallel-local-source-10-c10/Release/PublishScratch10C10.exe'),('audit','parallel-local-source-10-c10/Release/SourceAllocationAudit10C10.exe')]:
 bins[name]=out/(name+'.exe');shutil.copy2(repo/'build'/relative,bins[name])
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
hashes={k:sha(p) for k,p in bins.items()};rows=[];gold={};cold={}
base={k:v for k,v in os.environ.items() if not k.startswith(('PLS','CW_RUNTIME_SHM_AUDIT'))}
def run(label,arm,verify=False,audit=False):
 env=dict(base,PLS08_CANDIDATE=str(int(arm!='v2')),PLS10_C7_FAST='1',PLS10_WORKERS='4',PLS10_C10_REUSE=str(int(arm!='off')))
 if verify:env['PLS08_OBJECT_AUDIT']='1'
 exe=bins['audit' if audit else 'c7' if arm in ('c7','v2') else 'native']
 r=subprocess.run([str(exe),'publish',str(out/'fixture/project.json'),'0'],env=env,capture_output=True,text=True,timeout=180)
 tag=f'{label}-{arm}'+('-audit' if audit else '')
 (out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr);assert r.returncode==0,(tag,r.stderr[-2500:])
 fields={}
 for line in r.stdout.splitlines():
  for item in line.split(','):
   if '=' in item:
    k,v=item.split('=',1)
    try:fields[k]=float(v)
    except ValueError:pass
 domain='v2' if arm=='v2' else 'c7';d={name:sha(out/'fixture/.serverengine/project.json'/name) for name in ('compiled.bin','runtime.bin')}
 if not audit:
  assert fields.get('c7_fast_calls',0)==0 and fields.get('c7_verified',0)==0,'native hot audit enabled'
  assert not any(k.startswith('c9_') for k in fields),'coarse observer in native benchmark'
 if domain not in gold:gold[domain]=d
 assert gold[domain]==d,(tag,'artifact parity')
 if verify:
  cold[tag]=next(s for s in r.stdout.splitlines() if s.startswith('pls08_object_storage_sha256='))
  assert 'cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in cold[tag]
 print(tag,round(fields['total_ms'],2),flush=True)
 return dict(label=label,arm=arm,**fields)
for arm in ('v2','c7','off','reuse'):run('cold',arm,True)
for arm in ('off','reuse'):run('cold',arm,True,True)
arms=['v2','c7','off','reuse']
for i in range(15):
 order=arms[i%4:]+arms[:i%4]
 if i%2:order=order[::-1]
 for arm in order:rows.append(run(str(i),arm))
 (out/'samples.json').write_text(json.dumps(rows,indent=2))
summary={}
for control,candidate in [('off','reuse'),('c7','off'),('c7','reuse'),('v2','reuse')]:
 result={}
 for metric in ('total_ms','pls08_linking_ms','pls08_full_with_teardown_ms','peak_ws_bytes'):
  if control=='v2' and metric=='pls08_linking_ms':continue
  a=[next(r[metric] for r in rows if r['label']==str(i) and r['arm']==control) for i in range(15)]
  b=[next(r[metric] for r in rows if r['label']==str(i) and r['arm']==candidate) for i in range(15)];g=[x-y for x,y in zip(a,b)]
  rng=random.Random(110);boot=sorted(statistics.median(rng.choices(g,k=15)) for _ in range(20000))
  result[metric]=dict(control_median=statistics.median(a),candidate_median=statistics.median(b),paired_gain=statistics.median(g),bootstrap95=[boot[500],boot[19499]],wins=sum(x>0 for x in g))
 summary[control+'->'+candidate]=result
assert hashes=={k:sha(p) for k,p in bins.items()}
(out/'summary.json').write_text(json.dumps(dict(binary_hashes=hashes,artifact_hashes=gold,cold=cold,pairs=15,comparisons=summary),indent=2));print(json.dumps(summary,indent=2))
