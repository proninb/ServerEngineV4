"""Nine unchanged THREAD-06 single-process controls. No rebuild or server launch."""
import argparse,csv,hashlib,json,statistics,subprocess,time
from pathlib import Path
EXPECTED={'alloc':9536484,'free':9536471,'unmatched':159,'live':13,'workers_created':8781,'workers_stopped':8781}
def sha(path):
 h=hashlib.sha256()
 with path.open('rb') as stream:
  for block in iter(lambda:stream.read(8*1024*1024),b''):h.update(block)
 return h.hexdigest()
def validate(rows,reference):
 if len(rows)!=len(reference)+2 or rows[0].get('stage')!='initial' or rows[-1].get('stage')!='complete':raise ValueError('incomplete output')
 if any(rows[-1].get(k)!=v for k,v in EXPECTED.items()):raise ValueError('counter mismatch')
 checkpoints=rows[1:-1]
 signature=lambda r:tuple(r.get(k) for k in ('boundary','stage_code','active_workers','live'))
 if [signature(r) for r in checkpoints]!=[signature(r) for r in reference]:raise ValueError('boundary signature mismatch')
 initial=rows[0]['private_bytes']
 for row in checkpoints:
  if row.get('worker_stack_commit')!=0:raise ValueError('physical worker stacks in single')
  if row['private_bytes']-initial!=row['drift_bytes']:raise ValueError('drift mismatch')
 return checkpoints
def phase(selected):return dict(cycle70_above80=selected[70]>selected[80],cycle90_above80=selected[90]>selected[80],
 selected_peak_cycle=max(selected,key=selected.get),selected_trough_cycle=min(selected,key=selected.get))
def summarize(runs):
 patterns={tuple(sorted(r['phase'].items())) for r in runs}
 return dict(observed_phase_patterns=len(patterns),selected_phase_repeatable=len(patterns)==1,
  phase_definition='Pairwise 70/80 and 90/80 signs plus selected peak/trough cycle; no invented absolute high-state threshold.',
  selected_cycles={str(c):dict(min_bytes=min(r['selected_drift_bytes'][c] for r in runs),
   median_bytes=statistics.median(r['selected_drift_bytes'][c] for r in runs),
   max_bytes=max(r['selected_drift_bytes'][c] for r in runs)) for c in (70,80,90)},
  interpretation='Observed phase varies with identical executable/input; investigate startup heap/layout variability.' if len(patterns)>1 else 'Observed selected-cycle phase is repeatable in this nine-process sample; compare DOMAIN-04 and THREAD-06 implementation/layout before concurrency experiments.')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--runs',type=int,default=9);a=p.parse_args()
 if a.runs!=9:raise ValueError('This protocol requires nine processes')
 base=Path('build/project-unload-thread-06').resolve();exe=base/'replay.exe';input_file=base/'strict-replay.bin';stages=base/'stages.bin';boundary_file=base/'boundaries.json'
 a.out.mkdir(parents=True,exist_ok=False)
 reference=[json.loads(x) for x in (base/'single.jsonl').read_text().splitlines()][1:-1]
 boundaries=json.loads(boundary_file.read_text());stage_bytes=stages.read_bytes()
 if len(reference)!=274 or len(stage_bytes)!=274 or any(r['boundary']!=i or r['stage_code']!=stage_bytes[i] for i,r in enumerate(reference)):raise ValueError('reference gate')
 paths=[exe,input_file,stages,boundary_file];hashes={str(x):sha(x) for x in paths}
 result=dict(status='RUNNING',protocol='CONTROL-STABILITY-07',runs=[],hashes=hashes,executable_rebuilt=False,mode='single',release='BLOCKED',
  command=[str(exe),str(input_file),str(stages),'single'],creationflags=0x08000000)
 def save(): (a.out/'comparison.json').write_text(json.dumps(result,indent=2))
 save()
 try:
  for index in range(1,10):
   before={str(x):sha(x) for x in paths}
   if before!=hashes:raise ValueError('input hash changed before run')
   started=time.monotonic();stdout=a.out/f'run-{index:02}.jsonl';stderr=a.out/f'run-{index:02}.stderr.txt'
   with stdout.open('w') as out,stderr.open('w') as err:
    process=subprocess.Popen([str(exe),str(input_file),str(stages),'single'],stdout=out,stderr=err,creationflags=0x08000000)
    try:code=process.wait(timeout=180)
    except subprocess.TimeoutExpired:
     process.terminate();process.wait(timeout=15);raise
   elapsed=time.monotonic()-started
   if code:raise ValueError(f'run {index} exit {code}')
   after={str(x):sha(x) for x in paths}
   if after!=hashes:raise ValueError('input hash changed after run')
   cps=validate([json.loads(x) for x in stdout.read_text().splitlines()],reference)
   selected={boundaries[x['boundary']]['cycle']:x['drift_bytes'] for x in cps if x['stage_code']==2 and boundaries[x['boundary']]['cycle'] in (70,80,90)}
   if len(selected)!=3:raise ValueError('missing selected cycles')
   with (a.out/f'run-{index:02}.csv').open('w',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=['boundary','cycle','stage','private_bytes','drift_bytes','active_workers','handles','live']);writer.writeheader()
    for x in cps:writer.writerow(dict(boundary=x['boundary'],cycle=boundaries[x['boundary']]['cycle'],stage=boundaries[x['boundary']]['stage'],**{k:x[k] for k in ('private_bytes','drift_bytes','active_workers','handles','live')}))
   result['runs'].append(dict(run=index,pid=process.pid,exit_code=code,elapsed_seconds=elapsed,boundaries_verified=len(cps),
    before_hashes=before,after_hashes=after,stdout_sha256=sha(stdout),selected_drift_bytes=selected,phase=phase(selected),
    max_quiescent_drift_bytes=max(x['drift_bytes'] for x in cps if x['stage_code']==2)))
   print(index,'PID',process.pid,'selected MiB',{k:round(v/2**20,6) for k,v in selected.items()},flush=True);save()
  result.update(status='COMPLETE / NO ADOPTION',summary=summarize(result['runs']),production_changed=False)
 except Exception as error:
  result.update(status='FAIL',error=repr(error));raise
 finally:save()
if __name__=='__main__':main()
