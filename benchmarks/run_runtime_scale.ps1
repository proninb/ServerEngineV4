param(
    [string]$Exe = "build/Release/ServerEngineV4RuntimeBenchmark.exe",
    [ValidateRange(1, 21)][int]$Runs = 3,
    [string]$ResultPath = "build/runtime-scale.csv"
)

$ErrorActionPreference = 'Stop'

$runtimeExe = (Resolve-Path -LiteralPath $Exe).Path
$runtimeResult =
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
        $ResultPath)

if (Test-Path -LiteralPath $runtimeResult) {
    throw "Result file must be new: $runtimeResult"
}

[IO.Directory]::CreateDirectory(
    (Split-Path -Parent $runtimeResult)) | Out-Null

$cases = @(
    [pscustomobject]@{ scenario = 'objects'; count = 100000 },
    [pscustomobject]@{ scenario = 'objects'; count = 1000000 },
    [pscustomobject]@{ scenario = 'links'; count = 100000 },
    [pscustomobject]@{ scenario = 'links'; count = 1000000 },
    [pscustomobject]@{ scenario = 'chain'; count = 100000 },
    [pscustomobject]@{ scenario = 'chain'; count = 1000000 }
)

$culture = [Globalization.CultureInfo]::InvariantCulture
$rows = [Collections.Generic.List[object]]::new()

foreach ($case in $cases) {
    for ($run = 1; $run -le $Runs; ++$run) {
        $line = & $runtimeExe $case.scenario $case.count

        if ($LASTEXITCODE -ne 0) {
            throw "Runtime benchmark failed: $($case.scenario) $($case.count)"
        }

        $fields = @{}

        foreach ($field in ($line -split ',')) {
            $pair = $field -split '=', 2

            if ($pair.Count -eq 2) {
                $fields[$pair[0]] = $pair[1]
            }
        }

        foreach ($required in @(
            'setup_ms',
            'layout_ms',
            'shm_create_ms',
            'materialize_ms',
            'runtime_total_ms',
            'runtime_bytes',
            'peak_ws_bytes')) {

            if (!$fields.ContainsKey($required)) {
                throw "Missing field '$required': $line"
            }
        }

        $row = [pscustomobject][ordered]@{
            scenario = $case.scenario
            count = $case.count
            run = $run
            setup_ms = [double]::Parse($fields.setup_ms, $culture)
            layout_ms = [double]::Parse($fields.layout_ms, $culture)
            shm_create_ms = [double]::Parse($fields.shm_create_ms, $culture)
            materialize_ms = [double]::Parse($fields.materialize_ms, $culture)
            runtime_total_ms = [double]::Parse($fields.runtime_total_ms, $culture)
            runtime_bytes = [long]::Parse($fields.runtime_bytes, $culture)
            mapping_bytes = [long]::Parse($fields.mapping_bytes, $culture)
            compiled_bytes = [long]::Parse($fields.compiled_bytes, $culture)
            types = [long]::Parse($fields.types, $culture)
            members = [long]::Parse($fields.members, $culture)
            objects = [long]::Parse($fields.objects, $culture)
            links = [long]::Parse($fields.links, $culture)
            peak_ws_bytes = [long]::Parse($fields.peak_ws_bytes, $culture)
        }

        $rows.Add($row)
        $rows | Export-Csv -LiteralPath $runtimeResult -NoTypeInformation

        Write-Output "run=$run,$line"
    }

    $times = @(
        $rows |
        Where-Object {
            $_.scenario -eq $case.scenario -and
            $_.count -eq $case.count
        } |
        Sort-Object runtime_total_ms |
        ForEach-Object runtime_total_ms
    )

    $middle = [int][Math]::Floor($times.Count / 2)

    if ($times.Count % 2 -eq 0) {
        $median =
            ($times[$middle - 1] +
             $times[$middle]) / 2
    }
    else {
        $median = $times[$middle]
    }

    Write-Output (
        "median,scenario={0},count={1},runtime_total_ms={2}" -f
        $case.scenario,
        $case.count,
        $median)
}

Write-Output "results=$runtimeResult"
