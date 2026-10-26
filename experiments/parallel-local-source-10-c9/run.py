"""Frozen C7/V2 control, then separate coarse observer overhead and profiles.
All hashes/cold native storage checks run outside measured child PUBLISH.
"""
import hashlib,json,os,random,shutil,statistics,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
shutil.copytree(repo/'build/parallel-local-source-08/publish-c-final/fixture',out/'fixture',ignore=shutil.ignore_patterns('.serverengine','__pycache__'))
bins={}
for name,experiment,exe in [('baseline','parallel-local-source-10-c7','PublishTypedView10C7'),('coarse','parallel-local-source-10-c9','PublishCost10C9')]:
 bins[name]=out/(name+'.exe');shutil.copy2(repo/'build'/experiment/'Release'/(exe+'.exe'),bins[name])
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
hashes={k:sha(p) for k,p in bins.items()};rows=[];gold={};cold={}
base={k:v for k,v in os.environ.items() if not k.startswith(('PLS','CW_RUNTIME_SHM_AUDIT'))}
def run(label,arm,binary='baseline',profile=False,mode='publish',verify=False):
 env=dict(base,PLS08_CANDIDATE=str(int(arm=='c7')),PLS10_WORKERS='4',PLS10_C7_FAST='1')
 if profile:env['PLS10_C9_PROFILE']='1'
 if verify:env['PLS08_OBJECT_AUDIT']='1'
 r=subprocess.run([str(bins[binary]),mode,str(out/'fixture/project.json'),'0'],env=env,capture_output=True,text=True,timeout=180)
 tag=f'{label}-{arm}-{binary}-{int(profile)}';(out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr)
 assert r.returncode==0,(tag,r.stderr[-2500:]);f={}
 for line in r.stdout.splitlines():
  for item in line.split(','):
   if '=' in item:
    k,v=item.split('=',1)
    try:f[k]=float(v)
    except ValueError:pass
 d={name:sha(out/'fixture/.serverengine/project.json'/name) for name in ('compiled.bin','runtime.bin')}
 if arm not in gold:gold[arm]=d
 assert gold[arm]==d,(tag,'artifact mismatch')
 if verify:
  cold[tag]=next(s for s in r.stdout.splitlines() if s.startswith('pls08_object_storage_sha256='))
  assert 'cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in cold[tag]
 row=dict(label=label,arm=arm,binary=binary,profile=profile,mode=mode,**f);print(tag,round(f['total_ms'],2),flush=True);return row
for arm in ('v2','c7'):
 run('cold',arm,verify=True);run('cold',arm,'coarse',True,verify=True)
for i in range(15):
 for arm in (('v2','c7') if i%2==0 else ('c7','v2')):rows.append(run(str(i),arm))
 (out/'samples.json').write_text(json.dumps(rows,indent=2))
# Same-executable observer off/on plus original binary checks; no fine timers.
for i in range(7):
 arms=[('baseline',False),('coarse',False),('coarse',True)]
 if i%2:arms.reverse()
 for arm in ('v2','c7'):
  for binary,on in arms:rows.append(run('profile'+str(i),arm,binary,on,'publish-timeline'))
 (out/'samples.json').write_text(json.dumps(rows,indent=2))
def paired(a,b):
 g=[x-y for x,y in zip(a,b)];rng=random.Random(109);boot=sorted(statistics.median(rng.choices(g,k=len(g))) for _ in range(20000))
 return dict(control_median=statistics.median(a),candidate_median=statistics.median(b),paired_gain=statistics.median(g),bootstrap95=[boot[500],boot[19499]],wins=sum(x>0 for x in g))
timed=[r for r in rows if r['mode']=='publish']
summary={'baseline':paired([r['total_ms'] for r in timed if r['arm']=='v2'],[r['total_ms'] for r in timed if r['arm']=='c7'])}
for arm in ('v2','c7'):
 p=[r for r in rows if r['arm']==arm and r['mode']=='publish-timeline'];off=[r for r in p if r['binary']=='coarse' and not r['profile']];on=[r for r in p if r['profile']];original=[r for r in p if r['binary']=='baseline']
 summary[arm]={'observer_overhead':paired([r['total_ms'] for r in off],[r['total_ms'] for r in on]),'regenerated_vs_original':paired([r['total_ms'] for r in original],[r['total_ms'] for r in off]),'profile_medians':{k:statistics.median(r[k] for r in on) for k in on[0] if k.startswith('c9_') or k.endswith('_ms') or k=='peak_ws_bytes'}}
assert hashes=={k:sha(p) for k,p in bins.items()}
(out/'summary.json').write_text(json.dumps(dict(binary_hashes=hashes,cold=cold,artifact_hashes=gold,results=summary),indent=2));print(json.dumps(summary,indent=2))
