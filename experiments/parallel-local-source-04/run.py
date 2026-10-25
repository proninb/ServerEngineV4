"""File-local lifetime: correctness, allocation audit, and paired RSS/lifecycle."""
import argparse,csv,hashlib,json,os,random,statistics,subprocess,time
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=15);a=p.parse_args();assert 5<=a.pairs<=40
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
normal=repo/'build/parallel-local-source-04/Release/ParallelLocalSource04.exe';audit=normal.with_name('ParallelLocalSource04Audit.exe')
old=repo/'build/parallel-local-source-03/Release/ParallelLocalSource03.exe'
project=repo/'build/publish-subsecond-01/fixture/project.json';oracle=project.parent/'.serverengine'/project.name/'compiled.bin'
(out/'executables.json').write_text(json.dumps({str(e):hashlib.sha256(e.read_bytes()).hexdigest() for e in (normal,audit,old)},indent=2))
rows=[];pairs=[]
def run(exe,early,tag,verify=False,reverse=False):
 report=out/(tag+'.json');memory=out/(tag+'.memory.json');profile=out/(tag+'.profile.json')
 env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY=str(early),PLS03_PROFILE_PATH=str(profile),PLS04_MEMORY_PATH=str(memory))
 cmd=[str(exe),str(project),str(report)]+(['--oracle',str(oracle)] if verify else [])+(['--reverse'] if reverse else [])
 t=time.perf_counter();r=subprocess.run(cmd,capture_output=True,text=True,env=env);wall=(time.perf_counter()-t)*1000
 (out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr);assert r.returncode==0,(tag,r.stdout,r.stderr)
 d=json.loads(report.read_text());assert d['semantic_fingerprint']=='13649286433448276567';assert d['counts']['linker_token_reads']==0
 assert d['counts']['writes']==705238 and d['counts']['bindings']==367144 and d['counts']['warnings']==0
 row=dict(tag=tag,early=early,wall_ms=wall,lifecycle_ms=d['lifecycle_ms'],alias_ms=d['timings_ms']['aliases'],semantic_ms=d['timings_ms']['semantic'],ir_release_ms=d['ir_release_ms'])
 if exe!=old:
  m=json.loads(memory.read_text());assert m['released_files']==(len(d['tasks']) if early else 0)
  assert m['released_capacity']==(d['counts']['model_bytes'] if early else 0)
  row.update(file_destruction_ms=m['file_destruction_ms'],tail_destruction_ms=m['tail_destruction_ms'],destruction_ms=m['file_destruction_ms']+m['tail_destruction_ms'],
    rss_peak=max(x['sampled_peak_working_set'] for x in m['phases'].values()),linking_rss=m['phases']['linking']['sampled_peak_working_set'],
    os_peak=m['os_process_peak_after'],private_peak=max(x['sampled_peak_private_bytes'] for x in m['phases'].values()),
    allocations=sum(x['allocations'] for x in m['phases'].values()),requested_bytes=sum(x['requested_bytes'] for x in m['phases'].values()),heap_peak=m['heap_peak_live_requested'])
 rows.append(row);(out/'runs.json').write_text(json.dumps(rows,indent=2));print(tag,row['lifecycle_ms'],row.get('rss_peak'),flush=True);return row
for early in (0,1):
 for reverse in (False,True):run(audit,early,f'audit-{early}-{int(reverse)}',True,reverse)
for early in (0,1):run(normal,early,f'warmup-{early}')
for category,n in [('observer',5),('candidate',a.pairs)]:
 for i in range(n):
  rr={}
  for arm in ([0,1] if i%2 else [1,0]):rr[arm]=run(old if category=='observer' and arm==0 else normal,arm if category=='candidate' else 0,f'{category}-{i:02d}-{arm}')
  pair=dict(category=category,pair=i)
  for key in ('lifecycle_ms','alias_ms','semantic_ms','ir_release_ms','wall_ms','rss_peak','linking_rss','private_peak','os_peak','destruction_ms'):
   if key in rr[0] and key in rr[1]:pair['control_'+key]=rr[0][key];pair['test_'+key]=rr[1][key];pair['saved_'+key]=rr[0][key]-rr[1][key]
  pairs.append(pair)
  with (out/'pairs.csv').open('w',newline='') as f:
   keys=list(dict.fromkeys(k for r in pairs for k in r));w=csv.DictWriter(f,fieldnames=keys);w.writeheader();w.writerows(pairs)
summary={}
for category in ('observer','candidate'):
 rr=[r for r in pairs if r['category']==category];summary[category]={}
 for key in [k[6:] for k in rr[0] if k.startswith('saved_')]:
  v=[r['saved_'+key] for r in rr];rng=random.Random(4404);b=sorted(statistics.median(rng.choices(v,k=len(v))) for _ in range(10000))
  summary[category][key]=dict(control=statistics.median(r['control_'+key] for r in rr),test=statistics.median(r['test_'+key] for r in rr),paired_saved=statistics.median(v),ci95=[b[250],b[9750]],faster=sum(x>0 for x in v),pairs=len(v))
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
