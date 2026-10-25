"""PLS07-A ownership correctness, allocations, lifecycle and consumer residency."""
import argparse,csv,hashlib,json,os,random,statistics,subprocess,time
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=15);a=p.parse_args();assert 5<=a.pairs<=40
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
normal=repo/'build/parallel-local-source-07/Release/ParallelLocalSource07.exe';audit=normal.with_name('ParallelLocalSource07Audit.exe')
old=repo/'build/parallel-local-source-06/Release/ParallelLocalSource06.exe'
project=repo/'build/publish-subsecond-01/fixture/project.json';oracle=project.parent/'.serverengine'/project.name/'compiled.bin'
(out/'executables.json').write_text(json.dumps({str(e):hashlib.sha256(e.read_bytes()).hexdigest() for e in (normal,audit,old)},indent=2))
rows=[];pairs=[]
def run(exe,own,tag,verify=False,reverse=False):
 report=out/(tag+'.json');memory=out/(tag+'.memory.json');profile=out/(tag+'.profile.json');ownership=out/(tag+'.owner.json')
 env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS06_DENSE='1',PLS07_OWN=str(own),PLS07_VERIFY='1' if verify else '0',PLS03_PROFILE_PATH=str(profile),PLS04_MEMORY_PATH=str(memory),PLS07_OWNER_PATH=str(ownership))
 cmd=[str(exe),str(project),str(report)]+(['--oracle',str(oracle)] if verify else [])+(['--reverse'] if reverse else [])
 t=time.perf_counter();r=subprocess.run(cmd,capture_output=True,text=True,env=env);wall=(time.perf_counter()-t)*1000
 (out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr);assert r.returncode==0,(tag,r.stdout,r.stderr)
 d=json.loads(report.read_text());m=json.loads(memory.read_text());assert d['semantic_fingerprint']=='13649286433448276567'
 assert d['counts']['writes']==705238 and d['counts']['bindings']==367144 and d['counts']['linker_token_reads']==0 and d['counts']['warnings']==0
 assert d['counts']['bound_bytes']==187667640 and d['counts']['final_array_bytes']==39195108
 assert m['released_files']==3373 and m['released_capacity']==182960089
 row=dict(tag=tag,own=own,exe=str(exe),wall_ms=wall,lifecycle_ms=d['lifecycle_ms'],alias_ms=d['timings_ms']['aliases'],semantic_ms=d['timings_ms']['semantic'],consumer_ms=d['consumer_ms'],ir_release_ms=d['ir_release_ms'],
  os_peak=m['os_process_peak_after'],rss_peak=max(s['sampled_peak_working_set'] for s in m['phases'].values()),consumer_rss=m['phases']['consumer']['sampled_peak_working_set'],private_peak=max(s['sampled_peak_private_bytes'] for s in m['phases'].values()),
  allocations=sum(s['allocations'] for s in m['phases'].values()),requested_bytes=sum(s['requested_bytes'] for s in m['phases'].values()),heap_peak=m['heap_peak_live_requested'],live_end=m['heap_live_requested_end'])
 if exe!=old:
  o=json.loads(ownership.read_text());assert o['owned']==bool(own) and o['verified']==verify
  if own:assert o['buffers_moved'] and o['bound_empty'] and o['endpoint_released_bytes']==50393696
  row.update(retained_bytes=o['retained_bytes'],endpoint_release_ms=o['endpoint_release_ms'],transfer_ms=o['transfer_ms'],history_hash=o['history_hash'])
 rows.append(row);(out/'runs.json').write_text(json.dumps(rows,indent=2));print(tag,row['lifecycle_ms'],row['consumer_rss'],flush=True);return row
histories=[]
for exe,name in ((normal,'verify'),(audit,'audit')):
 for own in (0,1):
  for reverse in (False,True):histories.append(run(exe,own,f'{name}-{own}-{int(reverse)}',True,reverse)['history_hash'])
assert len(set(histories))==1
for own in (0,1):run(normal,own,f'warmup-{own}')
for category,n in [('calibration',5),('candidate',a.pairs)]:
 for i in range(n):
  rr={}
  for arm in ([0,1] if i%2 else [1,0]):rr[arm]=run(old if category=='calibration' and arm==0 else normal,arm if category=='candidate' else 0,f'{category}-{i:02d}-{arm}')
  pair=dict(category=category,pair=i)
  for key in ('lifecycle_ms','alias_ms','consumer_ms','ir_release_ms','semantic_ms','wall_ms','os_peak','rss_peak','consumer_rss','private_peak'):
   pair['control_'+key]=rr[0][key];pair['test_'+key]=rr[1][key];pair['saved_'+key]=rr[0][key]-rr[1][key]
  pairs.append(pair)
  with (out/'pairs.csv').open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=list(pairs[0]));w.writeheader();w.writerows(pairs)
summary={}
for category in ('calibration','candidate'):
 rr=[r for r in pairs if r['category']==category];summary[category]={}
 for key in [k[6:] for k in rr[0] if k.startswith('saved_')]:
  v=[r['saved_'+key] for r in rr];rng=random.Random(7707);b=sorted(statistics.median(rng.choices(v,k=len(v))) for _ in range(10000))
  summary[category][key]=dict(control=statistics.median(r['control_'+key] for r in rr),test=statistics.median(r['test_'+key] for r in rr),paired_saved=statistics.median(v),ci95=[b[250],b[9750]],lower=sum(x>0 for x in v),pairs=len(v))
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
