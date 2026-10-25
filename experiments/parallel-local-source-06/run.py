"""Same-executable unordered-map/dense A/B, retained PLS04/PLS03 contracts."""
import argparse,csv,hashlib,json,os,random,statistics,subprocess,time
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=15);p.add_argument('--calibration-only',action='store_true');a=p.parse_args();assert 5<=a.pairs<=40
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
normal=repo/'build/parallel-local-source-06/Release/ParallelLocalSource06.exe';audit=normal.with_name('ParallelLocalSource06Audit.exe')
old=repo/'build/parallel-local-source-05/Release/ParallelLocalSource05.exe'
project=repo/'build/publish-subsecond-01/fixture/project.json';oracle=project.parent/'.serverengine'/project.name/'compiled.bin'
(out/'executables.json').write_text(json.dumps({str(e):hashlib.sha256(e.read_bytes()).hexdigest() for e in (normal,audit,old)},indent=2))
rows=[];pairs=[]
def run(exe,dense,tag,verify=False,reverse=False):
 report=out/(tag+'.json');memory=out/(tag+'.memory.json');profile=out/(tag+'.profile.json');heads=out/(tag+'.heads.json')
 env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE=str(dense),PLS03_PROFILE_PATH=str(profile),PLS04_MEMORY_PATH=str(memory),PLS06_HEADS_PATH=str(heads))
 cmd=[str(exe),str(project),str(report)]+(['--oracle',str(oracle)] if verify else [])+(['--reverse'] if reverse else [])
 start=time.perf_counter();r=subprocess.run(cmd,capture_output=True,text=True,env=env);wall=(time.perf_counter()-start)*1000
 (out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr);assert r.returncode==0,(tag,r.stdout,r.stderr)
 d=json.loads(report.read_text());m=json.loads(memory.read_text());s=json.loads(profile.read_text())['stages']
 assert d['semantic_fingerprint']=='13649286433448276567' and d['counts']['linker_token_reads']==0
 assert d['counts']['writes']==705238 and d['counts']['bindings']==367144 and d['counts']['warnings']==0
 assert d['counts']['bound_bytes']==187667640 and d['counts']['final_array_bytes']==39195108
 assert m['released_files']==3373 and m['released_capacity']==182960089
 row=dict(tag=tag,dense=dense,exe=str(exe),wall_ms=wall,lifecycle_ms=d['lifecycle_ms'],alias_ms=d['timings_ms']['aliases'],semantic_ms=d['timings_ms']['semantic'],
  write_targets_ms=s['write_targets_and_index']['ms'],lww_ms=s['lww_reduction']['ms'],write_cleanup_ms=s['write_cleanup']['ms'],value_order_ms=s['value_order']['ms'],
  rss_peak=max(x['sampled_peak_working_set'] for x in m['phases'].values()),os_peak=m['os_process_peak_after'],private_peak=max(x['sampled_peak_private_bytes'] for x in m['phases'].values()),
  allocations=sum(x['allocations'] for x in m['phases'].values()),requested_bytes=sum(x['requested_bytes'] for x in m['phases'].values()),heap_peak=m['heap_peak_live_requested'])
 if exe!=old:
  h=json.loads(heads.read_text());assert h['populated']==705238 and h['initial_cells']==640178
  assert h['dense']==bool(dense)
 rows.append(row);(out/'runs.json').write_text(json.dumps(rows,indent=2));print(tag,row['lifecycle_ms'],row['alias_ms'],flush=True);return row
if not a.calibration_only:
 for exe,name in ((normal,'verify'),(audit,'audit')):
  for dense in (0,1):
   for reverse in (False,True):run(exe,dense,f'{name}-{dense}-{int(reverse)}',True,reverse)
 for dense in (0,1):run(normal,dense,f'warmup-{dense}')
categories=[('calibration',5)]+([] if a.calibration_only else [('candidate',a.pairs)])
for category,n in categories:
 for i in range(n):
  rr={}
  for arm in ([0,1] if i%2 else [1,0]):rr[arm]=run(old if category=='calibration' and arm==0 else normal,arm if category=='candidate' else 0,f'{category}-{i:02d}-{arm}')
  pair=dict(category=category,pair=i)
  for key in ('lifecycle_ms','alias_ms','write_targets_ms','lww_ms','write_cleanup_ms','value_order_ms','semantic_ms','wall_ms','rss_peak','os_peak','private_peak'):
   pair['control_'+key]=rr[0][key];pair['test_'+key]=rr[1][key];pair['saved_'+key]=rr[0][key]-rr[1][key]
  pairs.append(pair)
  with (out/'pairs.csv').open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=list(pairs[0]));w.writeheader();w.writerows(pairs)
summary={}
for category,_ in categories:
 rr=[r for r in pairs if r['category']==category];summary[category]={}
 for key in [k[6:] for k in rr[0] if k.startswith('saved_')]:
  v=[r['saved_'+key] for r in rr];rng=random.Random(6606);b=sorted(statistics.median(rng.choices(v,k=len(v))) for _ in range(10000))
  summary[category][key]=dict(control=statistics.median(r['control_'+key] for r in rr),test=statistics.median(r['test_'+key] for r in rr),paired_saved=statistics.median(v),ci95=[b[250],b[9750]],lower=sum(x>0 for x in v),pairs=len(v))
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
