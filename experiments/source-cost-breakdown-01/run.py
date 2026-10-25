import csv
import hashlib
import json
import os
import pathlib
import statistics as stat
import subprocess
import sys

baseline,candidate,project,output=(pathlib.Path(p).resolve() for p in sys.argv[1:])
output.mkdir(parents=True,exist_ok=True)
variants=('baseline','off','counts','timed')
golden={'compiled.bin':'a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d',
        'runtime.bin':'a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6'}
rows=[];costs=[]
for block in range(9):
    order=variants[block%4:]+variants[:block%4]
    if block%2:order=tuple(reversed(order))
    for variant in order:
        env=os.environ.copy();env['CW_SOURCE_COST_MODE']='off' if variant=='baseline' else variant
        exe=baseline if variant=='baseline' else candidate
        p=subprocess.run([str(exe),'publish-timeline',str(project),'24027'],capture_output=True,text=True,env=env)
        (output/f'b{block}-{variant}.stdout.txt').write_text(p.stdout)
        (output/f'b{block}-{variant}.stderr.txt').write_text(p.stderr)
        if p.returncode:raise RuntimeError((block,variant,p.returncode,p.stderr))
        row=dict(field.split('=',1) for field in next(line for line in p.stdout.splitlines() if line.startswith('mode=')).split(','))
        row.update(block=block,variant=variant,warmup=int(block==0));rows.append(row)
        assert row['phase_only']=='1' and row['source_token_count']=='0' and row['header_data_members']=='0'
        probe=[]
        for line in p.stderr.splitlines():
            if not line.startswith('SOURCE_COST,'):continue
            _,name,calls,inclusive,exclusive=line.split(',')
            probe.append(dict(block=block,variant=variant,warmup=int(block==0),category=name,
                calls=int(calls),inclusive_ns=int(inclusive),exclusive_ns=int(exclusive)))
        assert bool(probe)==(variant in ('counts','timed'))
        if variant=='timed':
            root=next(r for r in probe if r['category']=='source')
            assert sum(r['exclusive_ns'] for r in probe)==root['inclusive_ns']
        costs.extend(probe)
        for name,expected in golden.items():
            actual=hashlib.sha256((project.parent/'.serverengine'/project.name/name).read_bytes()).hexdigest()
            assert actual==expected,(block,variant,name,actual)
        print(block,variant,row['total_ms'],row['source_ms'],flush=True)

for name,data in [('measurements.csv',rows),('costs.csv',costs)]:
    with (output/name).open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(data[0]));writer.writeheader();writer.writerows(data)
measured=[r for r in rows if not r['warmup']]
measured_costs=[r for r in costs if not r['warmup']]
counts={category:{r['calls'] for r in measured_costs if r['category']==category} for category in {r['category'] for r in measured_costs}}
assert all(len(v)==1 for v in counts.values()),counts
summary={'pairs_per_variant':8,'excluded_warmup_blocks':1,
    'variants':{v:{k:stat.median(float(r[k]) for r in measured if r['variant']==v) for k in ('total_ms','source_ms','header_ms','lexical_ms','persistence_ms','resident_ms')} for v in variants},
    'observer_deltas':{},'timed_costs':{},'golden_hashes':golden,
    'executables':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in (baseline,candidate)},
    'note':'Instrumented exclusive times describe instrumented execution; do not subtract clocks or apply shares to the production baseline.'}
bykey={(r['block'],r['variant']):r for r in measured}
for v,control in [('off','baseline'),('counts','off'),('timed','counts'),('timed','baseline')]:
    summary['observer_deltas'][f'{v}_minus_{control}']={}
    for key in ('total_ms','source_ms'):
        ds=[float(bykey[b,v][key])-float(bykey[b,control][key]) for b in range(1,9)]
        summary['observer_deltas'][f'{v}_minus_{control}'][key]={'median_ms':stat.median(ds),'range_ms':[min(ds),max(ds)],'pairs_ms':ds}
for category,values in counts.items():
    series=[r for r in measured_costs if r['category']==category and r['variant']=='timed']
    summary['timed_costs'][category]={'calls':next(iter(values)),
        'mean_inclusive_ms':stat.mean(r['inclusive_ns']/1e6 for r in series),
        'mean_exclusive_ms':stat.mean(r['exclusive_ns']/1e6 for r in series)}
(output/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
