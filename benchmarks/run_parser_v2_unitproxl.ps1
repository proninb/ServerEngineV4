[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectPath,
    [string]$ResultPath = 'build/parser-v2-unitproxl.csv',
    [ValidateSet('v2','old')]
    [string]$Mode = 'v2',
    [int]$Runs = 7,
    [int]$Warmup = 2,
    [switch]$CompareOld,
    [string]$BuildDirectory = 'build'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ($Runs -lt 1 -or $Runs -gt 1000 -or $Warmup -lt 0 -or $Warmup -gt 1000) {
    throw 'Runs must be 1..1000 and Warmup must be 0..1000'
}
$project = (Resolve-Path -LiteralPath $ProjectPath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
    throw "Project file not found: $project"
}
if (-not [System.IO.Path]::IsPathRooted($ResultPath)) {
    $ResultPath = Join-Path $root $ResultPath
}
if (Test-Path -LiteralPath $ResultPath) {
    throw "Result file must be new: $ResultPath"
}
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $root $BuildDirectory
}
$exe = Join-Path $BuildDirectory 'Release/ServerEngineV4ParserV2ProjectBenchmark.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Benchmark executable not found: $exe"
}
$lines = @(& $exe $Mode $project $Runs $Warmup)
if ($LASTEXITCODE -ne 0) {
    throw "PARSER-V2-PERF-02 failed in mode $Mode; no CSV was written"
}
if ($lines.Count -ne ($Runs + 1)) {
    throw "Expected $($Runs + 1) CSV lines; received $($lines.Count)"
}
$expectedHeader = 'mode,run,manifest_ms,physical_prepare_ms,preprocessor_start_ms,'
if (-not $lines[0].StartsWith($expectedHeader)) {
    throw 'Unexpected benchmark CSV schema'
}
$parent = Split-Path -Parent $ResultPath
if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
}
[System.IO.File]::WriteAllLines(
    $ResultPath, [string[]]$lines,
    [System.Text.UTF8Encoding]::new($false))
Write-Host "Measured $Runs real-project Header-only runs: $ResultPath"
Write-Host "Mode: $Mode; Project: $project"
if ($CompareOld) {
    & $exe compare $project
    if ($LASTEXITCODE -ne 0) {
        throw 'OLD/V2 Header Graph parity check failed; see diagnostics above'
    }
}
