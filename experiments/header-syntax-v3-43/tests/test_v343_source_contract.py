#!/usr/bin/env python3
from pathlib import Path
import hashlib,sys
p=Path(__file__).resolve().parents[1]
want={
  'benchmarks/v3_38a_graph_producer.inc': 'd208d9617a7576efb4d535274671e807420578d20967f6678af9465d991451a9',
  'benchmarks/v3_42_graph_producer.inc': '9226057808ab3aabfc13389d29437824c209e1f3dce5dc6a861bcb9b84a734b4',
}
for name,digest in want.items():
 if hashlib.sha256((p/name).read_bytes()).hexdigest()!=digest:sys.exit('V343_SOURCE_CHANGED '+name)
a=(p/'benchmarks/v3_42_perf_benchmark.cpp').read_text()
b=(p/'benchmarks/v3_43_reverse_perf_benchmark.cpp').read_text()
old='#define v319 v338a\n#include "v3_38a_graph_producer.inc"\n#undef v319\n#define v319 v342\n#include "v3_42_graph_producer.inc"\n#undef v319'
new='#define v319 v342\n#include "v3_42_graph_producer.inc"\n#undef v319\n#define v319 v338a\n#include "v3_38a_graph_producer.inc"\n#undef v319'
if a.count(old)!=1 or b!=a.replace(old,new,1):sys.exit('V343_REVERSE_ORDER_NOT_ISOLATED')
print('V343_SOURCE_CONTRACT_PASS')
