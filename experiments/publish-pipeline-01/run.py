"""Rotating three-arm Release+IPO study; all parity checks outside timing."""
from pathlib import Path
import csv,hashlib,json,os,random,statistics,subprocess,sys,time
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve();assert out.is_relative_to(repo/'build')
exe=out/'binaries/PublishPipeline01.exe';logs=out/'timing-logs';logs.mkdir(exist_ok=False)
inputs=json.loads((out/'input-manifest.json').read_text());binary=json.loads((out/'binary.json').read_text())
base={k:v for k,v in os.environ.items() if not k.startswith(('PUBLISH_PIPELINE_01_','PLS','CW_'))}
arms=('baseline','serial','overlap');samples=[];checks=[];gold={}

def sha(path):
 with path.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
assert sha(exe)==binary['sha256']

def fields(text):
 result={}
 for line in text.splitlines():
  for part in line.split(','):
   if '=' not in part:continue
   k,v=part.split('=',1)
   try:result[k]=float(v)
   except ValueError:result[k]=v
 return result

def run(corpus,arm,operation,tag,audit=False):
 artifact=out/'artifacts'/corpus/arm
 env=dict(base,PUBLISH_PIPELINE_01_MODE=arm,PUBLISH_PIPELINE_01_ARTIFACT_ROOT=str(artifact))
 if audit:env['PUBLISH_PIPELINE_01_SHM_AUDIT']='1'
 begun=time.time();r=subprocess.run([str(exe),operation,inputs[corpus]['project'],str(inputs[corpus]['expected_types'])],
  capture_output=True,text=True,env=env,timeout=240)
 name=f'{corpus}-{tag}-{arm}-{operation}'
 (logs/(name+'.stdout')).write_text(r.stdout);(logs/(name+'.stderr')).write_text(r.stderr)
 assert r.returncode==0,(name,r.returncode,r.stderr[-5000:])
 row=fields(r.stdout);assert 'total_ms' in row,(name,r.stdout[-1000:])
 if corpus=='unitproxl':assert (row['types'],row['objects'],row['links'])==(24027,137584,367144)
 payload={n:sha(artifact/n) for n in ('compiled.bin','runtime.bin')}
 if corpus not in gold:gold[corpus]=payload
 assert gold[corpus]==payload,(name,'artifact mismatch',payload,gold[corpus])
 if audit:
  h=row['pipeline_shm_sha256'];key=corpus+'-shm'
  if key not in gold:gold[key]=h
  assert h==gold[key],(name,'SHM mismatch',h,gold[key])
 if operation in ('publish','publish-timeline') and arm!='baseline':
  assert row['pipeline_type_adoptions']==1 and row['pipeline_prepared_consumers']==1 and row['pipeline_consumer_success']==1,name
  if arm=='serial':assert row['pipeline_overlap_ms']==0,name
 if operation=='publish-timeline':
  assert row['persistence_flush_enabled']==0 and row['compiled_flush_ms']==0
  # Candidate SHM is inside persistence; this combined wall interval is
  # comparable across all arms, unlike persistence_ms alone.
  row['persistence_resident_ms']=row['persistence_ms']+row['resident_ms']
 row.update(corpus=corpus,arm=arm,operation=operation,tag=tag,start_unix=begun)
 print(name,round(row['total_ms'],3),'ms',flush=True)
 return row

for corpus in ('synthetic','unitproxl'):
 for i,arm in enumerate(arms+tuple(reversed(arms))):checks.append(run(corpus,arm,'publish',f'warm-{i}',i<3))
 for arm in arms:
  checks.append(run(corpus,arm,'audit','pre',True));checks.append(run(corpus,arm,'load','pre',True))
 for series,operation in (('native','publish'),('profile','publish-timeline')):
  for i in range(15):
   order=list(arms[i%3:]+arms[:i%3])
   if i%2:order.reverse()
   for arm in order:
    row=run(corpus,arm,operation,f'{series}-{i:02d}')
    row.update(series=series,pair=i);samples.append(row)
   (out/'samples.json').write_text(json.dumps(samples,indent=2))
 for arm in arms:
  checks.append(run(corpus,arm,'audit','post',True));checks.append(run(corpus,arm,'load','post',True))
 (out/'checks.json').write_text(json.dumps(checks,indent=2));(out/'parity.json').write_text(json.dumps(gold,indent=2))

assert sha(exe)==binary['sha256']
manifest=json.loads((out/'source-manifest.json').read_text());root=out/'source'
assert all(sha(root/k)==v for k,v in manifest.items()),'frozen sources changed'
for corpus,data in inputs.items():
 assert all(sha(Path(data['root'])/k)==v for k,v in data['manifest'].items()),('input changed',corpus)

summary={}
for corpus in ('synthetic','unitproxl'):
 summary[corpus]={}
 for series in ('native','profile'):
  rows=[r for r in samples if r['corpus']==corpus and r['series']==series]
  metrics=['total_ms','peak_ws_bytes','peak_private_commit_bytes','compiled_bytes','runtime_bytes',
   'pipeline_window_ms','pipeline_write_ms','pipeline_resident_ms','pipeline_overlap_ms']
  if series=='profile':metrics+=['semantic_ms','persistence_ms','resident_ms','persistence_resident_ms','compiled_encode_ms',
   'compiled_plan_ms','pipeline_resident_prepare_ms','pipeline_mark_ms','pipeline_objects_ms','pipeline_links_ms',
   'pipeline_initializations_ms','pipeline_shm_create_ms','pipeline_object_lanes','pipeline_mark_lanes']
  stat={}
  for control,candidate in (('baseline','serial'),('baseline','overlap'),('serial','overlap')):
   comparisons={}
   for metric in metrics:
    a=[next(r[metric] for r in rows if r['pair']==i and r['arm']==control) for i in range(15)]
    b=[next(r[metric] for r in rows if r['pair']==i and r['arm']==candidate) for i in range(15)]
    gains=[x-y for x,y in zip(a,b)];rng=random.Random(1101)
    boot=sorted(statistics.median(rng.choices(gains,k=15)) for _ in range(20000))
    comparisons[metric]=dict(control_median=statistics.median(a),candidate_median=statistics.median(b),
      paired_gain=statistics.median(gains),bootstrap95_gain=[boot[500],boot[19499]],candidate_wins=sum(g>0 for g in gains),
      paired_median_change_percent=statistics.median((y/x-1)*100 for x,y in zip(a,b)) if all(a) else None,
      control_min=min(a),control_max=max(a),candidate_min=min(b),candidate_max=max(b))
   stat[control+'->'+candidate]=comparisons
  summary[corpus][series]=stat
result=dict(binary=binary,pairs=15,arms=arms,timed_runs=len(samples),checks=len(checks),summary=summary,
 frozen_sources_inputs_binary_unchanged=True,parity='whole compiled.bin + whole runtime.bin + PUBLISH/audit/LOAD raw SHM')
(out/'summary.json').write_text(json.dumps(result,indent=2))
with (out/'samples.csv').open('w',newline='') as f:
 keys=sorted(set().union(*(r.keys() for r in samples)));w=csv.DictWriter(f,fieldnames=keys);w.writeheader();w.writerows(samples)
print(json.dumps(summary,indent=2),flush=True)
