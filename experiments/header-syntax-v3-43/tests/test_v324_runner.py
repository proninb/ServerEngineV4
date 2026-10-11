#!/usr/bin/env python3
"""Cross-platform regression guard for PowerShell case-insensitive parameter names."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
s=(root/'run_v3_24.ps1').read_text(encoding='utf-8')
assert re.search(r"\[int\]\$Pairs=7",s)
assert re.search(r"\$pairsCsvPath=Join-Path \$OutputDirectory 'v324-pairs.csv'",s)
for param in ('Pairs','WarmupPairs'):
    # PowerShell treats $pairs and $Pairs as the SAME variable. Neither may be reassigned.
    assert not re.search(r"^\s*\$"+param+r"\s*=",s,re.I|re.M),param
assert '& $perf $ProjectPath $pairsCsvPath $phases $summary $WarmupPairs $Pairs' in s
assert '$rows=@(Import-Csv -LiteralPath $pairsCsvPath)' in s
assert 'Write-Host "PairedReport=$pairsCsvPath"' in s
cpp=(root/'benchmarks/v3_24_perf_benchmark.cpp').read_text(encoding='utf-8')
assert 'parse_count(argv[5],"warmups",0,30)' in cpp
assert 'parse_count(argv[6],"pairs",1,100)' in cpp
assert 'const fs::path paths[]{argv[2],argv[3],argv[4]};' in cpp
print('V3-24-FIX2: runner argument and PowerShell case-insensitive collision check PASS')
