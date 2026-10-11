#!/usr/bin/env python3
# Focused source read-site guard; not a substitute for C++ runtime or ASan tests.
from pathlib import Path
import re,sys
b=Path(__file__).resolve().parents[1]/'benchmarks'
expected={
 'v3_42_canonical.hpp': {'expected.layers[count-i-1]':1},
 'v3_42_graph_producer.inc': {'spec.layers[i].kind':4,'spec.layers[i].payload':4},
}
for filename,sites in expected.items():
 content=(b/filename).read_text()
 for pattern in sites:
  if content.count(pattern)!=sites[pattern]:sys.exit(f'V343_ACCESS_SITE_DRIFT {filename} {pattern}: {content.count(pattern)}')
 if filename.endswith('canonical.hpp'):
  guards=['if(count!=expected.length)', 'for(unsigned i=0;i<count;++i)', 'if(!expected.push(']
 else:
  guards=['for(std::uint32_t i=0;i<spec.length;++i)', 'for(std::uint32_t i=0;i<spec.length;++i)']
 # Loop forms are explicitly validated below from the actual source snippets.
 if filename.endswith('canonical.hpp') and not all(x in content for x in guards):
  sys.exit('V343_CANONICAL_GUARD_DRIFT')
# Producer has two consumer loops. They may use auto or unsigned instead of u32.
s=(b/'v3_42_graph_producer.inc').read_text()
loops=re.findall(r'for\s*\([^)]*i\s*<\s*spec\.length[^)]*\)',s)
if len(loops)<2:sys.exit('V343_PRODUCER_BOUND_GUARD_DRIFT '+str(loops))
# Scope-chain / fast-type never perform direct indexed layer reads.
for filename in ['v3_42_fast_type.hpp','v3_42_scope_chain.hpp']:
 if re.search(r'\.layers\s*\[', (b/filename).read_text()):sys.exit('V343_UNAUDITED_LAYER_READ '+filename)
print('V343_ACCESS_AUDIT_PASS canonical and producer indexed read sites guarded')
