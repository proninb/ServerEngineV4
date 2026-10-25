import csv,json,os,subprocess,statistics,random
from pathlib import Path
import argparse
p=argparse.ArgumentParser()
p.add_argument('output',type=Path)
p.add_argument('--pairs',type=int,default=10)
a=p.parse_args()
assert 5<=a.pairs<=40
root=Path(__file__).resolve().parents[2]
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
paths={0:root/'build/enum-v4/Release/ServerEngineV4PublishBenchmark.exe',1:root/'build/resolved-g-02/engine/Release/ResolvedGPath02.exe'}
rows=[]
for pair in range(-1,a.pairs):
 for arm in ((0,1) if pair%2==0 else (1,0)):
  result=subprocess.run([str(paths[arm]),'publish-timeline',str(root/'build/publish-subsecond-01/fixture/project.json'),'24027'],capture_output=True,text=True,env=dict(os.environ,CW_RESOLVED_G_CAPTURE='0',CW_PATH_INTERN_MODE='0'))
  assert result.returncode==0,result.stderr
  (out/f'{pair}-{arm}.stdout').write_text(result.stdout)
  r=dict(f.split('=',1) for f in next(l for l in result.stdout.splitlines() if l.startswith('mode=')).split(','));r.update(pair=pair,arm=arm);rows.append(r)
  with (out/'rows.csv').open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
  print(pair,arm,r['total_ms'],r['source_ms'],flush=True)
summary={}
for metric in ('total_ms','source_ms'):
 diffs=[]
 for pair in range(a.pairs):
  d={r['arm']:float(r[metric]) for r in rows if r['pair']==pair};diffs.append(d[1]-d[0])
 rng=random.Random(24102403); boot=sorted(statistics.median(rng.choices(diffs,k=len(diffs))) for _ in range(10000))
 summary[metric]={'pure_median':statistics.median(float(r[metric]) for r in rows if r['pair']>=0 and r['arm']==0),'experimental_control_median':statistics.median(float(r[metric]) for r in rows if r['pair']>=0 and r['arm']==1),'difference_paired_median':statistics.median(diffs),'bootstrap_95_ms':[boot[249],boot[9749]]}
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2),flush=True)
