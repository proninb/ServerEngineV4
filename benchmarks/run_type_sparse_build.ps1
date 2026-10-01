param(
    [string]$BuildDirectory = 'build',
    [int[]]$TypeCounts = @(1000,10000,100000),
    [ValidateRange(1,4096)][int]$BulkFileCount = 128,
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..'))

$exe = Join-Path `
    (Join-Path $repo $BuildDirectory) `
    'Release/ServerEngineV4PublishBenchmark.exe'

if (!(Test-Path -LiteralPath $exe)) {
    throw "Missing executable: $exe"
}

if (!$OutputDirectory) {
    $OutputDirectory = Join-Path `
        $repo `
        ('build/type-sparse-' +
         [Guid]::NewGuid().ToString('N'))
}

$root = [IO.Path]::GetFullPath($OutputDirectory)

if (Test-Path -LiteralPath $root) {
    throw "Output directory must be new: $root"
}

[IO.Directory]::CreateDirectory($root) | Out-Null

$snapshot = Join-Path `
    $root `
    'ServerEngineV4PublishBenchmark.exe'

Copy-Item `
    -LiteralPath $exe `
    -Destination $snapshot

$exe = $snapshot
$utf8 = [Text.UTF8Encoding]::new($false)

$rows = [Collections.Generic.List[object]]::new()

function Invoke-Benchmark {
    param(
        [string]$Mode,
        [string]$Project,
        [int]$Types
    )

    $output = & $exe $Mode $Project $Types

    if ($LASTEXITCODE -ne 0) {
        throw "$Mode failed for $Types types"
    }

    $metrics = [ordered]@{}

    foreach ($pair in ($output.Trim() -split ',')) {
        $kv = $pair -split '=', 2

        if ($kv.Count -eq 2) {
            $metrics[$kv[0]] = $kv[1]
        }
    }

    return $metrics
}

foreach ($types in $TypeCounts) {
    $fixture = Join-Path $root "types_$types"

    & (Join-Path $PSScriptRoot 'new_type_sparse_fixture.ps1') `
        -OutputDirectory $fixture `
        -TypeCount $types `
        -BulkFileCount $BulkFileCount

    $project = Join-Path $fixture 'project.json'

    $rebuild = Invoke-Benchmark `
        -Mode 'rebuild' `
        -Project $project `
        -Types $types

    $changed = Join-Path $fixture 'changed.hpp'

    [IO.File]::WriteAllText(
        $changed,
        "struct T000000000 { int value; int changed; };`n",
        $utf8)

    $build = Invoke-Benchmark `
        -Mode 'build' `
        -Project $project `
        -Types $types

    if ($build['changed_files'] -ne '1' -or
        $build['affected_roots'] -ne '1' -or
        $build['replay_roots'] -ne '1') {

        throw (
            "Sparse semantic gate failed for $types types: " +
            "changed_files=$($build['changed_files']), " +
            "affected_roots=$($build['affected_roots']), " +
            "replay_roots=$($build['replay_roots'])")
    }

    if ($build['rebuild_fallback'] -ne '0') {
        throw "BUILD unexpectedly fell back to REBUILD for $types types"
    }

    $row = [ordered]@{
        types = $types
        rebuild_total_ms = $rebuild['total_ms']
        build_total_ms = $build['total_ms']
        sparse_ms = $build['sparse_ms']
        dense_projection_ms = $build['dense_projection_ms']
        artifact_materialization_ms = $build['artifact_materialization_ms']
        runtime_publication_ms = $build['runtime_publication_ms']
        promotion_ms = $build['promotion_ms']
        changed_files = $build['changed_files']
        affected_files = $build['affected_files']
        affected_roots = $build['affected_roots']
        invalidated_roots = $build['invalidated_roots']
        replay_roots = $build['replay_roots']
        retire_types = $build['retire_types']
        clear_type_definitions = $build['clear_type_definitions']
        graph_type_patches = $build['graph_type_patches']
        graph_appended_types = $build['graph_appended_types']
        compiled_bytes = $build['compiled_bytes']
    }

    $rows.Add([pscustomobject]$row)

    $rows |
        Export-Csv `
            (Join-Path $root 'results.csv') `
            -NoTypeInformation

    Write-Host ($row | ConvertTo-Json -Compress)
}

Write-Host 'PASS: TYPE-SPARSE-BUILD-V1 semantic gate'
Write-Host "Results: $(Join-Path $root 'results.csv')"
