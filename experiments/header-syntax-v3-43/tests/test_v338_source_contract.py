from pathlib import Path
import hashlib,json,sys
root=Path(__file__).resolve().parent.parent
b=root/'benchmarks'
m=json.loads((root/'V338_MANIFEST.json').read_text())
for name,expect in m['sha256'].items():
    actual=hashlib.sha256((b/name).read_bytes()).hexdigest()
    if actual!=expect:sys.exit('V338 frozen SHA mismatch: '+name)
control=(b/'v3_37_graph_producer.inc').read_text()
a=(b/'v3_38a_graph_producer.inc').read_text()
bb=(b/'v3_38b_graph_producer.inc').read_text()
assert control.count('events.reserve(ns.size()+parsed.syntax.records.size()+parsed.syntax.declarations.size());')==1
assert control.count('out.G.begin_direct_record(record_handle,rec.declaration_count)')==1
assert a==control.replace('events.reserve(ns.size()+parsed.syntax.records.size()+parsed.syntax.declarations.size());','events.reserve(ns.size()+parsed.syntax.records.size());')
assert bb==control.replace('#include "v3_23_direct_index.hpp"','#include "v3_23_direct_index.hpp"\n#include "v337r_owner_ranges.hpp"',1).replace('out.G.begin_direct_record(record_handle,rec.declaration_count)','out.G.begin_direct_record(record_handle,\n                 v337r_ranges::owned_declaration_count(parsed.syntax,ri))')
for mode in ('a','b'):
    s=(b/f'v3_38{mode}_perf_benchmark.cpp').read_text()
    assert '#include "v3_37_graph_producer.inc"' in s
    assert f'#include "v3_38{mode}_graph_producer.inc"' in s
    assert 'v3_37r_fields_graph_producer.inc' not in s
    assert s.count('HEADER_GRAPH_PARITY_PASS')>=2
    assert 'run_header_v2_semantic(w,oracle)' in s
print('V338_SOURCE_CONTRACT_PASS')
