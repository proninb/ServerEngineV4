import json,statistics,hashlib
from pathlib import Path
ROOT=Path('build/heap-optimize-performance-16')
def rows(p):return [json.loads(s) for s in p.read_text().splitlines()]
def stats(v):
 v=sorted(v)
 return dict(n=len(v),sum=sum(v),median=statistics.median(v),p95=v[min(len(v)-1,int(.95*len(v)))],max=max(v)) if v else dict(n=0)
def main():
 d=json.loads((ROOT/'driver-status.json').read_text());assert d['active_run'] is None and d['status'].startswith('COMPLETE')
 results=[]
 for run in d['runs']:
  out=ROOT/run['run'];s=json.loads((out/'status.json').read_text());q=rows(out/'samples.jsonl');t=rows(out/'server/heap-optimize.jsonl');ops=rows(out/'operation-samples.jsonl')
  assert len(q)==s['counts']['cycles'] and len(t)==len(ops)==2*len(q)
  assert [x['unload'] for x in t]==list(range(1,len(t)+1))
  assert all(x['before_ok'] and x['after_ok'] and x['timer_ok'] and x['selector_valid'] and x['mode']==run['mode'] and x['threshold']==120635392 for x in t)
  assert all(x['called']==(run['mode']=='POL_' and x['before_private']>=120635392) for x in t)
  gate={}
  for fixture in ('unit','strings','inheritance','links'):
   samples=[x for x in q if x['measured'] and x['case']==fixture];assert len(samples)>=10
   gate[fixture]={k:statistics.median(x[k] for x in samples[-5:])-statistics.median(x[k] for x in samples[:5]) for k in ('private_bytes','handles')}
  assert gate==s['quiescent_last5_minus_first5_by_case']
  measured=[x for x in q if x['measured']];warm=next(x for x in q if x['cycle']==9)
  # measured timer starts after warmup; do not subtract warmup's independent elapsed_seconds.
  intervals=[b['elapsed_seconds']-a['elapsed_seconds'] for a,b in zip(measured,measured[1:])]
  byseq={x['unload']:x for x in t};conditional=[]
  for op in ops:
   if op['command']!='PUBLISH' or not op['measured']:continue
   i=op['cycle'];previous=[byseq[n] for n in (2*i-1,2*i)] if i else []
   assert len(previous)==2
   prior=q[i-1];assert prior['cycle']==i-1
   conditional.append(dict(**op,previous_fixture=prior['case'],immediate_cleanup=previous[-1]['called'],any_previous_cycle_cleanup=any(x['called'] for x in previous),previous_API_ms=sum(x['ticks']/x['frequency']*1000 for x in previous if x['called'])))
  called=[x for x in t if x['called']];mt=[x for x in t if x['unload']>20 and x['called']]
  r=dict(run=run['run'],mode=run['mode'],elapsed_seconds=s['elapsed_seconds'],measured_cycles=len(measured),cycles_per_second=len(measured)/s['elapsed_seconds'],gate=gate,memory_gate_pass=all(v['private_bytes']<=128*2**20 and v['handles']<=16 for v in gate.values()),max_checkpoint_gap_seconds=max(intervals),continuous=max(intervals)<=60,API_calls=len(called),API_failures=[x for x in called if not x['success']],API_ms=stats([x['ticks']/x['frequency']*1000 for x in called]),measured_API_ms=stats([x['ticks']/x['frequency']*1000 for x in mt]),operations={},conditional={})
  for fixture in ('unit','strings','inheritance','links'):
   r['operations'][fixture]={command:{key:stats([x[field] for x in ops if x['measured'] and x['case']==fixture and x['command']==command]) for key,field in [('tcp_ms','tcp_ms'),('page_faults','page_faults_delta')]} for command in ('PUBLISH','LOAD')}
   for value in (False,True):
    load=[x for x in ops if x['measured'] and x['case']==fixture and x['command']=='LOAD' and byseq[2*x['cycle']+1]['called']==value]
    r['conditional'][f'{fixture}/LOAD_immediate_cleanup/{value}']=dict(tcp_ms=stats([x['tcp_ms'] for x in load]),faults=stats([x['page_faults_delta'] for x in load]))
   for criterion in ('immediate_cleanup','any_previous_cycle_cleanup'):
    for value in (False,True):
     a=[x for x in conditional if x['case']==fixture and x[criterion]==value]
     r['conditional'][f'{fixture}/{criterion}/{value}']=dict(tcp_ms=stats([x['tcp_ms'] for x in a]),faults=stats([x['page_faults_delta'] for x in a]),previous_fixtures=sorted(set(x['previous_fixture'] for x in a)))
  with (out/'conditional-publish.jsonl').open('x') as f:
   for row in conditional:f.write(json.dumps(row)+'\n')
  results.append(r)
 assert hashlib.sha256(Path('build/heap-optimize-policy-15/compiler/Release/ServerEngineV4.exe').read_bytes()).hexdigest()==d['binary_sha256']
 result=dict(status='MEASUREMENT COMPLETE / NO PERFORMANCE ACCEPTANCE THRESHOLD / NO ADOPTION',release='BLOCKED',pgo='OFF',binary_sha256=d['binary_sha256'],runs=results,limitations=['Conditional groups observational and fixture-dependent, not randomized causal estimates.','Process PageFaultCount includes soft/hard faults and other process activity.','Four processes and chronological drift do not establish robust performance equivalence.'])
 with (ROOT/'comparison.json').open('x') as f:json.dump(result,f,indent=2)
 print(json.dumps([dict(run=r['run'],gate=r['memory_gate_pass'],throughput=r['cycles_per_second'],API_ms=r['API_ms'],unit=r['operations']['unit']) for r in results],indent=2))
if __name__=='__main__':main()
