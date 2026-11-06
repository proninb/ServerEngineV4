import argparse,csv,statistics
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('csv')
a=p.parse_args()
with Path(a.csv).open(newline='') as f:
 rows=list(csv.DictReader(f))
if not rows: raise SystemExit('No cycles recorded')
def percentile(key,p):
 v=sorted(float(r[key]) for r in rows)
 x=(len(v)-1)*p/100
 lo=int(x); hi=min(lo+1,len(v)-1)
 return v[lo]+(v[hi]-v[lo])*(x-lo)
miss=sum(int(r['deadline_miss']) for r in rows)
skipped=sum(int(r['skipped_periods']) for r in rows)
print('cycles',len(rows),'deadline_misses',miss,f'({miss/len(rows):.2%})','skipped_periods',skipped)
for key in ['wakeup_lateness_us','barrier_duration_us','cycle_duration_us','overrun_us']:
 print(key,'p50/p95/p99',*[round(percentile(key,p),3) for p in [50,95,99]])
