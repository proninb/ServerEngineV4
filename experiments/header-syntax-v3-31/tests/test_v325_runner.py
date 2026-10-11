from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
s=(root/'run_v3_25.ps1').read_text()
assert '[int]$Pairs=7' in s
assert "$pairsCsvPath=Join-Path $OutputDirectory 'v325-pairs.csv'" in s
assert not re.search(r'^\s*\$Pairs\s*=', s, re.I|re.M)
assert '& $perf $ProjectPath $pairsCsvPath $phases $summary $WarmupPairs $Pairs' in s
assert 'v325_prevalidated_deferred.marker' in s
assert 'tools/build_v325_sources.py' in s
assert 'HEADER_GRAPH_PARITY_PASS' in s
cpp=(root/'benchmarks/v3_25_perf_benchmark.cpp').read_text()
assert '#include "v3_25_graph_producer.inc"' in cpp
assert 'v323::emit' in cpp and 'v325::emit' in cpp
assert 'full_parity(old' in cpp and 'v325_parity(neu' in cpp
assert 'parse_count(argv[5],"warmups",0,30)' in cpp
assert 'parse_count(argv[6],"pairs",1,100)' in cpp
print('V3-25 RUNNER / A-B / PARITY GUARDS PASS')
