"""Reuse C9 correctness oracles; refine allocation groups without timed calls."""
from pathlib import Path
import sys
repo=Path(__file__).resolve().parents[2]
source=(repo/'experiments/parallel-local-source-10-c9/audit.py').read_text()
source=source.replace("repo=Path(__file__).resolve().parents[2]",'repo=Path('+repr(str(repo))+')')
source=source.replace("'build/parallel-local-source-10-c9/Release/SourceAudit10C9.exe'","'build/parallel-local-source-10-c10/Release/SourceAllocationAudit10C10.exe'")
mode=sys.argv[2] if len(sys.argv)>2 else '1';assert mode in ('0','1')
source=source.replace(' p=subprocess.run('," e['PLS10_C10_REUSE']="+repr(mode)+"\n p=subprocess.run(")
source=source.replace("f'audit-{arm}.stdout'",f"f'c10-audit-{mode}-{{arm}}.stdout'").replace("f'audit-{arm}.stderr'",f"f'c10-audit-{mode}-{{arm}}.stderr'").replace("'audit.json'",repr('c10-audit-'+mode+'.json')).replace('for g in (1,2,3)','for g in (1,2,3,4,5,6)')
exec(compile(source,__file__,'exec'))
if mode=='1':
 import json
 out=Path(sys.argv[1]).resolve()
 before=json.loads((out/'c10-audit-0.json').read_text());after=json.loads((out/'c10-audit-1.json').read_text())
 assert before['binary_hash']==after['binary_hash']
 a=next(r for r in before['results'] if r['arm']=='c7');b=next(r for r in after['results'] if r['arm']=='c7')
 counters=[k for k in a if k.startswith('c9_audit_') and not any(s in k for s in ('allocations','requested','peak','live','group'))]
 assert len(counters)==12 and all(a[k]==b[k] for k in counters),'semantic/Graph work changed'
 assert b['c9_audit_allocations']<a['c9_audit_allocations'],'scratch did not reduce allocations'
 print('Same audit binary: all 12 WHO/Graph counters unchanged; allocation reduction verified')
