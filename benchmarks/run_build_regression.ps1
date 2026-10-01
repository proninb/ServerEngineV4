param(
    [string]$BuildDirectory = "build"
)

$ErrorActionPreference = "Stop"

$repo = [IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot ".."))

$buildRoot = [IO.Path]::GetFullPath(
    (Join-Path $repo $BuildDirectory))

$exe = Join-Path $buildRoot "Release\ServerEngineV4PublishBenchmark.exe"

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Publish benchmark executable not found: $exe"
}

$fixture = Join-Path $buildRoot (
    "changed-build-regression-" +
    [Guid]::NewGuid().ToString("N"))

[IO.Directory]::CreateDirectory($fixture) | Out-Null
[IO.Directory]::CreateDirectory(
    (Join-Path $fixture "headers")) | Out-Null

$utf8 = [Text.UTF8Encoding]::new($false)

$header = Join-Path $fixture "headers\types.hpp"
$project = Join-Path $fixture "project.json"

$artifactRoot = Join-Path (Join-Path ([IO.Path]::GetDirectoryName($project)) ".serverengine") ([IO.Path]::GetFileName($project))

$initial = @'
struct T000000000 { int value; };
struct T000000001 { int value; };
'@

[IO.File]::WriteAllText(
    $header,
    $initial,
    $utf8)

$config = [ordered]@{
    version = 1
    name = "ChangedBuildRegression"
    project = @(
        [ordered]@{
            name = "types.hpp"
            type = "header"
            path = "headers/types.hpp"
        }
    )
    preprocessor = [ordered]@{
        predefines = @()
    }
}

[IO.File]::WriteAllText(
    $project,
    ($config | ConvertTo-Json -Depth 8),
    $utf8)

function Invoke-Lifecycle {
    param(
        [Parameter(Mandatory=$true)][string]$Mode,
        [Parameter(Mandatory=$true)][int]$ExpectedTypes
    )

    & $exe $Mode $project $ExpectedTypes

    if ($LASTEXITCODE -ne 0) {
        throw "$Mode failed with exit code $LASTEXITCODE"
    }
}

function Artifact-Hash {
    param(
        [Parameter(Mandatory=$true)][string]$Name
    )

    $path = Join-Path $artifactRoot $Name

    if (-not (Test-Path -LiteralPath $path)) {
        throw "Artifact missing: $path"
    }

    return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
}

Write-Host "fixture=$fixture"

Invoke-Lifecycle -Mode rebuild -ExpectedTypes 2

$before = @{
    compiled = Artifact-Hash "compiled.bin"
    source   = Artifact-Hash "source.bin"
    database = Artifact-Hash "database.bin"
    manifest = Artifact-Hash "project.manifest"
}

[IO.File]::AppendAllText(
    $header,
    "struct T000000002 { int value; };`n",
    $utf8)

Invoke-Lifecycle -Mode build -ExpectedTypes 3

$after = @{
    compiled = Artifact-Hash "compiled.bin"
    source   = Artifact-Hash "source.bin"
    database = Artifact-Hash "database.bin"
    manifest = Artifact-Hash "project.manifest"
}

if ($before.compiled -eq $after.compiled) {
    throw "compiled.bin did not change after changed BUILD"
}

if ($before.source -eq $after.source) {
    throw "source.bin did not change after changed BUILD"
}

if ($before.database -eq $after.database) {
    throw "database.bin did not change after changed BUILD"
}

if ($before.manifest -ne $after.manifest) {
    throw "project.manifest changed for content-only BUILD"
}

Invoke-Lifecycle -Mode audit -ExpectedTypes 3

foreach ($artifact in @(
    "compiled.bin",
    "source.bin",
    "database.bin",
    "project.manifest"
)) {
    foreach ($suffix in @(
        ".build-new",
        ".build-old"
    )) {
        $path = Join-Path $artifactRoot ($artifact + $suffix)

        if (Test-Path -LiteralPath $path) {
            throw "Temporary BUILD artifact remains after success: $path"
        }
    }
}

Write-Host "PASS: changed BUILD promoted Gn+1"
Write-Host "PASS: compiled/source/database changed"
Write-Host "PASS: project.manifest remained stable"
Write-Host "PASS: LOAD audit observes 3 final types"
Write-Host "PASS: no .build-new/.build-old files remain"

# Exercise sparse semantic-counter growth at and beyond 75% occupancy.
# The old capacity calculation failed at 49 unique producers in 64 slots.
$source = Join-Path $fixture 'objects.cpp'
$config.project += [ordered]@{
    name = 'objects.cpp'
    type = 'source'
    path = 'objects.cpp'
}
[IO.File]::WriteAllText($project, ($config | ConvertTo-Json -Depth 8), $utf8)
foreach ($objectCount in @(48, 49, 97, 193)) {
    $declarations = [Text.StringBuilder]::new()
    for ($index = 0; $index -lt $objectCount; ++$index) {
        [void]$declarations.Append("T000000000 object_$index;`n")
    }
    $original = $declarations.ToString()
    [IO.File]::WriteAllText($source, $original, $utf8)
    Invoke-Lifecycle -Mode rebuild -ExpectedTypes 3
    $oldCompiled = Artifact-Hash 'compiled.bin'
    [IO.File]::WriteAllText($source, $original.Replace(' object_0;', ' renamed_0;'), $utf8)
    $changedOutput = @(Invoke-Lifecycle -Mode build -ExpectedTypes 3)
    $changedOutput | Write-Output
    if (($changedOutput -join "`n") -notmatch ",objects=$objectCount,") {
        throw "Changed BUILD did not preserve $objectCount objects"
    }
    if ($oldCompiled -eq (Artifact-Hash 'compiled.bin')) {
        throw "Changed BUILD did not publish renamed object for $objectCount producers"
    }
    Invoke-Lifecycle -Mode audit -ExpectedTypes 3
    Write-Host "PASS: renamed object with $objectCount producers survives sparse table growth"
}
