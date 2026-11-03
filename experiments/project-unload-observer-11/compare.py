import json,statistics,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'project-unload-control-07'))
from control_stability import sha,validate
from run_observer import MODES,schedule,SELECTOR
def stats(values):return dict(n=len(values),min=min(values),median=statistics.median(values),max=max(values),spread=max(values)-min(values))
def main():
 root=Path('build/project-unload-observer-11/run-01');capture=json.loads((root/'capture.json').read_text());base=Path('build/project-unload-thread-06');reference=[json.loads(x) for x in (base/'single.jsonl').read_text().splitlines()][1:-1];boundaries=json.loads((base/'boundaries.json').read_text())
 if capture['status']!='CAPTURE COMPLETE / ALL 16 VALIDATED' or [r['mode'] for r in capture['runs']]!=schedule():raise ValueError('capture/schedule gate')
 results=[]
 for run in capture['runs']:
  target=root/('run-%02d-%s'%(run['run'],run['mode']))
  if sha(target/'replay.jsonl')!=run['replay_sha256'] or sha(target/'protocol.jsonl')!=run['protocol_sha256'] or run['after_hashes']!=capture['hashes'] or run['before_hashes']!=capture['hashes'] or run['argv']!=capture['argv'] or run['base_environment_sha256']!=capture['base_environment_sha256']:raise ValueError('hash/launch gate')
  if run['selector_value']!=SELECTOR[run['mode']] or run['markers']!=(5 if run['mode'] in ('marker','ack') else 0):raise ValueError('selector/marker gate')
  cps=validate([json.loads(x) for x in (target/'replay.jsonl').read_text().splitlines()],reference);byid={x['boundary']:x for x in cps}
  result=dict(run=run['run'],block=run['block'],mode=run['mode'],pid=run['pid'],b01=byid[1]['private_bytes'],b02=byid[2]['private_bytes'],b03=byid[3]['private_bytes'],b02_to_b03=byid[3]['private_bytes']-byid[2]['private_bytes'],
   max_quiescent_drift=max(cp['drift_bytes'] for cp in cps if cp['stage_code']==2),selected_drift={str(boundaries[x['boundary']]['cycle']):x['drift_bytes'] for x in cps if x['stage_code']==2 and boundaries[x['boundary']]['cycle'] in (70,80,90)})
  results.append(result)
 modes={mode:{key:stats([r[key] for r in results if r['mode']==mode]) for key in ('b01','b02','b03','b02_to_b03','max_quiescent_drift')} for mode in MODES}
 for mode in modes:modes[mode]['selected_drift']={c:stats([r['selected_drift'][c] for r in results if r['mode']==mode]) for c in ('70','80','90')}
 contrasts={}
 for left,right in [('original','off'),('off','marker'),('marker','ack')]:
  contrasts[right+' minus '+left]={key:dict(median_difference=modes[right][key]['median']-modes[left][key]['median'],block_differences=[next(r[key] for r in results if r['block']==block and r['mode']==right)-next(r[key] for r in results if r['block']==block and r['mode']==left) for block in range(1,5)]) for key in ('b01','b02','b03','b02_to_b03','max_quiescent_drift')}
 result=dict(status='COMPLETE / DESCRIPTIVE ONLY / NO ADOPTION',release='BLOCKED',runs=results,modes=modes,contrasts=contrasts,
  all_16_274_signatures_and_counters_verified=True,causal_mechanism_proven=False,
  all_modes_b03_spread_over_1MiB=all(modes[m]['b03']['spread']>2**20 for m in MODES),
  off_lower_max_than_original_in_all_four_blocks=all(d<0 for d in contrasts['off minus original']['max_quiescent_drift']['block_differences']),
  decision='Original/off diagnostic-image plus no-op-hook contrast is substantial in this sample. All modes still have b03 spread >1 MiB; control initial heap state before attributing individual address maps. No single mechanism proved.',
  limitations=['Four processes per mode; uncontrolled initial heap state remains.',
   'Diagnostic off includes mode lookup and no-op hook calls; original/off is image plus hook implementation contrast.',
   'Marker includes output/flush and parent parsing; ack additionally includes wait, scheduling and C transport.',
   'No address maps, allocator-path ETW, observer09 off mode or private domain.'])
 with (root/'comparison.json').open('x') as stream:json.dump(result,stream,indent=2)
 print(json.dumps(modes,indent=2))
if __name__=='__main__':main()
