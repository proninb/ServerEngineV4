# V3-23: balanced paired A/B, separate V3 profile and allocation probe.
# No source mutation. New output files only; never overwrite benchmark results.
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
$reports=@('v323-pairs.csv','v323-phases.csv','v323-summary.csv','v323-hash-direct.csv')
if(-not $SkipAllocationProfile){$reports+= 'v323-allocations.csv'}
foreach($name in $reports){
    $path=Join-Path $OutputDirectory $name
    if(Test-Path -LiteralPath $path){throw "Result file must be new: $path"}
}
$build=Join-Path $PSScriptRoot 'build'
$perfExe=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV323Perf.exe'
$allocExe=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV323Alloc.exe'
if(-not $SkipBuild){
    $repoArgument=$RepoRoot.Replace('\','/')
    & cmake -S $PSScriptRoot -B $build "-DSERVERENGINE_ROOT=$repoArgument" '-DSERVER_ENGINE_ENABLE_IPO=OFF'
    if($LASTEXITCODE -ne 0){throw 'V3-23 CMake configure failed'}
    & cmake --build $build --config Release --target ServerEngineV4HeaderSyntaxV323Perf --parallel 1
    if($LASTEXITCODE -ne 0){throw 'V3-23 MSVC performance build failed'}
    if(-not $SkipAllocationProfile){
        & cmake --build $build --config Release --target ServerEngineV4HeaderSyntaxV323Alloc --parallel 1
        if($LASTEXITCODE -ne 0){throw 'V3-23 MSVC allocation build failed'}
    }
}
if(-not (Test-Path -LiteralPath $perfExe -PathType Leaf)){throw "Missing benchmark executable: $perfExe"}
if(-not $SkipAllocationProfile -and -not (Test-Path -LiteralPath $allocExe -PathType Leaf)){
    throw "Missing allocation executable: $allocExe"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$directPath=Join-Path $OutputDirectory 'v323-hash-direct.csv'
& $perfExe '--hash-direct-ab' $ProjectPath $directPath $WarmupPairs $Pairs
if($LASTEXITCODE -ne 0){throw "V3-23 hash-direct executable failed: $LASTEXITCODE"}
$pairsPath=Join-Path $OutputDirectory 'v323-pairs.csv'
$phasePath=Join-Path $OutputDirectory 'v323-phases.csv'
$summaryPath=Join-Path $OutputDirectory 'v323-summary.csv'
& $perfExe $ProjectPath $pairsPath $phasePath $summaryPath $WarmupPairs $Pairs
if($LASTEXITCODE -ne 0){throw "V3-23 paired executable failed: $LASTEXITCODE"}
if(-not $SkipAllocationProfile){
    $allocationPath=Join-Path $OutputDirectory 'v323-allocations.csv'
    & $allocExe '--alloc-only' $ProjectPath $allocationPath
    if($LASTEXITCODE -ne 0){throw "V3-23 allocation executable failed: $LASTEXITCODE"}
}
$culture=[Globalization.CultureInfo]::InvariantCulture
$records=@(Import-Csv -LiteralPath $pairsPath)
$measured=@($records | Where-Object {$_.phase -ceq 'measure'})
$warm=@($records | Where-Object {$_.phase -ceq 'warmup'})
if($measured.Count -ne $Pairs -or $warm.Count -ne $WarmupPairs){throw 'V3-23 measured/warmup count mismatch'}
if(@($records | Where-Object {$_.header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS'}).Count -ne 0){
    throw 'V3-23 pair Header Graph gate failed'
}
for($i=0;$i -lt $measured.Count;$i++){
    $r=$measured[$i]
    $order=if($i%2 -eq 0){'V3-V2'}else{'V2-V3'}
    if($r.order -cne $order -or [int]$r.pair -ne $i+1){throw "V3-23 pair order mismatch: $i"}
    foreach($name in @('v3_ms','v2_ms','saved_ms','speedup_pct')){
        $n=[double]::Parse([string]$r.$name,$culture)
        if([double]::IsNaN($n) -or [double]::IsInfinity($n)){throw "Invalid V3-23 $name"}
        if(($name -eq 'v3_ms' -or $name -eq 'v2_ms') -and $n -le 0){throw "Nonpositive V3-23 time $name"}
    }
    if([math]::Abs(([double]::Parse($r.v2_ms,$culture)-[double]::Parse($r.v3_ms,$culture)) - [double]::Parse($r.saved_ms,$culture)) -gt 0.005){
        throw "V3-23 paired delta incorrect in row $i"
    }
}
$direct=@(Import-Csv -LiteralPath $directPath)
if(@($direct | Where-Object {$_.phase -ceq 'measure'}).Count -ne $Pairs -or
   @($direct | Where-Object {$_.header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS'}).Count -ne 0){
   throw 'V3-23 hash-direct paired parity or count failure'
}
for($i=0;$i -lt $Pairs;$i++){
    $row=@($direct | Where-Object {$_.phase -ceq 'measure'})[$i]
    $expected=if($i%2 -eq 0){'HASH-DIRECT'}else{'DIRECT-HASH'}
    if($row.order -cne $expected -or [int]$row.pair -ne $i+1){
       throw "V3-23 incorrect direct pair order: $i"
    }
    foreach($name in @('hash_v322_ms','direct_v323_ms','saved_ms')){
        $n=[double]::Parse([string]$row.$name,$culture)
        if([double]::IsNaN($n) -or [double]::IsInfinity($n)){
            throw "Invalid direct benchmark $name"
        }
    }
    if([math]::Abs(([double]::Parse($row.hash_v322_ms,$culture)-
       [double]::Parse($row.direct_v323_ms,$culture))-
       [double]::Parse($row.saved_ms,$culture)) -gt 0.005){
       throw "Incorrect direct paired delta $i"
    }
}
$summary=@(Import-Csv -LiteralPath $summaryPath)
$phase=@(Import-Csv -LiteralPath $phasePath)
if($summary.Count -ne 1 -or $phase.Count -ne 1){throw 'V3-23 summary/profile must have one row'}
$s=$summary[0];$p=$phase[0]
if($s.header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or $s.full_publish_gate -cne 'PARTIAL' -or
   $p.header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS'){
    throw 'V3-23 summary/profile false PASS'
}
if([int]$s.pairs -ne $Pairs -or [int]$s.warmups -ne $WarmupPairs){throw 'V3-23 summary counts incorrect'}
$sum=0.0
foreach($name in @('syntax_scan_ms','identity_registration_ms','record_index_ms','field_and_method_ms',
   'constructor_decode_ms','inheritance_ms','graph_define_ms','nested_defaults_ms','unattributed_ms')){
    $x=[double]::Parse([string]$p.$name,$culture)
    if([double]::IsNaN($x) -or [double]::IsInfinity($x) -or $x -lt 0){throw "V3-23 invalid profile: $name"}
    $sum+=$x
}
$whole=[double]::Parse($p.profiled_v3_ms,$culture)
if([math]::Abs($whole-$sum) -gt 0.1){throw 'V3-23 profile phase accounting mismatch'}
if(-not $SkipAllocationProfile){
    $a=@(Import-Csv -LiteralPath $allocationPath)
    if($a.Count -ne 1 -or $a[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or
        $a[0].coverage -cne 'current_thread_global_new_only'){
        throw 'V3-23 allocation gate invalid'
    }
    foreach($name in @('v3_new_calls','v3_new_requested_bytes','v2_new_calls','v2_new_requested_bytes','hash_v322_new_calls','hash_v322_requested_bytes')){
        if([long]$a[0].$name -lt 0){throw "Negative allocation metric: $name"}
    }
}
Write-Host "V3-23 VERIFIED: median V3=$($s.median_v3_ms) ms; V2=$($s.median_v2_ms) ms; paired saved=$($s.median_paired_saved_ms) ms; wins=$($s.paired_wins)/$Pairs"
Write-Host "PairedReport=$pairsPath"
Write-Host "ProfileReport=$phasePath"
Write-Host "SummaryReport=$summaryPath"
if(-not $SkipAllocationProfile){Write-Host "AllocationReport=$allocationPath"}

Write-Host "DirectABReport=$directPath"
