"""Fresh paired full PUBLISH, audit/verification outside measured series."""
import argparse,csv,hashlib,json,os,random,statistics,subprocess
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--pairs',type=int,default=15)
p.add_argument('--block',type=int,default=65536)
p.add_argument('--exe',type=Path,default=repo/'build/bulk-materialization-01/engine/Release/BulkMaterialization01.exe')
a=p.parse_args();assert 5<=a.pairs<=40 and 1<=a.block<=1048576
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
exe=a.exe.resolve();plain=repo/'build/enum-v4/Release/ServerEngineV4PublishBenchmark.exe'
project=repo/'build/publish-subsecond-01/fixture/project.json'
gold=['a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d','a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6']
(out/'executables.json').write_text(json.dumps({str(b):hashlib.sha256(b.read_bytes()).hexdigest() for b in (exe,plain)},indent=2))
rows=[];pairs=[]
def run(binary,mode,tag,audit=False):
 r=subprocess.run([str(binary),'publish-timeline',str(project),'24027'],capture_output=True,text=True,
  env=dict(os.environ,CW_BULK_MODE=str(mode),CW_BULK_BLOCK=str(a.block),CW_BULK_AUDIT=str(int(audit)),
           CW_BULK_VERIFY=str(int(audit)),CW_SOURCE_READY_CURSOR='0',CW_RESOLVED_G_CAPTURE='0'))
 (out/(tag+'.stdout')).write_text(r.stdout);(out/(tag+'.stderr')).write_text(r.stderr)
 assert r.returncode==0,(tag,r.stderr[-3000:],r.stdout[-1000:])
 d=project.parent/'.serverengine'/project.name
 assert [hashlib.sha256((d/n).read_bytes()).hexdigest() for n in ('compiled.bin','runtime.bin')]==gold,tag
 line=next(l for l in r.stdout.splitlines() if l.startswith('mode=publish-timeline,') and 'phase_only=1' in l)
 row=dict(v.split('=',1) for v in line.split(','));row.update(tag=tag,bulk_mode=mode,binary=str(binary),audit=audit)
 if audit and binary==exe:
  aa=dict(v.split('=',1) for v in next(l for l in r.stdout.splitlines() if l.startswith('bulk_materialization01,')).split(',')[1:])
  row['bulk']=aa
  if mode:
   assert int(aa['init_calls'])==705237 and int(aa['link_calls'])==367144 and int(aa['replacements'])==0,aa
   assert int(aa['peak_pending'])<=a.block and int(aa['flushes'])>0,aa
 rows.append(row);(out/'runs.json').write_text(json.dumps(rows,indent=2))
 print(tag,row['total_ms'],row['source_ms'],row['peak_ws_bytes'],flush=True);return row
run(exe,0,'verify-control',True);run(exe,1,'verify-bulk',True)
for category,n in [('calibration',7),('candidate',a.pairs)]:
 for i in range(n):
  rr={}
  for arm in ([0,1] if i%2 else [1,0]):
   binary=plain if category=='calibration' and arm==0 else exe
   rr[arm]=run(binary,arm if category=='candidate' else 0,f'{category}{i:02d}-{arm}')
  row=dict(category=category,pair=i)
  for key in ('total_ms','source_ms','peak_ws_bytes'):
   row['control_'+key]=float(rr[0][key]);row['test_'+key]=float(rr[1][key]);row['saved_'+key]=float(rr[0][key])-float(rr[1][key])
  row['saved_percent']=100*row['saved_total_ms']/row['control_total_ms'];pairs.append(row)
  with (out/'pairs.csv').open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=list(pairs[0]));w.writeheader();w.writerows(pairs)
summary={}
for category in ('calibration','candidate'):
 rs=[r for r in pairs if r['category']==category];summary[category]={}
 for metric in ('saved_total_ms','saved_source_ms','saved_percent','saved_peak_ws_bytes'):
  ds=[r[metric] for r in rs];rng=random.Random(1701)
  boot=sorted(statistics.median(rng.choices(ds,k=len(ds))) for _ in range(10000))
  summary[category][metric]=dict(median=statistics.median(ds),ci95=[boot[250],boot[9750]],positive=sum(v>0 for v in ds),n=len(ds))
 for metric in ('control_total_ms','test_total_ms','control_source_ms','test_source_ms','control_peak_ws_bytes','test_peak_ws_bytes'):
  summary[category][metric]=statistics.median(r[metric] for r in rs)
candidate=summary['candidate'];candidate['accept_threshold_percent']=10
candidate['threshold_passed']=candidate['saved_percent']['median']>=10 and candidate['saved_total_ms']['ci95'][0]>0
(out/'summary.json').write_text(json.dumps(summary,indent=2));print(json.dumps(summary,indent=2))
