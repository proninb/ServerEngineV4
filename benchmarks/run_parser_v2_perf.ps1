[CmdletBinding()]
param(
    [string]$ResultPath = "build/parser-v2-perf-01.csv",
    [int]$Runs = 7,
    [int]$Warmup = 2,
    [string]$BuildDirectory = "build"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not [System.IO.Path]::IsPathRooted($ResultPath)) {
    $ResultPath = Join-Path $root $ResultPath
}
if (Test-Path $ResultPath) {
    throw "Result file must be new: $ResultPath"
}
if ($Runs -lt 1 -or $Warmup -lt 0) {
    throw "Runs must be >= 1 and Warmup >= 0"
}
if (-not [System.IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $root $BuildDirectory
}
$exe = Join-Path $BuildDirectory "Release/ServerEngineV4ParserV2Benchmark.exe"
if (-not (Test-Path $exe)) {
    throw "Benchmark executable not found: $exe. Configure with -DSERVER_ENGINE_BUILD_BENCHMARKS=ON and build target ServerEngineV4ParserV2Benchmark"
}
$cases = @(
    @{ Shape = "fields"; Types = 2000; Members = 4 },
    @{ Shape = "fields"; Types = 100; Members = 128 },
    @{ Shape = "fields"; Types = 20; Members = 550 },
    @{ Shape = "ctor-after"; Types = 80; Members = 128 },
    @{ Shape = "ctor-before"; Types = 80; Members = 128 }
)
$lines = [System.Collections.Generic.List[string]]::new()
foreach ($case in $cases) {
    Write-Host "Profiling $($case.Shape) types=$($case.Types) members=$($case.Members)"
    $out = @(& $exe $case.Shape $case.Types $case.Members $Runs $Warmup)
    if ($LASTEXITCODE -ne 0) {
        throw "Parser V2 benchmark failed: shape=$($case.Shape) types=$($case.Types) members=$($case.Members)"
    }
    if ($out.Count -ne ($Runs + 1)) {
        throw "Unexpected benchmark output rows: $($out.Count), expected $($Runs + 1)"
    }
    if ($lines.Count -eq 0) { $lines.Add($out[0]) }
    for ($index = 1; $index -lt $out.Count; ++$index) {
        $lines.Add($out[$index])
    }
}
$parent = Split-Path -Parent $ResultPath
if ($parent -and -not (Test-Path $parent)) {
    [System.IO.Directory]::CreateDirectory($parent) | Out-Null
}
[System.IO.File]::WriteAllLines(
    $ResultPath, $lines, [System.Text.UTF8Encoding]::new($false))
Write-Host "Completed $($cases.Count) scenarios, $Runs measured trials each."
Write-Host "CSV: $ResultPath"
