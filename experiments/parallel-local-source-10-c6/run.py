"""Frozen observer-control series; no algorithm candidate is adopted here."""
import hashlib,json,os,random,shutil,statistics,subprocess,sys
from pathlib import Path
repo=Path(__file__).resolve().parents[2];out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build') and not out.exists();out.mkdir(parents=True)
shutil.copytree(repo/'build/parallel-local-source-08/publish-c-final/fixture',out/'fixture',ignore=shutil.ignore_patterns('.serverengine','__pycache__'))
bins={}
for name,directory in [('baseline','parallel-local-source-10-c5'),('observer','parallel-local-source-10-c6')]:
 source='PublishStreamingSource10C5' if name=='baseline' else 'PublishCommitAudit10C6'
 bins[name]=out/(source+'.exe');shutil.copy2(repo/'build'/directory/'Release'/(source+'.exe'),bins[name])
def sha(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
binary={k:sha(p) for k,p in bins.items()};rows=[];gold=None;cold={};profiles={}
environment={k:v for k,v in os.environ.items() if not k.startswith(('PLS','CW_RUNTIME_SHM_AUDIT'))}
def run(label,arm,audit=False,seed=198):
 global gold
 mode={'baseline':0,'off':0,'counts':1,'sample':3}[arm]
 env=dict(environment,PLS08_CANDIDATE='1',PLS10_WORKERS='4',PLS10_C6=str(mode),PLS10_C6_SEED=str(seed),PLS10_C6_REPORT=str(out/f'{label}-{arm}.json'))
 if audit:env['PLS08_OBJECT_AUDIT']='1'
 r=subprocess.run([str(bins['baseline' if arm=='baseline' else 'observer']),'publish',str(out/'fixture/project.json'),'0'],env=env,capture_output=True,text=True,timeout=180)
 (out/f'{label}-{arm}.stdout').write_text(r.stdout);(out/f'{label}-{arm}.stderr').write_text(r.stderr)
 assert r.returncode==0,(label,arm,r.stdout,r.stderr)
 fields={}
 for line in r.stdout.splitlines():
  for part in line.split(','):
   if '=' in part:
    k,v=part.split('=',1)
    try:fields[k]=float(v)
    except ValueError:pass
 artifacts=out/'fixture/.serverengine/project.json';digest={f:sha(artifacts/f) for f in ('compiled.bin','runtime.bin')}
 if gold is None:gold=digest
 assert gold==digest,(label,arm,'observer changed output')
 if arm!='baseline':profiles[label,arm]=json.loads((out/f'{label}-{arm}.json').read_text())
 if audit:
  cold[arm]=next(s for s in r.stdout.splitlines() if s.startswith('pls08_object_storage_sha256='))
  assert 'cb5aa2607c10f928b273ad8f5d33c29f3656190b3ec9597550cbf068a5f0d0e2' in cold[arm]
 print(label,arm,round(fields['total_ms'],2),round(fields['pls08_linking_ms'],2),flush=True)
 return dict(label=label,arm=arm,seed=seed,**fields)
arms=['baseline','off','counts','sample']
for arm in arms:run('cold',arm,True)
assert len(set(cold.values()))==1
for i in range(15):
 order=arms[i%4:]+arms[:i%4]
 if i%2:order=order[::-1]
 for arm in order:rows.append(run(str(i),arm,seed=198+i*7919))
 (out/'samples.json').write_text(json.dumps(rows,indent=2))
summary={}
for control,candidate in [('baseline','off'),('off','counts'),('counts','sample'),('baseline','sample')]:
 result={}
 for metric in ('total_ms','pls08_linking_ms','pls08_full_with_teardown_ms'):
  pairs=[(next(r[metric] for r in rows if r['label']==str(i) and r['arm']==control),next(r[metric] for r in rows if r['label']==str(i) and r['arm']==candidate)) for i in range(15)]
  differences=[b-a for a,b in pairs];rng=random.Random(106);boot=sorted(statistics.median(rng.choices(differences,k=15)) for _ in range(20000))
  result[metric]=dict(control_median=statistics.median(a for a,b in pairs),observer_median=statistics.median(b for a,b in pairs),paired_overhead_ms=statistics.median(differences),bootstrap95=[boot[500],boot[19499]])
 summary[f'{control}->{candidate}']=result
# Counts must be independent of seed/timing mode, including the cold audit.
counts=profiles['cold','counts']
for (label,arm),profile in profiles.items():
 if arm not in ('counts','sample'):continue
 for name,data in counts['tags'].items():
  metrics=('calls',) if name=='queue_wait' else ('calls','hits','misses')
  assert all(profile['tags'][name][m]==data[m] for m in metrics),(label,arm,name)
report={}
for name in counts['tags']:
 p=[profiles[str(i),'sample']['tags'][name] for i in range(15)]
 report[name]=dict(calls=counts['tags'][name]['calls'],hits=counts['tags'][name]['hits'],misses=counts['tags'][name]['misses'])
 if name=='queue_wait':
  report[name]['hits_median']=statistics.median(x['hits'] for x in p);report[name]['misses_median']=statistics.median(x['misses'] for x in p)
 for metric in ('inclusive_estimate_ms','exclusive_estimate_ms','exact_ms','sample_p50_ns','sample_p95_ns','samples'):
  v=[x[metric] for x in p];report[name][metric]=dict(median=statistics.median(v),minimum=min(v),maximum=max(v))
derived=[]
for i in range(15):
 profile=profiles[str(i),'sample'];incoming={k:0. for k in report};outgoing={k:0. for k in report};roots=0.;unknown=[]
 for edge in profile['edges']:
  if edge['exact_calls']:continue # rare work is timed exhaustively, outside regular sampling
  if not edge['samples']:unknown.append(edge);continue
  estimate=edge['regular_ns']/edge['samples']*edge['calls']/1e6
  incoming[edge['child']]+=estimate
  if edge['parent']=='ROOT':roots+=estimate
  else:outgoing[edge['parent']]+=estimate
 rare=sum(v['exact_ms'] for k,v in profile['tags'].items() if k in ('value_rehash','link_rehash','value_reserve','link_reserve','origin_reserve'))
 derived.append(dict(inclusive=incoming,exclusive={k:incoming[k]-outgoing[k] for k in incoming},root_regular_ms=roots,rare_exact_ms=rare,apply_file_ms=profile['tags']['apply_file']['exact_ms'],unmeasured_edges=unknown))
for name in report:
 for metric in ('inclusive','exclusive'):
  v=[p[metric][name] for p in derived];report[name]['edge_'+metric+'_ms']=dict(median=statistics.median(v),minimum=min(v),maximum=max(v))
assert binary=={k:sha(p) for k,p in bins.items()}
(out/'summary.json').write_text(json.dumps(dict(binary_hashes=binary,digests=gold,cold=cold,observer_controls=summary,breakdown=report,edge_profiles=derived),indent=2))
print(json.dumps(summary,indent=2))
