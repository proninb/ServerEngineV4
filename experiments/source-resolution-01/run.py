"""Paired diagnostic ablations. stage_ms excludes trace IO and provenance restoration."""
import argparse,csv,hashlib,json,os,random,statistics,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=15)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
exe=repo/'build/source-resolution-01/engine/Release/SourceResolution01.exe'
project=repo/'build/publish-subsecond-01/fixture/project.json';tape=out/'source.trace'
gold=['a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d','a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6']
rows=[]
def run(mode,verify,tag):
 r=subprocess.run([str(exe),'publish-timeline',str(project),'24027'],capture_output=True,text=True,
  env=dict(os.environ,CW_SOURCE_RESOLUTION_MODE=str(mode),CW_SOURCE_RESOLUTION_TAPE=str(tape),CW_SOURCE_RESOLUTION_VERIFY=str(verify),CW_SOURCE_READY_CURSOR='0'))
 (out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr)
 assert r.returncode==0,(tag,r.stderr[-3000:])
 d=project.parent/'.serverengine'/project.name
 assert [hashlib.sha256((d/n).read_bytes()).hexdigest() for n in ('compiled.bin','runtime.bin')]==gold,tag
 line=next(l for l in r.stdout.splitlines() if l.startswith('source_resolution01,'))
 row=dict(v.split('=',1) for v in line.split(',')[1:]);row['tag']=tag;rows.append(row)
 with (out/'runs.json').open('w') as f:json.dump(rows,f,indent=2)
 print(tag,row['stage_ms'],flush=True);return float(row['stage_ms'])
run(0,1,'warmup');run(1,1,'capture')
for m in range(2,7):run(m,1,f'verify{m}')
pairs=[]
for i in range(a.pairs):
 modes=[3,4,5,6];random.Random(i+1701).shuffle(modes)
 for m in modes:
  values={}
  for mode in ([2,m] if (i+m)%2 else [m,2]):values[mode]=run(mode,0,f'p{i:02d}_m{m}_arm{mode}')
  pairs.append(dict(pair=i,mode=m,control=values[2],oracle=values[m],saved=values[2]-values[m]))
 with (out/'pairs.csv').open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=list(pairs[0]));w.writeheader();w.writerows(pairs)
summary={}
for m in range(3,7):
 rr=[r for r in pairs if r['mode']==m];diff=[r['saved'] for r in rr]
 rng=random.Random(1337);boot=sorted(statistics.median(rng.choices(diff,k=len(diff))) for _ in range(10000))
 summary[m]=dict(control_ms=statistics.median(r['control'] for r in rr),oracle_ms=statistics.median(r['oracle'] for r in rr),paired_saved_ms=statistics.median(diff),ci95=[boot[250],boot[9750]],faster=sum(d>0 for d in diff),pairs=len(rr))
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
