"""Correctness/audit + fresh paired experimental lifecycle, no materialization."""
import argparse,csv,hashlib,json,os,random,statistics,subprocess,time
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=15);a=p.parse_args()
assert 5<=a.pairs<=40
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
normal=repo/'build/parallel-local-source-03/Release/ParallelLocalSource03.exe'
audit=normal.with_name('ParallelLocalSource03Audit.exe')
old=repo/'build/parallel-local-source-02/Release/ParallelLocalSource02.exe'
project=repo/'build/publish-subsecond-01/fixture/project.json';oracle=project.parent/'.serverengine'/project.name/'compiled.bin'
metadata={str(exe):hashlib.sha256(exe.read_bytes()).hexdigest() for exe in (normal,audit,old)}
(out/'executables.json').write_text(json.dumps(metadata,indent=2));rows=[];pairs=[]
def run(exe,mode,tag,verify=False,reverse=False):
 report=out/(tag+'.json');profile=out/(tag+'.profile.json')
 env=dict(os.environ,PLS03_PROFILE_PATH=str(profile),PLS03_MODE=str(mode))
 cmd=[str(exe),str(project),str(report)]+(['--oracle',str(oracle)] if verify else [])+(['--reverse'] if reverse else [])
 t=time.perf_counter();r=subprocess.run(cmd,capture_output=True,text=True,env=env);wall=(time.perf_counter()-t)*1000
 (out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr)
 assert r.returncode==0,(tag,r.stdout,r.stderr)
 d=json.loads(report.read_text());assert d['semantic_fingerprint']=='13649286433448276567',(tag,d['semantic_fingerprint'])
 assert d['counts']['linker_token_reads']==0 and d['counts']['writes']==705238 and d['counts']['bindings']==367144
 row=dict(tag=tag,mode=mode,exe=str(exe),wall_ms=wall,alias_ms=d['timings_ms']['aliases'],extraction_ms=d['timings_ms']['extraction'],semantic_ms=d['timings_ms']['semantic'],lifecycle_ms=d.get('lifecycle_ms'),fingerprint=d['semantic_fingerprint'])
 rows.append(row);(out/'runs.json').write_text(json.dumps(rows,indent=2));print(tag,row['alias_ms'],row['lifecycle_ms'],flush=True);return row
for mode in (0,1):
 for reverse in (False,True):run(audit,mode,f'audit-{mode}-{int(reverse)}',True,reverse)
for mode in (0,1):run(normal,mode,f'warmup-{mode}')
for category,n in [('observer',5),('candidate',a.pairs)]:
 for i in range(n):
  rr={}
  for arm in ([0,1] if i%2 else [1,0]):
   exe=old if category=='observer' and arm==0 else normal
   rr[arm]=run(exe,arm if category=='candidate' else 0,f'{category}-{i:02d}-{arm}')
  pair=dict(category=category,pair=i)
  for field in ('alias_ms','lifecycle_ms','wall_ms'):
   if rr[0][field] is not None and rr[1][field] is not None:
    pair['control_'+field]=rr[0][field];pair['test_'+field]=rr[1][field];pair['saved_'+field]=rr[0][field]-rr[1][field]
   else:pair['control_'+field]=pair['test_'+field]=pair['saved_'+field]=''
  pairs.append(pair)
  with (out/'pairs.csv').open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=list(pairs[0]));w.writeheader();w.writerows(pairs)
summary={}
for cat in ('observer','candidate'):
 rr=[r for r in pairs if r['category']==cat];summary[cat]={}
 for field in ('alias_ms','lifecycle_ms','wall_ms'):
  values=[r['saved_'+field] for r in rr if r['saved_'+field]!='']
  if not values:continue
  rng=random.Random(3317);boot=sorted(statistics.median(rng.choices(values,k=len(values))) for _ in range(10000))
  control=statistics.median(r['control_'+field] for r in rr);test=statistics.median(r['test_'+field] for r in rr)
  summary[cat][field]=dict(control=control,test=test,paired_saved=statistics.median(values),ci95=[boot[250],boot[9750]],faster=sum(x>0 for x in values),pairs=len(values))
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
