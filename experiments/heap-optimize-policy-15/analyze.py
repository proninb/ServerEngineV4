import hashlib,json,statistics
from pathlib import Path
ROOT=Path('build/heap-optimize-policy-15')
def stats(values):
 values=sorted(values)
 if not values:raise ValueError('empty measurements')
 return dict(n=len(values),median=statistics.median(values),p95=values[min(len(values)-1,int(.95*len(values)))],max=values[-1],min=values[0])
def main():
 driver=json.loads((ROOT/'driver-status.json').read_text());assert driver['status']=='COMPLETE / DIAGNOSTIC ONLY'
 threshold=driver['calibration']['threshold_bytes'];results=[]
 for run in driver['runs']:
  out=ROOT/run['run'];s=json.loads((out/'status.json').read_text());samples=[json.loads(x) for x in (out/'samples.jsonl').open()]
  telemetry=[json.loads(x) for x in (out/'server/heap-optimize.jsonl').open()];operations=[json.loads(x) for x in (out/'operation-samples.jsonl').open()]
  timing=[json.loads(x) for x in (out/'unload-timings.jsonl').open()]
  assert s['elapsed_seconds']>=600 and len(samples)==s['counts']['cycles'] and len(telemetry)==len(operations)==len(timing)==2*len(samples)
  assert [t['unload'] for t in telemetry]==list(range(1,len(telemetry)+1))
  assert all(t['before_ok'] and t['after_ok'] and t['timer_ok'] and t['selector_valid'] and t['mode']==run['mode'] and t['threshold']==threshold for t in telemetry)
  assert all(t['called']==(run['mode']=='EACH' or (run['mode']=='POL_' and t['before_private']>=threshold)) for t in telemetry)
  measured=[x for x in samples if x['measured']];gate={}
  for case in ('unit','strings','inheritance','links'):
   rows=[x for x in measured if x['case']==case];assert len(rows)>=10
   gate[case]={key:statistics.median(x[key] for x in rows[-5:])-statistics.median(x[key] for x in rows[:5]) for key in ('private_bytes','handles')}
  assert gate==s['quiescent_last5_minus_first5_by_case']
  passed=all(x['private_bytes']<=128*2**20 and x['handles']<=16 for x in gate.values())
  called=[t for t in telemetry if t['called']]
  result=dict(run=run['run'],mode=run['mode'],memory_gate='PASS_PRELIMINARY' if passed else 'FAIL',elapsed_seconds=s['elapsed_seconds'],measured_cycles=len(measured),gate=gate,
   API_calls=len(called),UNLOADs=len(telemetry),call_fraction=len(called)/len(telemetry),API_failures=[t['error'] for t in called if not t['success']],
   API_interval_ms=stats([t['ticks']/t['frequency']*1000 for t in called]) if called else None,
   measured_TCP_UNLOAD_ms=stats([t['tcp_unload_ms'] for t in timing if t['measured']]),
   quiescent_private_bytes=stats([x['private_bytes'] for x in measured]),handle_min=min(x['handles'] for x in measured),handle_max=max(x['handles'] for x in measured),operations={})
  for case in ('unit','strings','inheritance','links'):
   result['operations'][case]={command:dict(tcp_ms=stats([x['tcp_ms'] for x in operations if x['measured'] and x['case']==case and x['command']==command]),
    page_faults=stats([x['page_faults_delta'] for x in operations if x['measured'] and x['case']==case and x['command']==command])) for command in ('PUBLISH','LOAD')}
  result['unit_lifecycle_ms']=stats([(b['elapsed_seconds']-a['elapsed_seconds'])*1000 for a,b in zip(samples,samples[1:]) if a['measured'] and b['measured'] and b['case']=='unit'])
  intervals=[b['elapsed_seconds']-a['elapsed_seconds'] for a,b in zip(samples,samples[1:]) if b['measured']]
  result['max_checkpoint_interval_seconds']=max(intervals)
  result['continuity_warning']=max(intervals)>60
  result['performance_comparison_eligible']=not result['continuity_warning']
  results.append(result)
 summary={mode:dict(runs=[r['run'] for r in results if r['mode']==mode],all_memory_gates_pass=all(r['memory_gate']=='PASS_PRELIMINARY' for r in results if r['mode']==mode),
  unit_publish_medians_ms=[r['operations']['unit']['PUBLISH']['tcp_ms']['median'] for r in results if r['mode']==mode],unit_load_medians_ms=[r['operations']['unit']['LOAD']['tcp_ms']['median'] for r in results if r['mode']==mode],
  unit_publish_fault_medians=[r['operations']['unit']['PUBLISH']['page_faults']['median'] for r in results if r['mode']==mode],API_call_fractions=[r['call_fraction'] for r in results if r['mode']==mode]) for mode in ('OFF_','EACH','POL_')}
 result=dict(status='COMPLETE / DIAGNOSTIC ONLY / NO PRODUCTION ADOPTION',release='BLOCKED',same_executable_sha256=driver['binary_sha256'],calibration=driver['calibration'],runs=results,summary=summary,
  limitations=['Two independent runs per mode, not a large performance confidence study.',
  'TCP operation latency includes dispatch/network; process PageFaultCount includes soft and hard faults and concurrent TCP activity.',
  'Fixed threshold is derived on prior workload; no universally minimal frequency is proven.',
  'Original 600-second per-fixture median memory gate is unchanged.'])
 assert hashlib.sha256((ROOT/'compiler/Release/ServerEngineV4.exe').read_bytes()).hexdigest()==driver['binary_sha256']
 with (ROOT/'comparison.json').open('x') as out:json.dump(result,out,indent=2)
 print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
