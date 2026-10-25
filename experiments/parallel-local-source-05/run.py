"""Read-only ownership audit; no optimization arm or production benchmark."""
import argparse,hashlib,json,os,random,statistics,subprocess,time
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=5);a=p.parse_args();assert 5<=a.pairs<=20
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
normal=repo/'build/parallel-local-source-05/Release/ParallelLocalSource05.exe';audit=normal.with_name('ParallelLocalSource05Audit.exe')
old=repo/'build/parallel-local-source-04/Release/ParallelLocalSource04.exe'
project=repo/'build/publish-subsecond-01/fixture/project.json';oracle=project.parent/'.serverengine'/project.name/'compiled.bin'
(out/'executables.json').write_text(json.dumps({str(e):hashlib.sha256(e.read_bytes()).hexdigest() for e in (normal,audit,old)},indent=2))
rows=[];pairs=[]
def run(exe,tag,verify=False,reverse=False):
 report=out/(tag+'.json');memory=out/(tag+'.memory.json');profile=out/(tag+'.profile.json');ownership=out/(tag+'.ownership.json')
 env=dict(os.environ,PLS03_MODE='1',PLS04_EARLY='1',PLS03_PROFILE_PATH=str(profile),PLS04_MEMORY_PATH=str(memory),PLS05_AUDIT_PATH=str(ownership))
 cmd=[str(exe),str(project),str(report)]+(['--oracle',str(oracle)] if verify else [])+(['--reverse'] if reverse else [])
 t=time.perf_counter();r=subprocess.run(cmd,capture_output=True,text=True,env=env);wall=(time.perf_counter()-t)*1000
 (out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr);assert r.returncode==0,(tag,r.stdout,r.stderr)
 d=json.loads(report.read_text());m=json.loads(memory.read_text());assert d['semantic_fingerprint']=='13649286433448276567'
 assert d['counts']['linker_token_reads']==0 and d['counts']['writes']==705238 and d['counts']['bindings']==367144 and d['counts']['warnings']==0
 assert m['released_files']==3373 and m['released_capacity']==182960089
 if exe==audit:
  o=json.loads(ownership.read_text());assert sum(s['allocations'] for s in o['tags'].values())==sum(s['allocations'] for s in m['phases'].values())
  assert sum(s['requested_bytes'] for s in o['tags'].values())==sum(s['requested_bytes'] for s in m['phases'].values())
  assert sum(o['cell_role_masks'])==o['output_roots']+o['unrooted_cells']
 row=dict(tag=tag,exe=str(exe),wall_ms=wall,lifecycle_ms=d['lifecycle_ms'],alias_ms=d['timings_ms']['aliases'],semantic_ms=d['timings_ms']['semantic'],os_peak=m['os_process_peak_after'])
 rows.append(row);(out/'runs.json').write_text(json.dumps(rows,indent=2));print(tag,row['lifecycle_ms'],flush=True);return row
for exe,name in ((normal,'control'),(audit,'audit')):
 for reverse in (False,True):run(exe,f'{name}-oracle-{int(reverse)}',True,reverse)
for category in ('calibration','observer'):
 for i in range(a.pairs):
  rr={}
  for arm in ([0,1] if i%2 else [1,0]):
   exe=(old if arm==0 else normal) if category=='calibration' else (normal if arm==0 else audit)
   rr[arm]=run(exe,f'{category}-{i:02d}-{arm}')
  pairs.append(dict(category=category,pair=i,control=rr[0],test=rr[1]))
  (out/'pairs.json').write_text(json.dumps(pairs,indent=2))
summary={}
for category in ('calibration','observer'):
 rr=[r for r in pairs if r['category']==category];summary[category]={}
 for key in ('lifecycle_ms','alias_ms','semantic_ms','wall_ms','os_peak'):
  delta=[r['test'][key]-r['control'][key] for r in rr];rng=random.Random(5505);b=sorted(statistics.median(rng.choices(delta,k=len(delta))) for _ in range(10000))
  summary[category][key]=dict(control=statistics.median(r['control'][key] for r in rr),test=statistics.median(r['test'][key] for r in rr),paired_added=statistics.median(delta),ci95=[b[250],b[9750]],pairs=len(delta))
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
