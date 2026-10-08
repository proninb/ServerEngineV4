# FRONTEND-PHYSICAL-12: ordered paired A/B, anchor scanning for both.
# Positive include memo vs original probe resolution (benchmark-only flag).
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectPath,
    [string]$ResultPath='build/parser-v2-include-resolution-12-ab.csv',
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
        ServerEngineV4ParserV2IncludeResolveReference
    if ($LASTEXITCODE -ne 0) { throw 'FRONTEND-PHYSICAL-12 build failed' }
}
$executables = @{
    candidate = Join-Path $BuildDirectory 'Release/ServerEngineV4ParserV2ProjectBenchmark.exe'
    reference = Join-Path $BuildDirectory 'Release/ServerEngineV4ParserV2IncludeResolveReference.exe'
}
foreach ($name in @('reference','candidate')) {
    if (-not (Test-Path -LiteralPath $executables[$name] -PathType Leaf)) {
        throw "Missing executable for $name : $($executables[$name])"
    }
}
if ($CompareOld) {
    foreach ($name in @('reference','candidate')) {
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
$candidatePhysical = [Collections.Generic.List[double]]::new()
$pairedPhysicalSaved = [Collections.Generic.List[double]]::new()
$pairedScanSaved = [Collections.Generic.List[double]]::new()
$script:hitsCandidate = 0L
$script:missesCandidate = 0L

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
        throw 'Benchmark CSV schemas differ'
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
    $physicalMs = [double]::Parse($fields['physical_prepare_ms'],$culture)
    $scanMs = 0.0
    if ($Detailed) {
        foreach ($key in @('include_scan_ms','perf11_include_walk_ms',
                'perf11_candidate_ms','perf11_filesystem_probe_ms',
                'perf11_file_resolve_ms','perf11_ensure_file_ms',
                'perf11_candidate_calls','perf11_filesystem_probe_calls',
                'perf11_file_resolve_calls','perf11_ensure_file_calls',
                'perf12_cache_lookups','perf12_cache_misses',
                'perf12_positive_cache_hits','perf12_positive_cache_entries')) {
            if (-not $fields.ContainsKey($key)) { throw "Missing profile field: $key" }
        }
        $scanMs = [double]::Parse($fields['include_scan_ms'],$culture)
        $lookups=[long]::Parse($fields['perf12_cache_lookups'],$culture)
        $misses=[long]::Parse($fields['perf12_cache_misses'],$culture)
        $hits=[long]::Parse($fields['perf12_positive_cache_hits'],$culture)
        if ($lookups -ne ($misses+$hits)) { throw "Invalid cache accounting: $which" }
        if ($which -eq 'reference' -and $hits -ne 0) {
            throw 'Reference must not use positive cache'
        }
        if ($which -eq 'candidate') {
            $script:hitsCandidate=$hits
            $script:missesCandidate=$misses
        }
    }
    if ($record) { $records.Add("$pair,$position,$which,$($lines[1])") }
    return @{physical=$physicalMs;scan=$scanMs}
}

for ($index=1; $index -le ($Pairs+$WarmupPairs); ++$index) {
    $order = if ($index % 2 -eq 1) { @('reference','candidate') }
             else { @('candidate','reference') }
    $measured = $index -gt $WarmupPairs
    $pair = $index - $WarmupPairs
    $left = Invoke-Trial $order[0] $pair 1 $measured
    $right = Invoke-Trial $order[1] $pair 2 $measured
    if (-not $measured) { continue }
    $r = if ($order[0] -eq 'reference') { $left } else { $right }
    $c = if ($order[0] -eq 'candidate') { $left } else { $right }
    $referencePhysical.Add([double]$r.physical)
    $candidatePhysical.Add([double]$c.physical)
    $pairedPhysicalSaved.Add([double]($r.physical-$c.physical))
    if ($Detailed) { $pairedScanSaved.Add([double]($r.scan-$c.scan)) }
    Write-Host ('Pair {0}: physical reference {1:F3}, cache {2:F3}, saved {3:F3} ms' -f `
        $pair,$r.physical,$c.physical,($r.physical-$c.physical))
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
Write-Host ('Physical medians: reference {0:F3} ms, cache {1:F3} ms, paired saved {2:F3} ms' -f `
    (Median ($referencePhysical.ToArray())),(Median ($candidatePhysical.ToArray())),
    (Median ($pairedPhysicalSaved.ToArray())))
if ($Detailed) {
    Write-Host ('Include scan paired median saved: {0:F3} ms (profiled)' -f `
        (Median ($pairedScanSaved.ToArray())))
    Write-Host "Positive cache: hits=$script:hitsCandidate misses=$script:missesCandidate"
}
Write-Host 'Candidate timers are nested; never sum them as independent wall time.'
