"""Full PUBLISH paired cursor candidate; no trace or diagnostic oracle enabled."""
import argparse,hashlib,json,os,random,statistics,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=15)
p.add_argument('--exe',type=Path,default=repo/'build/source-resolution-01/engine/Release/SourceResolution01.exe');a=p.parse_args()
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
exe=a.exe.resolve()
plain=repo/'build/enum-v4/Release/ServerEngineV4PublishBenchmark.exe'
project=repo/'build/publish-subsecond-01/fixture/project.json'
gold=['a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d','a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6']
rows=[];pairs=[]
(out/'executables.json').write_text(json.dumps({str(b):hashlib.sha256(b.read_bytes()).hexdigest() for b in (exe,plain)},indent=2))
def run(binary,enabled,tag):
 r=subprocess.run([str(binary),'publish-timeline',str(project),'24027'],capture_output=True,text=True,
  env=dict(os.environ,CW_SOURCE_RESOLUTION_MODE='0',CW_SOURCE_READY_CURSOR=str(enabled)))
 (out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr)
 assert r.returncode==0,(tag,r.stderr[-3000:])
 d=project.parent/'.serverengine'/project.name
 assert [hashlib.sha256((d/n).read_bytes()).hexdigest() for n in ('compiled.bin','runtime.bin')]==gold,tag
 line=next(l for l in r.stdout.splitlines() if l.startswith('mode=publish-timeline,') and 'phase_only=1' in l)
 row=dict(v.split('=',1) for v in line.split(','));row.update(tag=tag,enabled=enabled,binary=str(binary));rows.append(row)
 (out/'runs.json').write_text(json.dumps(rows,indent=2));print(tag,row['total_ms'],row['source_ms'],flush=True);return row
run(exe,0,'warmup0');run(exe,1,'warmup1')
for category,count in [('calibration',7),('candidate',a.pairs)]:
 for i in range(count):
  rr={}
  for arm in ([0,1] if i%2 else [1,0]):
   binary=plain if category=='calibration' and arm==0 else exe
   enabled=arm if category=='candidate' else 0
   rr[arm]=run(binary,enabled,f'{category}{i:02d}_{arm}')
  pairs.append(dict(category=category,pair=i,control_total=float(rr[0]['total_ms']),test_total=float(rr[1]['total_ms']),
   saved_total=float(rr[0]['total_ms'])-float(rr[1]['total_ms']),saved_source=float(rr[0]['source_ms'])-float(rr[1]['source_ms'])))
  (out/'pairs.json').write_text(json.dumps(pairs,indent=2))
summary={}
for cat in ('calibration','candidate'):
 rs=[r for r in pairs if r['category']==cat];summary[cat]={}
 for metric in ('saved_total','saved_source'):
  ds=[r[metric] for r in rs];rng=random.Random(111);boot=sorted(statistics.median(rng.choices(ds,k=len(ds))) for _ in range(10000))
  summary[cat][metric]=dict(median=statistics.median(ds),ci95=[boot[250],boot[9750]],faster=sum(d>0 for d in ds),n=len(ds))
 summary[cat]['control_total']=statistics.median(r['control_total'] for r in rs)
 summary[cat]['test_total']=statistics.median(r['test_total'] for r in rs)
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
