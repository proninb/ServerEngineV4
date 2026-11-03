"""C10 baseline + string observer off/on and sampling-density controls."""
import hashlib,json,os,random,shutil,statistics,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
shutil.copytree(repo/'build/parallel-local-source-08/publish-c-final/fixture',out/'fixture',ignore=shutil.ignore_patterns('.serverengine','__pycache__'))
bins={}
for name,relative in [('c10','parallel-local-source-10-c10/Release/PublishScratch10C10.exe'),('profile','parallel-local-source-10-c11/Release/PublishStringProfile10C11.exe'),('audit','parallel-local-source-10-c11/Release/StringAllocationAudit10C11.exe')]:
 bins[name]=out/(name+'.exe');shutil.copy2(repo/'build'/relative,bins[name])
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
hashes={k:sha(p) for k,p in bins.items()};rows=[];gold=None;cold={}
base={k:v for k,v in os.environ.items() if not k.startswith(('PLS','CW_RUNTIME_SHM_AUDIT'))}
def run(label,arm,seed,verify=False):
 global gold
 env=dict(base,PLS08_CANDIDATE='1',PLS10_C7_FAST='1',PLS10_C10_REUSE='1',PLS10_WORKERS='4',PLS10_C11_SEED=str(seed))
 if arm.startswith('p'):env.update(PLS10_C11_PROFILE='1',PLS10_C11_SHIFT=arm[1:])
 if verify:env['PLS08_OBJECT_AUDIT']='1'
 exe=bins['c10' if arm=='c10' else 'audit' if arm=='audit' else 'profile']
 p=subprocess.run([str(exe),'publish',str(out/'fixture/project.json'),'0'],env=env,capture_output=True,text=True,timeout=180)
 tag=f'{label}-{arm}';(out/(tag+'.stdout')).write_text(p.stdout);(out/(tag+'.stderr')).write_text(p.stderr);assert p.returncode==0,(tag,p.stderr[-2500:])
 f={}
 for line in p.stdout.splitlines():
  for item in line.split(','):
   if '=' in item:
    k,v=item.split('=',1)
    try:f[k]=float(v)
    except ValueError:pass
 d={name:sha(out/'fixture/.serverengine/project.json'/name) for name in ('compiled.bin','runtime.bin')}
 if gold is None:gold=d
 assert gold==d,(tag,'artifact parity')
 if verify:
  cold[arm]=next(s for s in p.stdout.splitlines() if s.startswith('pls08_object_storage_sha256='))
  assert 'cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in cold[arm]
 if arm.startswith('p'):assert f['pls11_statements']==15899 and f['pls11_elements']==508768
 elif arm=='off':assert f['pls11_profile']==0 and f['pls11_string_ms']==0
 if arm!='audit':assert f.get('c7_fast_calls',0)==0 and f.get('c7_verified',0)==0 and not any(k.startswith('c9_') for k in f)
 print(tag,round(f['total_ms'],2),round(f.get('pls11_string_ms',0),2),flush=True)
 return dict(label=label,arm=arm,**f)
for arm in ('c10','off','p10','p8','audit'):run('cold',arm,111,True)
audit=run('counts','audit',111);(out/'audit.json').write_text(json.dumps(audit,indent=2))
arms=['c10','off','p10','p8']
for i in range(15):
 order=arms[i%4:]+arms[:i%4]
 if i%2:order=order[::-1]
 for arm in order:rows.append(run(str(i),arm,11000+i*997))
 (out/'samples.json').write_text(json.dumps(rows,indent=2))
comparisons={}
for control,candidate in [('c10','off'),('off','p10'),('off','p8'),('c10','p10')]:
 result={}
 for key in ('total_ms','pls08_linking_ms','pls08_full_with_teardown_ms','peak_ws_bytes'):
  a=[next(r[key] for r in rows if r['label']==str(i) and r['arm']==control) for i in range(15)];b=[next(r[key] for r in rows if r['label']==str(i) and r['arm']==candidate) for i in range(15)];g=[x-y for x,y in zip(a,b)]
  rng=random.Random(111);boot=sorted(statistics.median(rng.choices(g,k=15)) for _ in range(20000))
  result[key]=dict(control_median=statistics.median(a),candidate_median=statistics.median(b),paired_gain=statistics.median(g),bootstrap95=[boot[500],boot[19499]],candidate_wins=sum(x>0 for x in g))
 comparisons[control+'->'+candidate]=result
profiles={a:{k:dict(median=statistics.median(r[k] for r in rows if r['arm']==a),minimum=min(r[k] for r in rows if r['arm']==a),maximum=max(r[k] for r in rows if r['arm']==a)) for k in rows[-1] if k.startswith('pls11_') and k not in ('pls11_seed',)} for a in ('p10','p8')}
assert hashes=={k:sha(p) for k,p in bins.items()}
(out/'summary.json').write_text(json.dumps(dict(binary_hashes=hashes,cold=cold,artifact_hashes=gold,pairs=15,comparisons=comparisons,profiles=profiles),indent=2));print(json.dumps(comparisons,indent=2))
