param(
    [string]$BuildDir = "build",
    [string]$Configuration = "Release",
    [string]$ResultPath = "",
    [int]$Iterations = 7
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ResultPath)) {
    $ResultPath = Join-Path $BuildDir "runtime-ic-snapshot-v2-production.csv"
}

$ResultPath = [System.IO.Path]::GetFullPath($ResultPath)

if (Test-Path $ResultPath) {
    throw "Result file must be new: $ResultPath"
}

$exe = Join-Path $BuildDir "$Configuration\ServerEngineV4RuntimeBenchmark.exe"

if (-not (Test-Path $exe)) {
    throw "Benchmark executable not found: $exe"
}

$cases = @(
    @{ scenario = "objects";    count = 1000 },
    @{ scenario = "objects";    count = 10000 },
    @{ scenario = "objects";    count = 100000 },
    @{ scenario = "objects";    count = 1000000 },
    @{ scenario = "many_types"; count = 100 },
    @{ scenario = "many_types"; count = 1000 },
    @{ scenario = "many_types"; count = 10000 }
)

$rows = New-Object System.Collections.Generic.List[object]
$culture = [Globalization.CultureInfo]::InvariantCulture

foreach ($case in $cases) {
    Write-Host "IC SNAP V2 production: $($case.scenario) $($case.count)"

    $line = & $exe $case.scenario $case.count $Iterations

    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark failed: $($case.scenario) $($case.count)"
    }

    if ($line -is [array]) {
        $line = $line[-1]
    }

    $v = @{}

    foreach ($field in ($line -split ",")) {
        $pair = $field -split "=", 2
        if ($pair.Count -eq 2) {
            $v[$pair[0]] = $pair[1]
        }
    }

    $rows.Add([pscustomobject]@{
        scenario                  = $v["scenario"]
        count                     = [uint64]$v["count"]
        ic_records                = [uint64]$v["ic_records"]
        ic_image_bytes            = [uint64]$v["ic_image_bytes"]
        snap_total_ms             = [double]::Parse($v["snap_total_ms"], $culture)
        snap_v2_total_ms          = [double]::Parse($v["snap_v2_total_ms"], $culture)
        snap_v2_api_ms            = [double]::Parse($v["snap_v2_api_ms"], $culture)
        snap_v2_api_records_per_s = [double]::Parse($v["snap_v2_api_records_per_s"], $culture)
        snap_v2_api_binary_equal  = [uint64]$v["snap_v2_api_binary_equal"]
        snap_v2_api_reset_ok      = [uint64]$v["snap_v2_api_reset_ok"]
        peak_ws_bytes             = [uint64]$v["peak_ws_bytes"]
    })
}

$rows | Export-Csv -NoTypeInformation -Encoding UTF8 $ResultPath

Write-Host ""
Write-Host "Saved: $ResultPath"
$rows | Format-Table -AutoSize
