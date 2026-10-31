"""Read completed diagnostic server arms; preserve the original soak gate."""
import hashlib,json,statistics
from pathlib import Path
ROOT=Path('build/heap-optimize-resources-14')
def distribution(values):
 values=sorted(values)
 if not values:raise ValueError('empty measurements')
 return dict(n=len(values),min=values[0],median=statistics.median(values),p95=values[min(len(values)-1,int(.95*len(values)))],max=values[-1])
def main():
 driver=json.loads((ROOT/'driver-status.json').read_text())
 if driver['status']!='COMPLETE / DIAGNOSTIC ONLY':raise ValueError('Both 600-second arms must finish')
 result=dict(status='DIAGNOSTIC ANALYSIS COMPLETE / NO PRODUCTION ADOPTION',release='BLOCKED',same_executable_sha256=driver['binary_sha256'],pgo='OFF',arms={})
 for mode in ('OFF_','ON__'):
  out=ROOT/('soak-'+mode);status=json.loads((out/'status.json').read_text());samples=[json.loads(x) for x in (out/'samples.jsonl').read_text().splitlines()]
  telemetry=[json.loads(x) for x in (out/'server/heap-optimize.jsonl').read_text().splitlines()]
  timing=[json.loads(x) for x in (out/'unload-timings.jsonl').read_text().splitlines()]
  assert status['elapsed_seconds']>=600 and status['counts']['cycles']==len(samples)
  assert len(telemetry)==len(timing)==2*len(samples)
  assert [x['unload'] for x in telemetry]==list(range(1,len(telemetry)+1))
  assert all(t['selector_valid'] and t['before_ok'] and t['after_ok'] and t['timer_ok'] and t['frequency']>0 and t['called']==(mode=='ON__') for t in telemetry)
  measured=[row for row in samples if row['measured']];gate={}
  for case in ('unit','strings','inheritance','links'):
   rows=[row for row in measured if row['case']==case];assert len(rows)>=10
   gate[case]={field:statistics.median(r[field] for r in rows[-5:])-statistics.median(r[field] for r in rows[:5]) for field in ('private_bytes','handles')}
  assert gate==status['quiescent_last5_minus_first5_by_case']
  passed=all(g['private_bytes']<=128*2**20 and g['handles']<=16 for g in gate.values())
  result['arms'][mode]=dict(memory_gate='PASS_PRELIMINARY' if passed else 'FAIL',cycles=status['counts']['cycles'],measured_cycles=len(measured),elapsed_seconds=status['elapsed_seconds'],
   gate_by_case=gate,handle_min=min(r['handles'] for r in measured),handle_max=max(r['handles'] for r in measured),quiescent_private_bytes=distribution([r['private_bytes'] for r in measured]),
   all_API_success=all(t['success'] for t in telemetry) if mode=='ON__' else None,
   API_failures=[dict(unload=t['unload'],error=t['error']) for t in telemetry if t['called'] and not t['success']],
   API_interval_ms=distribution([t['ticks']/t['frequency']*1000 for t in telemetry]),
   immediate_private_reduction_bytes=distribution([t['before_private']-t['after_private'] for t in telemetry]),
   immediate_increase_count=sum(t['after_private']>t['before_private'] for t in telemetry),
   measured_TCP_UNLOAD_ms=distribution([t['tcp_unload_ms'] for t in timing if t['measured']]),correctness_and_same_TCP_through_UNLOAD='PASS within executed native/artifact/value/state and reconnect checks')
  result['arms'][mode]['measured_lifecycle_interval_ms_by_case']={case:distribution([(b['elapsed_seconds']-a['elapsed_seconds'])*1000 for a,b in zip(samples,samples[1:]) if a['measured'] and b['measured'] and b['case']==case]) for case in ('unit','strings','inheritance','links')}
  for phase in (0,1):
   chosen=[telemetry[2*i+phase] for i,s in enumerate(samples) if s['measured'] and s['case']=='unit']
   result['arms'][mode]['unit_after_publish_UNLOAD' if phase==0 else 'unit_after_load_UNLOAD']=dict(api_ms=distribution([t['ticks']/t['frequency']*1000 for t in chosen]),private_reduction_bytes=distribution([t['before_private']-t['after_private'] for t in chosen]))
 off=result['arms']['OFF_'];on=result['arms']['ON__']
 result['OFF_failure_reproduced']=off['memory_gate']=='FAIL'
 result['ON_original_gate_passed']=on['memory_gate']=='PASS_PRELIMINARY'
 result['candidate_effective_in_this_preliminary_AB']=result['OFF_failure_reproduced'] and result['ON_original_gate_passed'] and not on['API_failures']
 result['interpretation']='One sequential OFF/ON pair on a new instrumented image; no production deployment or release gate replacement. API success is not a guarantee of physical release.'
 executable=ROOT/'compiler/Release/ServerEngineV4.exe';assert hashlib.sha256(executable.read_bytes()).hexdigest()==driver['binary_sha256']
 with (ROOT/'comparison.json').open('x') as stream:json.dump(result,stream,indent=2)
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
