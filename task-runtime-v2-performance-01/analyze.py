import argparse,csv,statistics
from collections import defaultdict
p=argparse.ArgumentParser();p.add_argument('csv');a=p.parse_args()
groups=defaultdict(list)
with open(a.csv, newline='') as f:
    for row in csv.reader(f):
        if len(row)==3 and row[0] in ('ipc_roundtrip','barrier_wait','manager_tick','server_tick','cycle_interval'):
            groups[row[0]].append(float(row[2])/1000)
def pct(v,q): return sorted(v)[min(len(v)-1,int((len(v)-1)*q))]
print('Stage              Count       p50 ms     p95 ms     p99 ms      max ms')
for stage in ('cycle_interval','server_tick','manager_tick','ipc_roundtrip','barrier_wait'):
    v=groups[stage]
    if v: print(f'{stage:18} {len(v):7d} {pct(v,.5):10.3f} {pct(v,.95):10.3f} {pct(v,.99):10.3f} {max(v):11.3f}')
