param(
    [string]$Exe = "build/Release/ServerEngineV4RuntimeBenchmark.exe",
    [ValidateRange(1, 21)][int]$Runs = 3,
    [string]$ResultPath = "build/runtime-v2-scale.csv"
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

$culture =
    [Globalization.CultureInfo]::InvariantCulture

$rows =
    [Collections.Generic.List[object]]::new()

$required = @(
    'benchmark',
    'setup_ms',
    'layout_ms',
    'prepare_ms',
    'shm_create_ms',
    'canonical_ms',
    'links_mark_ms',
    'objects_ms',
    'links_ms',
    'initializations_ms',
    'materialize_ms',
    'runtime_total_ms',
    'runtime_bytes',
    'mapping_bytes',
    'compiled_bytes',
    'construction_bytes',
    'type_apis',
    'object_child_visits',
    'types',
    'members',
    'objects',
    'links',
    'endpoint_paths',
    'endpoint_path_steps',
    'peak_ws_bytes'
)

function Get-Median([object[]]$Values) {
    $ordered = @(
        $Values |
        Sort-Object
    )

    $middle =
        [int][Math]::Floor(
            $ordered.Count / 2)

    if ($ordered.Count % 2 -eq 0) {
        return (
            $ordered[$middle - 1] +
            $ordered[$middle]) / 2
    }

    return $ordered[$middle]
}

foreach ($case in $cases) {
    for ($run = 1; $run -le $Runs; ++$run) {
        $line =
            & $runtimeExe `
                $case.scenario `
                $case.count `
                v2

        if ($LASTEXITCODE -ne 0) {
            throw (
                "Runtime V2 benchmark failed: {0} {1}" -f
                $case.scenario,
                $case.count)
        }

        $fields = @{}

        foreach ($field in ($line -split ',')) {
            $pair = $field -split '=', 2

            if ($pair.Count -eq 2) {
                $fields[$pair[0]] =
                    $pair[1]
            }
        }

        foreach ($name in $required) {
            if (!$fields.ContainsKey($name)) {
                throw "Missing field '$name': $line"
            }
        }

        if ($fields.benchmark -ne 'v2_kernel') {
            throw "Unexpected benchmark mode: $line"
        }

        $row =
            [pscustomobject][ordered]@{
                scenario = $case.scenario
                count = $case.count
                run = $run
                setup_ms = [double]::Parse(
                    $fields.setup_ms, $culture)
                layout_ms = [double]::Parse(
                    $fields.layout_ms, $culture)
                prepare_ms = [double]::Parse(
                    $fields.prepare_ms, $culture)
                shm_create_ms = [double]::Parse(
                    $fields.shm_create_ms, $culture)
                canonical_ms = [double]::Parse(
                    $fields.canonical_ms, $culture)
                links_mark_ms = [double]::Parse(
                    $fields.links_mark_ms, $culture)
                objects_ms = [double]::Parse(
                    $fields.objects_ms, $culture)
                links_ms = [double]::Parse(
                    $fields.links_ms, $culture)
                initializations_ms = [double]::Parse(
                    $fields.initializations_ms, $culture)
                materialize_ms = [double]::Parse(
                    $fields.materialize_ms, $culture)
                runtime_total_ms = [double]::Parse(
                    $fields.runtime_total_ms, $culture)
                runtime_bytes = [long]::Parse(
                    $fields.runtime_bytes, $culture)
                mapping_bytes = [long]::Parse(
                    $fields.mapping_bytes, $culture)
                compiled_bytes = [long]::Parse(
                    $fields.compiled_bytes, $culture)
                construction_bytes = [long]::Parse(
                    $fields.construction_bytes, $culture)
                type_apis = [long]::Parse(
                    $fields.type_apis, $culture)
                object_child_visits = [long]::Parse(
                    $fields.object_child_visits, $culture)
                types = [long]::Parse(
                    $fields.types, $culture)
                members = [long]::Parse(
                    $fields.members, $culture)
                objects = [long]::Parse(
                    $fields.objects, $culture)
                links = [long]::Parse(
                    $fields.links, $culture)
                endpoint_paths = [long]::Parse(
                    $fields.endpoint_paths, $culture)
                endpoint_path_steps = [long]::Parse(
                    $fields.endpoint_path_steps, $culture)
                peak_ws_bytes = [long]::Parse(
                    $fields.peak_ws_bytes, $culture)
            }

        $rows.Add($row)

        $rows |
            Export-Csv `
                -LiteralPath $runtimeResult `
                -NoTypeInformation

        Write-Output "run=$run,$line"
    }

    $caseRows = @(
        $rows |
        Where-Object {
            $_.scenario -eq $case.scenario -and
            $_.count -eq $case.count
        }
    )

    $last = $caseRows[-1]

    Write-Output (
        "median,scenario={0},count={1},layout_ms={2},prepare_ms={3},objects_ms={4},links_ms={5},materialize_ms={6},runtime_total_ms={7},construction_bytes={8},object_child_visits={9},peak_ws_bytes={10}" -f
        $case.scenario,
        $case.count,
        (Get-Median @($caseRows.layout_ms)),
        (Get-Median @($caseRows.prepare_ms)),
        (Get-Median @($caseRows.objects_ms)),
        (Get-Median @($caseRows.links_ms)),
        (Get-Median @($caseRows.materialize_ms)),
        (Get-Median @($caseRows.runtime_total_ms)),
        $last.construction_bytes,
        $last.object_child_visits,
        $last.peak_ws_bytes)
}

Write-Output "results=$runtimeResult"
