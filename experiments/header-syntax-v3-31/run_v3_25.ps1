# V3-25 Direct Graph Write experiment. No mutations in the source repository.
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
$names=@('v325-pairs.csv','v325-phases.csv','v325-summary.csv')
if(-not $SkipAllocationProfile){$names+='v325-allocations.csv'}
foreach($n in $names){
    $p=Join-Path $OutputDirectory $n
    if(Test-Path -LiteralPath $p){throw "Output must be new: $p"}
}
$workspace=Join-Path $OutputDirectory '_experimental_v325'
$overlay=Join-Path $workspace 'engine-overlay'
$build=Join-Path $workspace 'build'
$perf=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV325Perf.exe'
$alloc=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV325Alloc.exe'
if($SkipBuild -and -not (Test-Path -LiteralPath (Join-Path $overlay 'v325_prevalidated_deferred.marker') -PathType Leaf)){
    throw 'V3-25 requires a NEW isolated overlay and V3-25 executable.'
}
if(-not $SkipBuild){
    if(Test-Path -LiteralPath $overlay){throw "Isolated overlay already exists: $overlay. Use a NEW OutputDirectory."}
    New-Item -ItemType Directory -Path $workspace -Force | Out-Null
    & python (Join-Path $PSScriptRoot 'tools/build_v325_sources.py') '--repo' $RepoRoot '--overlay' $overlay
    if($LASTEXITCODE -ne 0){throw 'V3-25 overlay generation failed'}
    # Never compile a modified copy from the production source location.
    & cmake -S $PSScriptRoot -B $build "-DSERVERENGINE_ROOT=$($overlay.Replace('\','/'))" '-DSERVER_ENGINE_ENABLE_IPO=OFF' '-DBUILD_TESTING=OFF'
    if($LASTEXITCODE -ne 0){throw 'V3-25 configure failed'}
    & cmake --build $build --config Release --target ServerEngineV4HeaderSyntaxV325Perf --parallel 1
    if($LASTEXITCODE -ne 0){throw 'V3-25 MSVC performance build failed'}
    if(-not $SkipAllocationProfile){
        & cmake --build $build --config Release --target ServerEngineV4HeaderSyntaxV325Alloc --parallel 1
        if($LASTEXITCODE -ne 0){throw 'V3-25 MSVC allocation build failed'}
    }
}
if(-not (Test-Path -LiteralPath $perf -PathType Leaf)){throw "Missing V3-25 benchmark: $perf"}
if(-not $SkipAllocationProfile -and -not (Test-Path -LiteralPath $alloc -PathType Leaf)){
    throw "Missing V3-25 allocation probe: $alloc"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$pairsCsvPath=Join-Path $OutputDirectory 'v325-pairs.csv'
$phases=Join-Path $OutputDirectory 'v325-phases.csv'
$summary=Join-Path $OutputDirectory 'v325-summary.csv'
& $perf $ProjectPath $pairsCsvPath $phases $summary $WarmupPairs $Pairs
if($LASTEXITCODE -ne 0){throw "V3-25 Header Graph benchmark failed: $LASTEXITCODE"}
if(-not $SkipAllocationProfile){
    $allocations=Join-Path $OutputDirectory 'v325-allocations.csv'
    & $alloc '--alloc-only' $ProjectPath $allocations
    if($LASTEXITCODE -ne 0){throw "V3-25 allocations failed: $LASTEXITCODE"}
}
$culture=[Globalization.CultureInfo]::InvariantCulture
$rows=@(Import-Csv -LiteralPath $pairsCsvPath)
$measured=@($rows|Where-Object{$_.phase -ceq 'measure'})
$warm=@($rows|Where-Object{$_.phase -ceq 'warmup'})
if($measured.Count -ne $Pairs -or $warm.Count -ne $WarmupPairs){throw 'V3-25 wrong pair/warmup counts'}
if(@($rows|Where-Object{$_.header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or $_.full_publish_gate -cne 'PARTIAL'}).Count -gt 0){
    throw 'V3-25 parity or publish gate invalid'
}
for($i=0;$i -lt $measured.Count;$i++){
    $r=$measured[$i]
    $order=if($i%2 -eq 0){'V323-V325'}else{'V325-V323'}
    if([int]$r.pair -ne $i+1 -or $r.order -cne $order){throw "V3-25 invalid A/B order: $i"}
    foreach($field in @('v323_ms','v325_ms','saved_ms','speedup_pct')){
        $n=[double]::Parse([string]$r.$field,$culture)
        if([double]::IsNaN($n) -or [double]::IsInfinity($n)){throw "V3-25 invalid metric: $field"}
        if($field -in @('v323_ms','v325_ms') -and $n -le 0){throw 'Nonpositive producer time'}
    }
    $delta=[double]::Parse($r.v323_ms,$culture)-[double]::Parse($r.v325_ms,$culture)
    if([math]::Abs($delta-[double]::Parse($r.saved_ms,$culture)) -gt 0.005){throw 'V3-25 incorrect paired delta'}
}
$s=@(Import-Csv -LiteralPath $summary)
$p=@(Import-Csv -LiteralPath $phases)
if($s.Count -ne 1 -or $p.Count -ne 1){throw 'V3-25 invalid summary or profile'}
if($s[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or $s[0].full_publish_gate -cne 'PARTIAL' -or
   $p[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS'){throw 'V3-25 false PASS'}
if([int]$s[0].pairs -ne $Pairs -or [int]$s[0].warmups -ne $WarmupPairs){throw 'V3-25 wrong summary counts'}
$phaseTotal=0.0
foreach($key in @('syntax_scan_ms','identity_ms','record_index_ms','fields_ms','constructors_ms','inheritance_ms','graph_commit_ms','nested_ms','unattributed_ms')){
    $value=[double]::Parse([string]$p[0].$key,$culture)
    if([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0){throw "V3-25 bad phase: $key"}
    $phaseTotal+=$value
}
if([math]::Abs($phaseTotal-[double]::Parse($p[0].total_ms,$culture)) -gt 0.1){throw 'V3-25 phase accounting mismatch'}
if(-not $SkipAllocationProfile){
    $a=@(Import-Csv -LiteralPath $allocations)
    if($a.Count -ne 1 -or $a[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or
       $a[0].coverage -cne 'current_thread_global_new_only'){throw 'V3-25 allocation gate invalid'}
}
Write-Host "V3-25 VERIFIED: V323=$($s[0].median_v323_ms) ms; V325=$($s[0].median_v325_ms) ms; paired saved=$($s[0].median_paired_saved_ms) ms; wins=$($s[0].paired_wins)/$Pairs"
Write-Host "PairedReport=$pairsCsvPath"
Write-Host "ProfileReport=$phases"
Write-Host "SummaryReport=$summary"
if(-not $SkipAllocationProfile){Write-Host "AllocationReport=$allocations"}
Write-Host "Production repository unchanged: $RepoRoot"
