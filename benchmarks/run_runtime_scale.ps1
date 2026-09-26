param(
    [string]$Exe = "build/Release/ServerEngineV4RuntimeBenchmark.exe",
    [ValidateRange(1, 21)][int]$Runs = 3,
    [string]$ResultPath = "build/runtime-scale-subobject-v1.csv"
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
    [pscustomobject]@{ scenario = 'indexed_links'; count = 100000 },
    [pscustomobject]@{ scenario = 'indexed_links'; count = 1000000 },
    [pscustomobject]@{ scenario = 'chain'; count = 100000 },
    [pscustomobject]@{ scenario = 'chain'; count = 1000000 },
    [pscustomobject]@{ scenario = 'many_types'; count = 1000 },
    [pscustomobject]@{ scenario = 'many_types'; count = 10000 }
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
            'endpoint_paths',
            'endpoint_path_steps',
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
            endpoint_paths = [long]::Parse($fields.endpoint_paths, $culture)
            endpoint_path_steps = [long]::Parse($fields.endpoint_path_steps, $culture)
            peak_ws_bytes = [long]::Parse($fields.peak_ws_bytes, $culture)
        }

        $rows.Add($row)
        $rows | Export-Csv -LiteralPath $runtimeResult -NoTypeInformation

        Write-Output "run=$run,$line"
    }

    $caseRows = @(
        $rows |
        Where-Object {
            $_.scenario -eq $case.scenario -and
            $_.count -eq $case.count
        }
    )

    $runtimeTimes = @(
        $caseRows |
        Sort-Object runtime_total_ms |
        ForEach-Object runtime_total_ms
    )

    $materializeTimes = @(
        $caseRows |
        Sort-Object materialize_ms |
        ForEach-Object materialize_ms
    )

    $middle = [int][Math]::Floor(
        $runtimeTimes.Count / 2)

    if ($runtimeTimes.Count % 2 -eq 0) {
        $runtimeMedian =
            ($runtimeTimes[$middle - 1] +
             $runtimeTimes[$middle]) / 2

        $materializeMedian =
            ($materializeTimes[$middle - 1] +
             $materializeTimes[$middle]) / 2
    }
    else {
        $runtimeMedian =
            $runtimeTimes[$middle]

        $materializeMedian =
            $materializeTimes[$middle]
    }

    $last = $caseRows[-1]

    Write-Output (
        "median,scenario={0},count={1},materialize_ms={2},runtime_total_ms={3},compiled_bytes={4},endpoint_paths={5},endpoint_path_steps={6},peak_ws_bytes={7}" -f
        $case.scenario,
        $case.count,
        $materializeMedian,
        $runtimeMedian,
        $last.compiled_bytes,
        $last.endpoint_paths,
        $last.endpoint_path_steps,
        $last.peak_ws_bytes)
}

Write-Output "results=$runtimeResult"
