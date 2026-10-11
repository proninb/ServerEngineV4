# V3-27 Exact Derived Type Cache experiment. No mutations in the source repository.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$ProjectPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(1,100)][int]$Pairs=7,
    [ValidateRange(0,30)][int]$WarmupPairs=2,
    [switch]$SkipBuild,
    [switch]$SkipAllocationProfile
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot -ErrorAction Stop).Path
$ProjectPath=(Resolve-Path -LiteralPath $ProjectPath -ErrorAction Stop).Path
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$names=@('v327-pairs.csv','v327-phases.csv','v327-summary.csv')
if(-not $SkipAllocationProfile){$names+='v327-allocations.csv'}
foreach($n in $names){
    $p=Join-Path $OutputDirectory $n
    if(Test-Path -LiteralPath $p){throw "Output must be new: $p"}
}
$workspace=Join-Path $OutputDirectory '_experimental_v327'
$overlay=Join-Path $workspace 'engine-overlay'
$build=Join-Path $workspace 'build'
$perf=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV327Perf.exe'
$alloc=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV327Alloc.exe'
if($SkipBuild -and -not (Test-Path -LiteralPath (Join-Path $overlay 'v327_derived_cache.marker') -PathType Leaf)){
    throw 'V3-27 requires a NEW isolated overlay and V3-27 executable.'
}
if(-not $SkipBuild){
    if(Test-Path -LiteralPath $overlay){throw "Isolated overlay already exists: $overlay. Use a NEW OutputDirectory."}
    New-Item -ItemType Directory -Path $workspace -Force | Out-Null
    & python (Join-Path $PSScriptRoot 'tools/build_v327_sources.py') '--repo' $RepoRoot '--overlay' $overlay
    if($LASTEXITCODE -ne 0){throw 'V3-27 overlay generation failed'}
    # Never compile a modified copy from the production source location.
    & cmake -S $PSScriptRoot -B $build "-DSERVERENGINE_ROOT=$($overlay.Replace('\','/'))" '-DSERVER_ENGINE_ENABLE_IPO=OFF' '-DBUILD_TESTING=OFF'
    if($LASTEXITCODE -ne 0){throw 'V3-27 configure failed'}
    & cmake --build $build --config Release --target ServerEngineV4HeaderSyntaxV327Perf --parallel 1
    if($LASTEXITCODE -ne 0){throw 'V3-27 MSVC performance build failed'}
    if(-not $SkipAllocationProfile){
        & cmake --build $build --config Release --target ServerEngineV4HeaderSyntaxV327Alloc --parallel 1
        if($LASTEXITCODE -ne 0){throw 'V3-27 MSVC allocation build failed'}
    }
}
if(-not (Test-Path -LiteralPath $perf -PathType Leaf)){throw "Missing V3-27 benchmark: $perf"}
if(-not $SkipAllocationProfile -and -not (Test-Path -LiteralPath $alloc -PathType Leaf)){
    throw "Missing V3-27 allocation probe: $alloc"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$pairsCsvPath=Join-Path $OutputDirectory 'v327-pairs.csv'
$phases=Join-Path $OutputDirectory 'v327-phases.csv'
$summary=Join-Path $OutputDirectory 'v327-summary.csv'
& $perf $ProjectPath $pairsCsvPath $phases $summary $WarmupPairs $Pairs
if($LASTEXITCODE -ne 0){throw "V3-27 Header Graph benchmark failed: $LASTEXITCODE"}
if(-not $SkipAllocationProfile){
    $allocations=Join-Path $OutputDirectory 'v327-allocations.csv'
    & $alloc '--alloc-only' $ProjectPath $allocations
    if($LASTEXITCODE -ne 0){throw "V3-27 allocations failed: $LASTEXITCODE"}
}
$culture=[Globalization.CultureInfo]::InvariantCulture
$rows=@(Import-Csv -LiteralPath $pairsCsvPath)
$measured=@($rows|Where-Object{$_.phase -ceq 'measure'})
$warm=@($rows|Where-Object{$_.phase -ceq 'warmup'})
if($measured.Count -ne $Pairs -or $warm.Count -ne $WarmupPairs){throw 'V3-27 wrong pair/warmup counts'}
if(@($rows|Where-Object{$_.header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or $_.full_publish_gate -cne 'PARTIAL'}).Count -gt 0){
    throw 'V3-27 parity or publish gate invalid'
}
for($i=0;$i -lt $measured.Count;$i++){
    $r=$measured[$i]
    $order=if($i%2 -eq 0){'V326-V327'}else{'V327-V326'}
    if([int]$r.pair -ne $i+1 -or $r.order -cne $order){throw "V3-27 invalid A/B order: $i"}
    foreach($field in @('v326_ms','v327_ms','saved_ms','speedup_pct')){
        $n=[double]::Parse([string]$r.$field,$culture)
        if([double]::IsNaN($n) -or [double]::IsInfinity($n)){throw "V3-27 invalid metric: $field"}
        if($field -in @('v326_ms','v327_ms') -and $n -le 0){throw 'Nonpositive producer time'}
    }
    $delta=[double]::Parse($r.v326_ms,$culture)-[double]::Parse($r.v327_ms,$culture)
    if([math]::Abs($delta-[double]::Parse($r.saved_ms,$culture)) -gt 0.005){throw 'V3-27 incorrect paired delta'}
}
$s=@(Import-Csv -LiteralPath $summary)
$p=@(Import-Csv -LiteralPath $phases)
if($s.Count -ne 1 -or $p.Count -ne 1){throw 'V3-27 invalid summary or profile'}
if($s[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or $s[0].full_publish_gate -cne 'PARTIAL' -or
   $p[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS'){throw 'V3-27 false PASS'}
if([int]$s[0].pairs -ne $Pairs -or [int]$s[0].warmups -ne $WarmupPairs){throw 'V3-27 wrong summary counts'}
$phaseTotal=0.0
foreach($key in @('syntax_scan_ms','identity_ms','record_index_ms','fields_ms','constructors_ms','inheritance_ms','graph_commit_ms','nested_ms','unattributed_ms')){
    $value=[double]::Parse([string]$p[0].$key,$culture)
    if([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0){throw "V3-27 bad phase: $key"}
    $phaseTotal+=$value
}
if([math]::Abs($phaseTotal-[double]::Parse($p[0].total_ms,$culture)) -gt 0.1){throw 'V3-27 phase accounting mismatch'}
$profileTotal=[double]::Parse($p[0].fields_ms,$culture)
$typeSubtotal=0.0
foreach($key in @('type_base_ms','type_shape_ms','type_derived_ms')){
    $value=[double]::Parse([string]$p[0].$key,$culture)
    if([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0){throw "V3-27 bad type phase: $key"}
    $typeSubtotal+=$value
}
if($typeSubtotal -gt $profileTotal + 0.5){throw 'V3-27 type timers exceed fields timer'}
$counts=0L
foreach($key in @('type_cache_hits','type_cache_misses','type_fallbacks','type_fast_builtin')){
    $v=[long]::Parse([string]$p[0].$key,$culture)
    if($v -lt 0){throw "V3-27 invalid count: $key"}
    $counts+=$v
}
# A failed named lookup may also go through the fallback resolver. Do not
# require exactly one count per field when cache misses and fallback overlap.
$fastCount=[long]::Parse([string]$p[0].type_cache_hits,$culture)+[long]::Parse([string]$p[0].type_fast_builtin,$culture)
if($fastCount -gt [long]::Parse([string]$s[0].members,$culture)){
    throw 'V3-27 fast path count exceeds member total'
}
if(-not $SkipAllocationProfile){
    $a=@(Import-Csv -LiteralPath $allocations)
    if($a.Count -ne 1 -or $a[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or
       $a[0].coverage -cne 'current_thread_global_new_only'){throw 'V3-27 allocation gate invalid'}
}
$derivedCalls=[long]::Parse([string]$p[0].derived_calls,$culture)
$derivedHits=[long]::Parse([string]$p[0].derived_hits,$culture)
$derivedMisses=[long]::Parse([string]$p[0].derived_misses,$culture)
$derivedCreated=[long]::Parse([string]$p[0].derived_graph_created,$culture)
if($derivedCalls -ne $derivedHits+$derivedMisses -or $derivedCreated -gt $derivedMisses){
    throw 'V3-27 derived-cache counters invalid'
}
if($derivedCreated -ne [long]::Parse([string]$s[0].derived,$culture)){
    throw 'V3-27 derived creation count differs from canonical Graph'
}
Write-Host "V3-27 derived: calls=$derivedCalls hits=$derivedHits misses=$derivedMisses created=$derivedCreated"
Write-Host "V3-27 VERIFIED: V326=$($s[0].median_v326_ms) ms; V327=$($s[0].median_v327_ms) ms; paired saved=$($s[0].median_paired_saved_ms) ms; wins=$($s[0].paired_wins)/$Pairs"
Write-Host "PairedReport=$pairsCsvPath"
Write-Host "ProfileReport=$phases"
Write-Host "V3-27 type profile: named_hits=$($p[0].type_cache_hits) named_misses=$($p[0].type_cache_misses) builtin=$($p[0].type_fast_builtin) fallback=$($p[0].type_fallbacks)"
Write-Host "SummaryReport=$summary"
if(-not $SkipAllocationProfile){Write-Host "AllocationReport=$allocations"}
Write-Host "Production repository unchanged: $RepoRoot"
