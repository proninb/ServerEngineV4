# FRONTEND-PHYSICAL-11: paired include anchor scan vs original scan.
# No file checkout, no semantic/Graph/Runtime mutation; benchmark is Header-only.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectPath,
    [string]$ResultPath='build/parser-v2-include-anchors-11-ab.csv',
    [string]$BuildDirectory='build',
    [ValidateRange(1,1000)][int]$Pairs=7,
    [ValidateRange(0,1000)][int]$WarmupPairs=2,
    [switch]$Detailed,
    [switch]$CompareOld,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path -LiteralPath $ProjectPath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
    throw "Project file not found: $project"
}
if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $repo $BuildDirectory
}
if (-not [IO.Path]::IsPathRooted($ResultPath)) {
    $ResultPath = Join-Path $repo $ResultPath
}
if (Test-Path -LiteralPath $ResultPath) {
    throw "Result file must be new: $ResultPath"
}
if (-not $SkipBuild) {
    & cmake --build $BuildDirectory --config Release --parallel 1 --target `
        ServerEngineV4ParserV2ProjectBenchmark `
        ServerEngineV4ParserV2IncludeScanReference
    if ($LASTEXITCODE -ne 0) { throw 'FRONTEND-PHYSICAL-11 Release build failed' }
}
$executables = @{
    anchor = Join-Path $BuildDirectory 'Release/ServerEngineV4ParserV2ProjectBenchmark.exe'
    reference = Join-Path $BuildDirectory 'Release/ServerEngineV4ParserV2IncludeScanReference.exe'
}
foreach ($name in @('reference','anchor')) {
    if (-not (Test-Path -LiteralPath $executables[$name] -PathType Leaf)) {
        throw "Missing executable for $name : $($executables[$name])"
    }
}
if ($CompareOld) {
    foreach ($name in @('reference','anchor')) {
        & $executables[$name] compare $project
        if ($LASTEXITCODE -ne 0) { throw "OLD/V2 parity FAILED: $name" }
    }
}
$mode = if ($Detailed) { 'profile' } else { 'v2' }
$culture = [Globalization.CultureInfo]::InvariantCulture
$script:csvHeader = $null
$script:semanticCounts = @{}
$records = [Collections.Generic.List[string]]::new()
$referencePhysical = [Collections.Generic.List[double]]::new()
$anchorPhysical = [Collections.Generic.List[double]]::new()
$pairedPhysicalSaved = [Collections.Generic.List[double]]::new()
$pairedScanSaved = [Collections.Generic.List[double]]::new()

function Invoke-Trial([string]$which,[int]$pair,[int]$position,[bool]$record) {
    $exe = $executables[$which]
    $lines = @(& $exe $mode $project 1 0)
    if ($LASTEXITCODE -ne 0 -or $lines.Count -ne 2) {
        throw "Benchmark failed for $which at pair $pair (exit $LASTEXITCODE)"
    }
    $keys = @($lines[0].Split(','))
    $values = @($lines[1].Split(','))
    if ($keys.Count -ne $values.Count -or $keys[0] -ne 'mode') {
        throw "Malformed benchmark CSV: $which"
    }
    if ($null -eq $script:csvHeader) {
        $script:csvHeader = $lines[0]
    } elseif ($script:csvHeader -ne $lines[0]) {
        throw "Incompatible benchmark CSV schemas for reference/anchor"
    }
    $fields = @{}
    for ($n=0; $n -lt $keys.Count; ++$n) {
        $fields[$keys[$n]] = $values[$n]
    }
    $compareKeys = @('prepared_files','header_roots','graph_types','graph_members')
    if ($Detailed) { $compareKeys += 'include_records' }
    foreach ($key in $compareKeys) {
        if (-not $fields.ContainsKey($key)) { throw "Missing counter: $key" }
        $count = [long]::Parse($fields[$key],$culture)
        if ($script:semanticCounts.ContainsKey($key)) {
            if ($script:semanticCounts[$key] -ne $count) {
                throw "Semantic count differs for $which : $key"
            }
        } else { $script:semanticCounts[$key] = $count }
    }
    if (-not $fields.ContainsKey('physical_prepare_ms')) {
        throw 'Missing physical_prepare_ms'
    }
    $physicalMs = [double]::Parse($fields['physical_prepare_ms'],$culture)
    $scanMs = 0.0
    if ($Detailed) {
        foreach ($key in @('include_scan_ms','perf11_include_walk_ms',
                'perf11_candidate_ms','perf11_filesystem_probe_ms',
                'perf11_file_resolve_ms','perf11_ensure_file_ms',
                'perf11_candidate_calls','perf11_filesystem_probe_calls',
                'perf11_file_resolve_calls','perf11_ensure_file_calls')) {
            if (-not $fields.ContainsKey($key)) { throw "Missing profile counter: $key" }
        }
        $scanMs = [double]::Parse($fields['include_scan_ms'],$culture)
    }
    if ($record) {
        $records.Add("$pair,$position,$which,$($lines[1])")
    }
    return @{physical=$physicalMs;scan=$scanMs}
}

for ($index=1; $index -le ($Pairs+$WarmupPairs); ++$index) {
    # Reverse order for each successive pair; warmups are excluded.
    $order = if ($index % 2 -eq 1) { @('reference','anchor') }
             else { @('anchor','reference') }
    $measured = $index -gt $WarmupPairs
    $pair = $index - $WarmupPairs
    $left = Invoke-Trial $order[0] $pair 1 $measured
    $right = Invoke-Trial $order[1] $pair 2 $measured
    if (-not $measured) { continue }
    $r = if ($order[0] -eq 'reference') { $left } else { $right }
    $a = if ($order[0] -eq 'anchor') { $left } else { $right }
    $referencePhysical.Add([double]$r.physical)
    $anchorPhysical.Add([double]$a.physical)
    $pairedPhysicalSaved.Add([double]($r.physical-$a.physical))
    if ($Detailed) {
        $pairedScanSaved.Add([double]($r.scan-$a.scan))
    }
    Write-Host ('Pair {0}: physical reference {1:F3}, anchor {2:F3}, saved {3:F3} ms' -f `
        $pair,$r.physical,$a.physical,($r.physical-$a.physical))
}
if ($records.Count -ne 2*$Pairs) {
    throw 'Incomplete paired run: refusing partial CSV output'
}
$parent = Split-Path -Parent $ResultPath
if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
$rows = [Collections.Generic.List[string]]::new()
$rows.Add('pair,position,variant,' + $script:csvHeader)
foreach ($record in $records) { $rows.Add($record) }
[IO.File]::WriteAllLines($ResultPath,[string[]]$rows,
    [Text.UTF8Encoding]::new($false))
function Median([double[]]$vals) {
    $ordered = @($vals | Sort-Object)
    $mid = [int][Math]::Floor($ordered.Count / 2)
    if ($ordered.Count % 2) { return [double]$ordered[$mid] }
    return ([double]$ordered[$mid-1]+[double]$ordered[$mid])/2.0
}
Write-Host "Result: $ResultPath"
Write-Host ('Physical medians: reference {0:F3} ms, anchor {1:F3} ms, paired saved {2:F3} ms' -f `
    (Median ($referencePhysical.ToArray())),(Median ($anchorPhysical.ToArray())),
    (Median ($pairedPhysicalSaved.ToArray())))
if ($Detailed) {
    Write-Host ('Include scan paired median saved: {0:F3} ms (profiled)' -f `
        (Median ($pairedScanSaved.ToArray())))
}
Write-Host 'Other timing columns are present in the CSV; group timings are nested.'
