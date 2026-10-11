from pathlib import Path
import hashlib,json,sys
r=Path(__file__).resolve().parent.parent
b=r/'benchmarks'
m=json.loads((r/'V340_MANIFEST.json').read_text())
for file,key in [('v3_38a_graph_producer.inc','baseline_sha256'),('v3_40_graph_producer.inc','candidate_sha256'),('v3_40_perf_benchmark.cpp','benchmark_sha256')]:
    if hashlib.sha256((b/file).read_bytes()).hexdigest()!=m[key]:sys.exit('V340 SHA mismatch '+file)
a=(b/'v3_38a_graph_producer.inc').read_text()
c=(b/'v3_40_graph_producer.inc').read_text()
if '#ifdef CW_V340_DEEP_PROFILE' not in c:sys.exit('V340 missing guarded profiler')
# All additions must be in diagnostic preprocessor sections; no change to the producer fast path outside guards.
import re
def strip_guarded(t):
    lines=t.splitlines(keepends=True);out=[];i=0
    while i<len(lines):
        if lines[i].strip()=='#ifdef CW_V340_DEEP_PROFILE':
            depth=1;i+=1
            while i<len(lines) and depth:
                if lines[i].lstrip().startswith(('#ifdef ','#ifndef ','#if ')):depth+=1
                if lines[i].strip()=='#endif':depth-=1
                i+=1
        else:
            out.append(lines[i]);i+=1
    return ''.join(out)
# The sampled named timer now stores a value in order to break out hit/miss;
# compare after one semantics-neutral conversion.
normalized=strip_guarded(c)
normalized=normalized.replace("""        if(sampled){
            const double elapsed=elapsed_ms(named_start);
            out.profile->sampled_named_ms+=elapsed;
        }""","""        if(sampled)out.profile->sampled_named_ms+=elapsed_ms(named_start);""")
if normalized!=a:sys.exit('V340 NON-diagnostic source drift from V338A')
p=(b/'v3_40_perf_benchmark.cpp').read_text()
for n in ('run_header_v2_semantic(w,oracle)','member_where_mismatch','#include "v3_38a_graph_producer.inc"','#include "v3_40_graph_producer.inc"'):
    if n not in p:sys.exit('V340 missing '+n)
print('V340_SOURCE_CONTRACT_PASS')
