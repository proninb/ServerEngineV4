# PARSER-V2-CURSOR-AB-DIRECTIVE-10: paired crossover A/B (same source tree).
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$ProjectPath,
    [string]$ResultPath = 'build/parser-v2-decode09-paired-ab.csv',
    [string]$BuildDirectory = 'build',
    [ValidateRange(1,1000)] [int]$Pairs = 7,
    [ValidateRange(0,1000)] [int]$WarmupPairs = 2,
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
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $repo $BuildDirectory
}
if (-not [System.IO.Path]::IsPathRooted($ResultPath)) {
    $ResultPath = Join-Path $repo $ResultPath
}
if (Test-Path -LiteralPath $ResultPath) {
    throw "Result file must be new: $ResultPath"
}
if (-not $SkipBuild) {
    & cmake --build $BuildDirectory --config Release --parallel 1 --target `
        ServerEngineV4ParserV2ProjectBenchmark `
        ServerEngineV4ParserV2ProjectBenchmarkReference
    if ($LASTEXITCODE -ne 0) { throw 'Paired benchmark Release build failed' }
}
$executables = @{
    decode09 = Join-Path $BuildDirectory 'Release/ServerEngineV4ParserV2ProjectBenchmark.exe'
    reference = Join-Path $BuildDirectory 'Release/ServerEngineV4ParserV2ProjectBenchmarkReference.exe'
}
foreach ($name in @('reference','decode09')) {
    if (-not (Test-Path -LiteralPath $executables[$name] -PathType Leaf)) {
        throw "Executable missing for $name : $($executables[$name])"
    }
}
# Validate both images against the same OLD semantic oracle before measuring.
if ($CompareOld) {
    foreach ($name in @('reference','decode09')) {
        & $executables[$name] compare $project
        if ($LASTEXITCODE -ne 0) { throw "OLD/V2 parity failed for $name" }
    }
}
$expectedHeader = $null
$records = [System.Collections.Generic.List[string]]::new()
$referenceMs = [System.Collections.Generic.List[double]]::new()
$decodeMs = [System.Collections.Generic.List[double]]::new()
$diffs = [System.Collections.Generic.List[double]]::new()
$culture = [System.Globalization.CultureInfo]::InvariantCulture
function Invoke-Trial([string]$which, [int]$pair, [int]$position, [bool]$record) {
    $exe = $executables[$which]
    $lines = @(& $exe v2 $project 1 0)
    if ($LASTEXITCODE -ne 0 -or $lines.Count -ne 2) {
        throw "Failed $which pair $pair (code $LASTEXITCODE, CSV lines $($lines.Count))"
    }
    if (-not $lines[0].StartsWith('mode,run,manifest_ms,physical_prepare_ms,')) {
        throw "Unexpected CSV header from $which"
    }
    if ($null -eq $script:expectedHeader) {
        $script:expectedHeader = $lines[0]
    } elseif ($script:expectedHeader -ne $lines[0]) {
        throw "Different CSV schemas for reference/decode09"
    }
    $header = $lines[0].Split(',')
    $values = $lines[1].Split(',')
    if ($values.Count -ne $header.Count) { throw "Invalid benchmark row for $which" }
    foreach ($name in @('prepared_files','graph_types','graph_members','header_roots')) {
        $index = [Array]::IndexOf($header, $name)
        if ($index -lt 0) { throw "Missing required column: $name" }
        $count = [long]::Parse($values[$index], $culture)
        if ($null -eq $script:semanticCounts) {
            $script:semanticCounts = @{}
        }
        if ($script:semanticCounts.ContainsKey($name)) {
            if ($script:semanticCounts[$name] -ne $count) {
                throw "Semantic size mismatch $name for $which pair $pair"
            }
        } else { $script:semanticCounts[$name] = $count }
    }
    $msIndex = [Array]::IndexOf($header, 'header_semantic_parse_graph_ms')
    if ($msIndex -lt 0) { throw 'Missing semantic time column' }
    $value = [double]::Parse($values[$msIndex], $culture)
    if ($record) {
        $records.Add("$pair,$position,$which,$($lines[1])")
        if ($which -eq 'reference') { $referenceMs.Add($value) }
        else { $decodeMs.Add($value) }
    }
    return $value
}
$script:semanticCounts = $null
$totalPairs = $WarmupPairs + $Pairs
for ($i = 1; $i -le $totalPairs; ++$i) {
    # AB/BA crossover: order reverses on alternate pairs, damping drift.
    $order = if ($i % 2 -eq 1) { @('reference','decode09') }
             else { @('decode09','reference') }
    $measured = $i -gt $WarmupPairs
    $pair = $i - $WarmupPairs
    $a = Invoke-Trial $order[0] $pair 1 $measured
    $b = Invoke-Trial $order[1] $pair 2 $measured
    if ($measured) {
        $r = if ($order[0] -eq 'reference') { $a } else { $b }
        $d = if ($order[0] -eq 'decode09') { $a } else { $b }
        $diffs.Add($r - $d)
        Write-Host ("Pair {0}: reference {1:F3} ms; decode09 {2:F3} ms; saved {3:F3} ms" -f $pair,$r,$d,($r-$d))
    }
}
if ($referenceMs.Count -ne $Pairs -or $decodeMs.Count -ne $Pairs -or
    $diffs.Count -ne $Pairs -or $records.Count -ne 2 * $Pairs) {
    throw 'Incomplete A/B measurements; no CSV written'
}
$parent = Split-Path -Parent $ResultPath
if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
$rows = [System.Collections.Generic.List[string]]::new()
$rows.Add('pair,position,variant,' + $expectedHeader)
foreach ($line in $records) { $rows.Add($line) }
[System.IO.File]::WriteAllLines(
    $ResultPath, [string[]]$rows, [System.Text.UTF8Encoding]::new($false))
function Median([double[]]$values) {
    $sorted = @($values | Sort-Object)
    $mid = [int][Math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 1) { return [double]$sorted[$mid] }
    return ([double]$sorted[$mid - 1] + [double]$sorted[$mid]) / 2.0
}
$referenceMedian = Median ($referenceMs.ToArray())
$decodeMedian = Median ($decodeMs.ToArray())
$deltaMedian = Median ($diffs.ToArray())
Write-Host "A/B results: $ResultPath"
Write-Host ("Semantic medians: reference {0:F3} ms; decode09 {1:F3} ms; paired median saved {2:F3} ms" -f `
    $referenceMedian,$decodeMedian,$deltaMedian)
Write-Host "Same project, same process architecture, alternating AB/BA process runs"
