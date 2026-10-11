# V3-31: sampled field-type profiler. Paired A/B has no profiling clocks.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$ProjectPath,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(1,100)][int]$Pairs=7,
    [ValidateRange(0,30)][int]$WarmupPairs=2,
    [ValidateRange(1,1024)][int]$SampleStride=32,
    [switch]$SkipBuild,
    [string]$ExistingBuildDirectory,
    [switch]$SkipAllocationProfile
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot -ErrorAction Stop).Path
$ProjectPath=(Resolve-Path -LiteralPath $ProjectPath -ErrorAction Stop).Path
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$fileNames=@('v331-pairs.csv','v331-phases.csv','v331-type-samples.csv','v331-summary.csv')
if(-not $SkipAllocationProfile){$fileNames+='v331-allocations.csv'}
foreach($name in $fileNames){
    $path=Join-Path $OutputDirectory $name
    if(Test-Path -LiteralPath $path){throw "V3-31 report must be NEW: $path"}
}
$workspace=Join-Path $OutputDirectory '_experimental_v331'
$overlay=Join-Path $workspace 'engine-overlay'
$build=Join-Path $workspace 'build'
if($ExistingBuildDirectory){
    if(-not $SkipBuild){throw '-ExistingBuildDirectory requires -SkipBuild'}
    $build=(Resolve-Path -LiteralPath $ExistingBuildDirectory -ErrorAction Stop).Path
    $overlay=Join-Path (Split-Path -Path $build -Parent) 'engine-overlay'
}
$marker=Join-Path $overlay 'v331_sampled_type_profile.marker'
$perf=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV331Perf.exe'
$alloc=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV331Alloc.exe'
if(-not $SkipBuild){
    if(Test-Path -LiteralPath $overlay){throw "V3-31 requires NEW overlay: $overlay"}
    New-Item -ItemType Directory -Path $workspace -Force | Out-Null
    & python (Join-Path $PSScriptRoot 'tools/build_v331_sources.py') '--repo' $RepoRoot '--overlay' $overlay
    if($LASTEXITCODE -ne 0){throw 'V3-31 overlay generation failed'}
    & cmake -S $PSScriptRoot -B $build "-DSERVERENGINE_ROOT=$($overlay.Replace('\','/'))" '-DSERVER_ENGINE_ENABLE_IPO=OFF' '-DBUILD_TESTING=OFF'
    if($LASTEXITCODE -ne 0){throw 'V3-31 configure failed'}
    & cmake --build $build --config Release --target ServerEngineV4HeaderSyntaxV331Perf --parallel 1
    if($LASTEXITCODE -ne 0){throw 'V3-31 Release perf build failed'}
    if(-not $SkipAllocationProfile){
        & cmake --build $build --config Release --target ServerEngineV4HeaderSyntaxV331Alloc --parallel 1
        if($LASTEXITCODE -ne 0){throw 'V3-31 Release allocation build failed'}
    }
}else{
    if(-not (Test-Path -LiteralPath $marker -PathType Leaf)){
        throw 'V3-31 -SkipBuild requires an existing V3-31 overlay (use -ExistingBuildDirectory for new output location)'
    }
}
if(-not (Test-Path -LiteralPath $perf -PathType Leaf)){throw "Missing perf binary: $perf"}
if(-not $SkipAllocationProfile -and -not(Test-Path -LiteralPath $alloc -PathType Leaf)){
    throw "Missing allocation binary: $alloc"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$pairsCsvPath=Join-Path $OutputDirectory 'v331-pairs.csv'
$phasesCsvPath=Join-Path $OutputDirectory 'v331-phases.csv'
$typeCsvPath=Join-Path $OutputDirectory 'v331-type-samples.csv'
$summaryCsvPath=Join-Path $OutputDirectory 'v331-summary.csv'
& $perf $ProjectPath $pairsCsvPath $phasesCsvPath $typeCsvPath $summaryCsvPath $WarmupPairs $Pairs $SampleStride
if($LASTEXITCODE -ne 0){throw "V3-31 benchmark/parity failed: $LASTEXITCODE"}
if(-not $SkipAllocationProfile){
    $allocCsvPath=Join-Path $OutputDirectory 'v331-allocations.csv'
    & $alloc '--alloc-only' $ProjectPath $allocCsvPath
    if($LASTEXITCODE -ne 0){throw 'V3-31 allocation/parity failed'}
}
$culture=[Globalization.CultureInfo]::InvariantCulture
$paired=@(Import-Csv -LiteralPath $pairsCsvPath)
$phases=@(Import-Csv -LiteralPath $phasesCsvPath)
$types=@(Import-Csv -LiteralPath $typeCsvPath)
$summary=@(Import-Csv -LiteralPath $summaryCsvPath)
if($paired.Count -ne ($Pairs+$WarmupPairs) -or $phases.Count -ne 1 -or $types.Count -ne 1 -or $summary.Count -ne 1){
    throw 'V3-31 CSV row counts invalid'
}
foreach($r in $paired){
    if($r.header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or $r.full_publish_gate -cne 'PARTIAL'){
        throw 'V3-31 paired parity failed'
    }
    $delta=[double]::Parse([string]$r.v327_ms,$culture)-[double]::Parse([string]$r.v331_ms,$culture)
    $saved=[double]::Parse([string]$r.saved_ms,$culture)
    if([math]::Abs($delta-$saved) -gt 0.005){throw 'V3-31 paired arithmetic mismatch'}
}
if($summary[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or
   $summary[0].full_publish_gate -cne 'PARTIAL' -or
   $phases[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS' -or
   $types[0].header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS'){
    throw 'V3-31 report parity failed'
}
if([int]$summary[0].pairs -ne $Pairs -or [int]$summary[0].warmups -ne $WarmupPairs -or
   [int]$types[0].stride -ne $SampleStride){throw 'V3-31 summary/stride mismatch'}
$eligible=[long]::Parse([string]$types[0].eligible_records,$culture)
$sampled=[long]::Parse([string]$types[0].sampled_records,$culture)
$fields=[long]::Parse([string]$types[0].sampled_field_types,$culture)
if($sampled -lt 1 -or $sampled -gt $eligible -or $fields -lt 1){throw 'V3-31 sampled coverage invalid'}
foreach($key in @('base_ms_sampled','shape_ms_sampled','prepare_ms_sampled','derived_ms_sampled','builtin_ms_sampled','named_ms_sampled','fallback_ms_sampled','scope_prepare_ms_sampled','name_parse_ms_sampled','name_cache_probe_ms_sampled','scope_lookup_ms_sampled','alias_classify_ms_sampled')){
    $v=[double]::Parse([string]$types[0].$key,$culture)
    if([double]::IsNaN($v) -or [double]::IsInfinity($v) -or $v -lt 0){throw "Invalid type timer: $key"}
}
if(-not $SkipAllocationProfile){
    $a=@(Import-Csv -LiteralPath $allocCsvPath)
    if($a.Count -ne 2 -or @($a|Where-Object{$_.header_graph_gate -cne 'HEADER_GRAPH_PARITY_PASS'}).Count -ne 0){
        throw 'V3-31 allocation gate failed'
    }
}
Write-Host "V3-31 VERIFIED pairs=$Pairs wins=$($summary[0].paired_wins) median_v327=$($summary[0].median_v327_ms) median_v331=$($summary[0].median_v331_ms) paired_saved=$($summary[0].median_paired_saved_ms) HEADER_GRAPH_PARITY_PASS"
Write-Host "V3-31 TYPE SAMPLES stride=$SampleStride records=$sampled/$eligible fields=$fields"
Write-Host "PairedReport=$pairsCsvPath"
Write-Host "ProfileReport=$phasesCsvPath"
Write-Host "TypeSampleReport=$typeCsvPath"
Write-Host "SummaryReport=$summaryCsvPath"
if(-not $SkipAllocationProfile){Write-Host "AllocationReport=$allocCsvPath"}
Write-Host "Production repository unchanged: $RepoRoot"
