#!/usr/bin/env python3
from pathlib import Path
import hashlib,re,sys
p=Path(__file__).resolve().parents[1]
control=(p/'benchmarks/v3_38a_graph_producer.inc').read_bytes()
expect='d208d9617a7576efb4d535274671e807420578d20967f6678af9465d991451a9'
if hashlib.sha256(control).hexdigest()!=expect:sys.exit('V341 frozen V338A mismatch')
old=(p/'benchmarks/v3_40_graph_producer.inc').read_text()
new=(p/'benchmarks/v3_41_graph_producer.inc').read_text()
# Every change to the V340 diagnostic producer is enclosed by its own macro.
stripped=re.sub(r'(?m)^#ifdef CW_V341_FIELDS_PROFILE\n.*?^#endif\n','',new,flags=re.S)
if stripped!=old:sys.exit('V341 changed semantics outside isolated profiler code')
if new.count('#ifdef CW_V341_FIELDS_PROFILE')<15:sys.exit('V341 missing field instrumentation')
print('V341 SOURCE_CONTRACT_PASS frozen V338A and diagnostic-only V341')
