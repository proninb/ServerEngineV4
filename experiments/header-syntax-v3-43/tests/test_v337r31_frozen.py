from pathlib import Path
import hashlib,sys
base=Path(__file__).resolve().parent.parent/'benchmarks'
EXPECTED={
    'v3_37_graph_producer.inc':'7b122b66427bd6a5883835b1ef8f2f99d977b871cc62e1de544f05ab3c4c6579',
    'v3_37r_graph_producer.inc':'59f602ebd9d65d93f0e4022f2f696793812be3e0308f43066c7b9a67c1818e45',
    'v3_37r_perf_benchmark.cpp':'e78af5901e3d91b7535c067604ca5416afd304185ea244dd4a7ad41752bfc679',
    'header_syntax_v3_06.hpp':'a0e0522b8c941dbaba70f99780adf62ed16b5d063ef73609fa286e1277dff5e0',
}
for name,expect in EXPECTED.items():
    digest=hashlib.sha256((base/name).read_bytes()).hexdigest()
    if digest!=expect:sys.exit(f'FROZEN SOURCE MISMATCH: {name}: {digest} != {expect}')
# Diagnostic includes a distinct instrumented producer, never included in Perf control.
if 'v3_37r_fields_graph_producer.inc' in (base/'v3_37r_perf_benchmark.cpp').read_text():
    sys.exit('Diagnostic instrumentation contaminated timed A/B')
if '#include "v3_37r_fields_graph_producer.inc"' not in (base/'v3_37r_fields_benchmark.cpp').read_text():
    sys.exit('Fields executable is not using the diagnostic producer')
print('V337R31_FROZEN_PASS unchanged V337/V337R performance sources')
