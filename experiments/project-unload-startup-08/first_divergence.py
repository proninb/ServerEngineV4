"""Read-only analysis of nine preserved CONTROL-07 CSV/JSONL pairs."""
import argparse,csv,hashlib,json
from pathlib import Path
HEADERS=['boundary','cycle','stage','private_bytes','drift_bytes','active_workers','handles','live']
COUNTERS=dict(alloc=9536484,free=9536471,unmatched=159,live=13,workers_created=8781,workers_stopped=8781)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read_csv(path):
 with path.open(newline='') as stream:
  reader=csv.DictReader(stream)
  if reader.fieldnames!=HEADERS:raise ValueError('CSV schema mismatch: '+str(path))
  rows=[]
  for row in reader:
   rows.append({k:v if k=='stage' else int(v) for k,v in row.items()})
 return rows
def validate_pair(csv_rows,raw,expected_count=274):
 if len(csv_rows)!=expected_count or len(raw)!=expected_count+2:raise ValueError('boundary count')
 if raw[0].get('stage')!='initial' or raw[-1].get('stage')!='complete':raise ValueError('completion markers')
 if any(raw[-1].get(k)!=v for k,v in COUNTERS.items()):raise ValueError('terminal allocation counters')
 for index,(row,event) in enumerate(zip(csv_rows,raw[1:-1])):
  if row['boundary']!=index or event.get('boundary')!=index:raise ValueError('boundary order')
  for key in ('private_bytes','drift_bytes','active_workers','handles','live'):
   if row[key]!=event.get(key):raise ValueError('CSV/JSONL mismatch '+key)
  if row['private_bytes']-raw[0]['private_bytes']!=row['drift_bytes']:raise ValueError('baseline/drift mismatch')
 return tuple(tuple(row[k] for k in ('boundary','cycle','stage','active_workers','live')) for row in csv_rows)
def analyze(series,threshold):
 if threshold<=0 or len(series)<2:raise ValueError('threshold/runs')
 if len({len(s) for s in series})!=1:raise ValueError('different lengths')
 spreads=[];transitions=[]
 for index in range(len(series[0])):
  row=series[0][index];values=[s[index]['private_bytes'] for s in series];drifts=[s[index]['drift_bytes'] for s in series]
  identity={k:row[k] for k in ('boundary','cycle','stage')}
  spreads.append(dict(**identity,min_private_bytes=min(values),max_private_bytes=max(values),spread_bytes=max(values)-min(values),
   drift_spread_bytes=max(drifts)-min(drifts),per_run_private_bytes=values,per_run_drift_bytes=drifts))
  if index:
   deltas=[s[index]['private_bytes']-s[index-1]['private_bytes'] for s in series]
   transitions.append(dict(from_boundary=index-1,to_boundary=index,from_cycle=series[0][index-1]['cycle'],
    from_stage=series[0][index-1]['stage'],to_cycle=row['cycle'],to_stage=row['stage'],
    delta_spread_bytes=max(deltas)-min(deltas),min_delta_bytes=min(deltas),max_delta_bytes=max(deltas),per_run_delta_bytes=deltas))
 return dict(first_private_spread_at_threshold=next((s for s in spreads if s['spread_bytes']>=threshold),None),
  first_drift_spread_at_threshold=next((s for s in spreads if s['drift_spread_bytes']>=threshold),None),
  first_available_boundary=spreads[0],ranked_transitions=sorted(transitions,key=lambda x:-x['delta_spread_bytes']),boundaries=spreads)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--spread-mib',type=float,default=1);a=p.parse_args()
 manifest=json.loads((a.root/'comparison.json').read_text());series=[];records=[];reference=None
 if len(manifest['runs'])!=9:raise ValueError('nine runs required')
 for record in manifest['runs']:
  index=record['run'];raw_path=a.root/f'run-{index:02}.jsonl';csv_path=a.root/f'run-{index:02}.csv'
  if sha(raw_path)!=record['stdout_sha256']:raise ValueError('archived stdout hash mismatch')
  if record['exit_code']!=0 or record['before_hashes']!=manifest['hashes'] or record['after_hashes']!=manifest['hashes']:raise ValueError('original executable/input hash gate')
  raw=[json.loads(x) for x in raw_path.read_text().splitlines()];rows=read_csv(csv_path);signature=validate_pair(rows,raw)
  if reference is not None and signature!=reference:raise ValueError('inter-run boundary signature mismatch')
  reference=signature;series.append(rows);records.append(dict(run=index,pid=record['pid'],csv_sha256=sha(csv_path),jsonl_sha256=sha(raw_path),initial_private_bytes=raw[0]['private_bytes'],terminal_counters=raw[-1]))
 result=analyze(series,int(a.spread_mib*2**20))
 initial=[r['initial_private_bytes'] for r in records]
 result.update(status='OFFLINE ANALYSIS COMPLETE / CAUSE OPEN / NO ADOPTION',release='BLOCKED',threshold_bytes=int(a.spread_mib*2**20),
  threshold_purpose='Diagnostic first-divergence only, not release acceptance.',runs=records,input_hashes=manifest['hashes'],csv_schema=HEADERS,
  initial_private_bytes_spread=max(initial)-min(initial),
  counter_coverage='All terminal Alloc/Free counters match; per-boundary live/active counts match. Cumulative Alloc/Free at each boundary were not recorded and cannot be verified here.',
  transition_limits='Deltas span external checkpoints, including validation/LOAD/RUN/FREEZE as applicable; not exclusive function timing/ownership attribution.')
 a.out.parent.mkdir(parents=True,exist_ok=True)
 with a.out.open('x') as out:json.dump(result,out,indent=2)
 print(json.dumps({k:result[k] for k in ('status','initial_private_bytes_spread','first_private_spread_at_threshold','first_drift_spread_at_threshold')},indent=2))
if __name__=='__main__':main()
