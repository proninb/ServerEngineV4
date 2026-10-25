"""RG02 paired path ablations, then an independent full-PUBLISH candidate series."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess

repo=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('output',type=Path)
p.add_argument('--exe',type=Path,default=repo/'build/resolved-g-02/engine/Release/ResolvedGPath02.exe')
p.add_argument('--project',type=Path,default=repo/'build/publish-subsecond-01/fixture/project.json')
p.add_argument('--types',type=int,default=24027)
p.add_argument('--pairs',type=int,default=15)
p.add_argument('--publish-pairs',type=int,default=15)
a=p.parse_args()
assert 5<=a.pairs<=40 and 5<=a.publish_pairs<=40
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
project=a.project.resolve();reference=None
names={1:'oracle_prehashed',2:'oracle_reserve_vectors',4:'oracle_reserve_index',6:'oracle_reserve_both',
       8:'single_probe',16:'cached_rehash_hashes',24:'single_probe_and_cached_hashes',
       32:'oracle_skip_existing',64:'oracle_prebuilt_paths'}
real=(8,16,24)

def line_fields(line):
 return dict(part.split('=',1) for part in line.split(',')[1:])

def run(capture,mode,tag):
 global reference
 env=dict(os.environ,CW_RESOLVED_G_CAPTURE=str(capture),CW_PATH_INTERN_MODE=str(mode),
          CW_RESOLVED_G_REPEATS=str(a.pairs),CW_RESOLVED_G_CORRUPT='0')
 result=subprocess.run([str(a.exe.resolve()),'publish-timeline',str(project),str(a.types)],
                       capture_output=True,text=True,env=env)
 (out/(tag+'.stdout')).write_text(result.stdout)
 (out/(tag+'.stderr')).write_text(result.stderr)
 if result.returncode: raise RuntimeError(tag+': '+result.stderr[-2000:])
 directory=project.parent/'.serverengine'/project.name
 digest={name:hashlib.sha256((directory/name).read_bytes()).hexdigest() for name in ('compiled.bin','runtime.bin')}
 if reference is None:
  reference=digest
  if project==(repo/'build/publish-subsecond-01/fixture/project.json').resolve():
   assert digest=={'compiled.bin':'a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d',
                   'runtime.bin':'a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6'}
 assert digest==reference,(tag,digest)
 return result

def write_csv(name,rows):
 with (out/name).open('w',newline='') as f:
  writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)

def ci(values):
 rng=random.Random(24102402)
 samples=sorted(statistics.median(rng.choices(values,k=len(values))) for _ in range(10000))
 return [samples[249],samples[9749]]

def paired(rows,key,control_label,candidate_label,metric):
 differences=[];percent=[]
 baseline=[];candidate=[]
 for pair in range(key):
  by_arm={int(row['mode']):float(row[metric]) for row in rows if int(row['pair'])==pair}
  b,c=by_arm[control_label],by_arm[candidate_label]
  baseline.append(b);candidate.append(c);differences.append(b-c);percent.append(100*(b-c)/b)
 return dict(control_median_ms=statistics.median(baseline),candidate_median_ms=statistics.median(candidate),
             paired_gain_median_ms=statistics.median(differences),bootstrap_95_ms=ci(differences),
             paired_gain_percent_median=statistics.median(percent),faster_pairs=sum(d>0 for d in differences),
             pairs=key,differences_ms=differences)

# Warm the ordinary PUBLISH path before the capture process. Its timings are not used as replay controls.
run(0,0,'warmup')
captured=run(1,0,'capture')
rows=[line_fields(line) for line in captured.stdout.splitlines() if line.startswith('resolved_g02,')]
assert len(rows)==(a.pairs+2)*18 and all(row['parity']=='1' for row in rows)
write_csv('replays.csv',rows)
summary={'artifacts':reference,'replay_pairs_per_case':a.pairs,
         'path_counts':line_fields(next(line for line in captured.stdout.splitlines() if line.startswith('resolved_g02_paths,'))),
         'operation_counts':line_fields(next(line for line in captured.stdout.splitlines() if line.startswith('resolved_g01_summary,'))),
         'cases':{}}
for mode,name in names.items():
 subset=[row for row in rows if int(row['case'])==mode and int(row['pair'])>=0]
 result=paired(subset,a.pairs,0,mode,'g_ms')
 result.update(name=name,kind='candidate' if mode in real else 'diagnostic_oracle',
               capacity_bytes=int(next(row['capacity_bytes'] for row in subset if int(row['mode'])==mode)))
 result['passes_replay_10_percent']=mode in real and result['paired_gain_percent_median']>=10 and result['bootstrap_95_ms'][0]>0
 summary['cases'][mode]=result
 print(mode,name,result['paired_gain_median_ms'],result['paired_gain_percent_median'],result['bootstrap_95_ms'],flush=True)
(out/'replay-summary.json').write_text(json.dumps(summary,indent=2))

# Independent full-PUBLISH verification of the best implementable candidate,
# even if the replay gate fails. No oracle is permitted on this path.
best=max(real,key=lambda mode:summary['cases'][mode]['paired_gain_median_ms'])
summary['selected_candidate']=best
pub=[]
for pair in range(-2,a.publish_pairs):
 order=(0,best) if pair%2==0 else (best,0)
 for mode in order:
  result=run(0,mode,f'publish-{pair:03}-mode{mode}')
  row=dict(part.split('=',1) for part in next(line for line in result.stdout.splitlines() if line.startswith('mode=')).split(','))
  row['mode']=mode;row['pair']=pair;pub.append(row)
  write_csv('publish.csv',pub)
  print('publish',pair,mode,row['total_ms'],row['source_ms'],flush=True)
summary['publish']={metric:paired(pub,a.publish_pairs,0,best,metric) for metric in ('total_ms','source_ms')}
summary['production_adoption']=False
summary['limitations']=[
 'Oracle variants estimate avoided-work bounds; they are not implementable end-to-end optimizations.',
 'Reservation/allocation for reserve variants is inside g_ms; planning future counts is excluded and not available in production.',
 'Prebuilt-path copying and prehash preparation are outside g_ms and must not be treated as free production work.',
 'Ablation differences are not additive: index occupancy, cache and allocator states change.',
 'No per-call timer is used. Original canonical handles, records and index fields must match exactly.',
 'Additional transient cached hashes are validated separately; they are never persisted.']
(out/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary['publish'],indent=2),flush=True)
