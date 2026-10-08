param(
    [string]$ResultPath = "build/graph-resolved-profile.csv",
    [int]$Runs = 5,
    [UInt64]$LookupIterations = 1000000,
    [int[]]$MemberCounts = @(8, 16, 32, 64, 128, 256, 550),
    [string]$ProjectPath = "",
    [UInt64]$ExpectedTypes = 0
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$result = Join-Path $root $ResultPath
$exe = Join-Path $root "build/Release/ServerEngineV4PublishBenchmark.exe"

if (Test-Path $result) {
    throw "Result file must be new: $result"
}

if (-not (Test-Path $exe)) {
    throw "Benchmark executable not found: $exe"
}

$directory = Split-Path -Parent $result
if ($directory -and -not (Test-Path $directory)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}

foreach ($members in $MemberCounts) {
    if ($members -le 0) {
        throw "MemberCounts must contain only positive values"
    }

    for ($run = 1; $run -le $Runs; ++$run) {
        $line = & $exe graph-resolved-profile $members $LookupIterations

        if ($LASTEXITCODE -ne 0) {
            throw "graph-resolved-profile failed: members=$members run=$run"
        }

        "kind=micro,run=$run,$line" | Add-Content -Path $result
        Write-Host $line
    }
}

if ($ProjectPath) {
    if ($ExpectedTypes -eq 0) {
        throw "ExpectedTypes must be provided with ProjectPath"
    }

    $project = Resolve-Path $ProjectPath

    for ($run = 1; $run -le $Runs; ++$run) {
        $line = & $exe publish-stage $project $ExpectedTypes

        if ($LASTEXITCODE -ne 0) {
            throw "publish-stage failed: run=$run"
        }

        "kind=publish,run=$run,$line" | Add-Content -Path $result
        Write-Host $line
    }
}

Write-Host "Result: $result"
