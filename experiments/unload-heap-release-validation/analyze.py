import hashlib,json,statistics
from collections import Counter
from pathlib import Path
ROOT=Path('build/unload-heap-release-validation')
def rows(p):return [json.loads(x) for x in p.read_text().splitlines()]
def main():
 protocol=json.loads((ROOT/'protocol.json').read_text());out=ROOT/'run-01-integrated-on'
 s=json.loads((out/'status.json').read_text());samples=rows(out/'samples.jsonl');ops=rows(out/'operation-samples.jsonl');unloads=rows(out/'unload-responses.jsonl');timings=rows(out/'unload-timings.jsonl')
 assert s['status']=='PASS_PRELIMINARY' and s['elapsed_seconds']>=600
 assert s['binary_sha256']==protocol['sha256']==hashlib.sha256(Path(protocol['executable']).read_bytes()).hexdigest()
 assert len(samples)==s['counts']['cycles'] and len(ops)==len(unloads)==len(timings)==2*len(samples)
 assert [r['cycle'] for r in samples]==list(range(len(samples)))
 measured=[r for r in samples if r['measured']];gates={}
 for fixture in ('unit','strings','inheritance','links'):
  q=[r for r in measured if r['case']==fixture];assert len(q)>=10
  gates[fixture]={k:statistics.median(r[k] for r in q[-5:])-statistics.median(r[k] for r in q[:5]) for k in ('private_bytes','handles')}
 assert gates==s['quiescent_last5_minus_first5_by_case']
 assert all(v['private_bytes']<=128*2**20 and v['handles']<=16 for v in gates.values())
 gaps=[b['elapsed_seconds']-a['elapsed_seconds'] for a,b in zip(measured,measured[1:])];assert max(gaps)<60
 assert all(r['response']['status'] is True for r in unloads)
 warnings=[d for r in unloads for d in r['response'].get('diagnostics',[])]
 assert all(d['severity']=='warning' for d in warnings),warnings
 assert all(sum(r['cycle']==q['cycle'] for r in unloads)==2 for q in samples)
 faults=json.loads((ROOT/'tcp-faults/summary.json').read_text());assert faults['status']=='PASS' and len(faults['scenarios'])==4
 assert all(r['executable_sha256']==protocol['sha256'] and r['status']=='PASS' and r['same_TCP'] and r['recovered_without_restart'] for r in faults['scenarios'])
 host=json.loads((ROOT/'windows-host.json').read_text(encoding='utf-8-sig'));assert 'Windows 11' in host['Caption'] and host['OSArchitecture']=='64-bit'
 result=dict(status='PRELIMINARY RELEASE VALIDATION PASS ON TESTED WINDOWS 11 X64 / NO ADOPTION',release='BLOCKED pending explicit decision and remaining scope review',pgo='OFF',default='OFF',executable_sha256=protocol['sha256'],host=host,elapsed_seconds=s['elapsed_seconds'],counts=s['counts'],quiescent_gates=gates,max_checkpoint_gap_seconds=max(gaps),handles_min=min(r['handles'] for r in measured),handles_max=max(r['handles'] for r in measured),unload_responses=len(unloads),normal_soak_warning_counts=dict(Counter(str(d['id']) for d in warnings)),fault_scenarios=[r['mode'] for r in faults['scenarios']],limitations=['600 measured seconds is preliminary, not long-duration certification.','Windows 11 Home build 26200 x64 / windows-x64 pack8 fixed_direct SHM only; no other editions/builds/modes were executed.','Fault scenarios use debugger-forced return values/query data in owned processes; not spontaneous Windows failures or ordinary unmodified execution.','Missing-export/old-OS fallback and diagnostic-allocation failure are not injected through TCP here.','No new performance acceptance threshold; timings are not a performance gate.'])
 with (ROOT/'comparison.json').open('x') as f:json.dump(result,f,indent=2)
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
