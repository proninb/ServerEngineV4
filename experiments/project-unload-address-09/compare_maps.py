import csv,json
from collections import defaultdict
from pathlib import Path
def by_base(rows):
 result=defaultdict(lambda:dict(commit=0,reserve=0))
 for row in rows:
  if row['size_bytes']<=0:raise ValueError('invalid region size')
  if row['type']!=0x20000 or row['state'] not in (0x1000,0x2000):continue
  result[row['allocation_base']]['commit' if row['state']==0x1000 else 'reserve']+=row['size_bytes']
 return dict(result)
def deltas(before,after):
 return sorted([dict(allocation_base=hex(base),before=before.get(base,dict(commit=0,reserve=0)),after=after.get(base,dict(commit=0,reserve=0)),
  commit_delta=after.get(base,{}).get('commit',0)-before.get(base,{}).get('commit',0),reserve_delta=after.get(base,{}).get('reserve',0)-before.get(base,{}).get('reserve',0))
  for base in before.keys()|after.keys() if before.get(base)!=after.get(base)],key=lambda x:-abs(x['commit_delta']))
def identity_gate(summary,expected):
 if summary['identity']!=expected:raise ValueError('identity mismatch')
def main():
 root=Path('build/project-unload-address-09/run-01');status=json.loads((root/'status.json').read_text())
 if status['status']!='CAPTURE COMPLETE' or len(status['runs'])!=6:raise ValueError('six complete processes required')
 results=[];bylabel=defaultdict(list)
 for run in status['runs']:
  if run['exit_code'] or run['signatures_verified']!=274 or run['after_hashes']!=status['hashes']:raise ValueError('run validation')
  directory=root/('process-%02d'%run['run']);maps={};points=[]
  for summary in run['snapshots']:
   identity_gate(summary,run['identity']);label=summary['label']
   with (directory/(label+'-regions.csv')).open(newline='') as stream:rows=[{k:int(v) for k,v in row.items()} for row in csv.DictReader(stream)]
   maps[label]=by_base(rows)
   total={key:sum(data[key] for data in maps[label].values()) for key in ('commit','reserve')}
   point=dict(label=label,private_usage_before=summary['before']['private_bytes'],private_usage_after=summary['after']['private_bytes'],
    private_stable=summary['private_stable_during_map'],handles=summary['before']['handles'],mem_private_commit=total['commit'],mem_private_reserve=total['reserve'])
   points.append(point);bylabel[label].append(point)
  changes=deltas(maps['b02'],maps['b03']);commit_delta=sum(x['commit_delta'] for x in changes);reserve_delta=sum(x['reserve_delta'] for x in changes)
  p={x['label']:x for x in points};usage_delta=p['b03']['private_usage_before']-p['b02']['private_usage_before']
  results.append(dict(run=run['run'],identity=run['identity'],snapshots=points,b02_to_b03=dict(private_usage_delta=usage_delta,
   mem_private_commit_delta=commit_delta,mem_private_reserve_delta=reserve_delta,private_minus_region_delta=usage_delta-commit_delta,allocation_base_changes=changes)))
 spread={label:{key:max(p[key] for p in points)-min(p[key] for p in points) for key in ('private_usage_before','mem_private_commit','mem_private_reserve')} for label,points in bylabel.items()}
 historical=json.loads(Path('build/project-unload-control-startup-08/first-divergence.json').read_text())['first_private_spread_at_threshold']['spread_bytes']
 reproduced=spread['b03']['private_usage_before']>=2**20
 comparison=dict(status='INCONCLUSIVE / NO ADOPTION' if not reproduced else 'REGION DELTAS CAPTURED / OWNER OPEN',release='BLOCKED',
  all_six_process_identities_verified=True,all_274_signatures_per_run_verified=True,diagnostic_only=True,input_hashes=status['hashes'],runs=results,
  interprocess_spread_bytes=spread,previous_b03_private_spread_bytes=historical,b03_spread_reproduced_at_1MiB=reproduced,
  all_maps_private_stable=all(p['private_stable'] for points in bylabel.values() for p in points),
  limitations=['AllocationBase is a virtual-region grouping, not proof of NT heap or C++ owner.',
   'Only within-process allocation bases are paired; no numeric base matching between processes.',
   'Diagnostic source copy, binary layout, stdin handshake and observation timing differ from CONTROL-07.',
   'Reduced spread cannot be attributed specifically to pauses rather than recompilation/layout or sampling.',
   'PrivateUsage and MEM_PRIVATE committed region totals are separate metrics.'])
 with (root/'comparison.json').open('x') as stream:json.dump(comparison,stream,indent=2)
 print(comparison['status']);print('spreads',spread)
 for run in results:print(run['run'],run['b02_to_b03'])
if __name__=='__main__':main()
