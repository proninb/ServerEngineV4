"""Run full-server V1/V2 Task benchmarks with optional PERF02 instrumentation."""
import argparse, csv, os, pathlib, subprocess, sys

p=argparse.ArgumentParser()
p.add_argument('--server',required=True)
p.add_argument('--config',required=True)
p.add_argument('--project',required=True)
p.add_argument('--fixture-dir',required=True)
p.add_argument('--output-dir',default='build/task-runtime-v2-performance-01')
p.add_argument('--duration',type=float,default=8)
p.add_argument('--warmup',type=float,default=2)
p.add_argument('--counts',nargs='+',type=int,default=[1,2,4,8])
p.add_argument('--modes',nargs='+',choices=['v1','v2'],default=['v1','v2'])
a=p.parse_args()
root=pathlib.Path.cwd()
runner=root/'task-performance-01'/'task_performance_01.py'
if not runner.is_file(): p.error('Missing task-performance-01/task_performance_01.py; extract TASK-PERFORMANCE-01 first')
if not (root/'task-server-e2e-01'/'task_server_e2e.py').is_file(): p.error('Missing task-server-e2e-01/task_server_e2e.py')
out=(root/a.output_dir).resolve();out.mkdir(parents=True,exist_ok=True)
allrows=[]
for mode in a.modes:
    for n in a.counts:
        if n<1: p.error('Task count must be >=1')
        perf=out/f'{mode}-{n}-stages.csv'; throughput=out/f'{mode}-{n}-throughput.csv'
        perf.unlink(missing_ok=True)
        env=os.environ.copy()
        env['TASK_RUNTIME_V2']='1' if mode=='v2' else '0'
        env['TASK_PERF02_CSV']=str(perf)
        cmd=[sys.executable,str(runner),'--server',str(pathlib.Path(a.server).resolve()),'--config',str(pathlib.Path(a.config).resolve()),'--project',str(pathlib.Path(a.project).resolve()),'--fixture-dir',str(pathlib.Path(a.fixture_dir).resolve()),'--duration',str(a.duration),'--warmup',str(a.warmup),'--periods','2','--counts',str(n),'--output',str(throughput)]
        print('RUN',mode,n,flush=True)
        subprocess.run(cmd,cwd=root,env=env,check=True)
        with throughput.open(newline='',encoding='utf-8') as f: row=next(csv.DictReader(f))
        samples={}
        if perf.is_file():
            with perf.open(newline='',encoding='utf-8') as f:
                for stage,cycle,microseconds in csv.reader(f):
                    if stage not in ('cycle_interval','server_tick','manager_tick','ipc_roundtrip','barrier_wait'):continue
                    samples.setdefault(stage,[]).append(float(microseconds)/1000)
        def percentile(values,q):
            if not values:return ''
            v=sorted(values)
            return round(v[int((len(v)-1)*q)],4)
        result={'mode':mode,'tasks':n,'effective_cycle_ms':row['effective_cycle_ms'],'cycles_per_s':row['cycles_per_s']}
        for stage in ('cycle_interval','server_tick','manager_tick','ipc_roundtrip','barrier_wait'):
            values=samples.get(stage,[])
            result[stage+'_n']=len(values)
            for q,label in ((.5,'p50'),(.95,'p95'),(.99,'p99')):result[stage+'_'+label+'_ms']=percentile(values,q)
        allrows.append(result)
        print('RESULT',result,flush=True)
if not allrows:raise RuntimeError('No benchmark results')
summary=out/'summary.csv'
with summary.open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,fieldnames=list(allrows[0]));w.writeheader();w.writerows(allrows)
print('SUMMARY',summary)
print('NOTE: Cycle throughput is observed from Task callbacks. No proof of actual V2 path without separate mode verification.')
