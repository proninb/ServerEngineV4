[CmdletBinding()]
param(
    [string]$ResultPath = "build\msvc-class-abi-oracle-v1b3.csv",
    [string]$Generator = ""
)

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not [System.IO.Path]::IsPathRooted($ResultPath)) {
    $ResultPath = Join-Path $Root $ResultPath
}

if (Test-Path $ResultPath) {
    throw "Result file already exists: $ResultPath"
}

if ([string]::IsNullOrWhiteSpace($Generator)) {
    $MainCache = Join-Path $Root "build\CMakeCache.txt"

    if (Test-Path $MainCache) {
        $GeneratorLine =
            Get-Content $MainCache |
            Where-Object {
                $_ -like "CMAKE_GENERATOR:INTERNAL=Visual Studio *"
            } |
            Select-Object -First 1

        if ($GeneratorLine) {
            $Generator =
                $GeneratorLine.Substring(
                    "CMAKE_GENERATOR:INTERNAL=".Length)
        }
    }
}

if ([string]::IsNullOrWhiteSpace($Generator)) {
    $Help = & cmake --help

    $GeneratorLine =
        $Help |
        Where-Object {
            $_ -match "^\s*\*?\s*Visual Studio [0-9]+ [0-9]{4}\s*="
        } |
        Select-Object -First 1

    if (-not $GeneratorLine) {
        throw "Cannot find a Visual Studio CMake generator"
    }

    $CleanGeneratorLine =
        ($GeneratorLine -replace "^\s*\*?\s*", "")

    $Generator =
        ($CleanGeneratorLine.Split("=")[0]).Trim()
}

if ($Generator -notlike "Visual Studio *") {
    throw "MSVC class ABI oracle requires a Visual Studio generator; got '$Generator'"
}

$Combined = [System.Collections.Generic.List[string]]::new()

$Targets = @(
    @{
        Architecture = "x64"
        Build = "build-oracle-x64"
    },
    @{
        Architecture = "Win32"
        Build = "build-oracle-win32"
    }
)

foreach ($Target in $Targets) {
    $BuildPath =
        Join-Path $Root $Target.Build

    & cmake `
        -S $Root `
        -B $BuildPath `
        -G $Generator `
        -A $Target.Architecture `
        -DBUILD_TESTING=OFF `
        -DSERVER_ENGINE_BUILD_BENCHMARKS=OFF `
        -DSERVER_ENGINE_ENABLE_IPO=OFF

    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for $($Target.Architecture)"
    }

    & cmake `
        --build $BuildPath `
        --config Release `
        --target ServerEngineV4MsvcClassAbiOracle

    if ($LASTEXITCODE -ne 0) {
        throw "Oracle build failed for $($Target.Architecture)"
    }

    $Executable =
        Join-Path `
            $BuildPath `
            "Release\ServerEngineV4MsvcClassAbiOracle.exe"

    if (-not (Test-Path $Executable)) {
        throw "Oracle executable not found: $Executable"
    }

    $Output = @(& $Executable)

    if ($LASTEXITCODE -ne 0) {
        throw "Oracle execution failed for $($Target.Architecture)"
    }

    if ($Output.Count -ne 116) {
        throw (
            "Unexpected oracle row count for " +
            "$($Target.Architecture): expected 116 lines, got " +
            $Output.Count
        )
    }

    if ($Combined.Count -eq 0) {
        foreach ($Line in $Output) {
            $Combined.Add($Line)
        }
    }
    else {
        for ($Index = 1; $Index -lt $Output.Count; ++$Index) {
            $Combined.Add($Output[$Index])
        }
    }
}

if ($Combined.Count -ne 231) {
    throw "Unexpected combined oracle row count: $($Combined.Count)"
}

$Records =
    $Combined |
    ConvertFrom-Csv

foreach ($Architecture in @("x64", "Win32")) {
    $Count =
        @(
            $Records |
            Where-Object {
                $_.arch -eq $Architecture
            }
        ).Count

    if ($Count -ne 115) {
        throw "Expected 115 $Architecture oracle rows, got $Count"
    }
}

$Parent =
    Split-Path -Parent $ResultPath

if ($Parent) {
    [System.IO.Directory]::CreateDirectory($Parent) |
        Out-Null
}

[System.IO.File]::WriteAllLines(
    $ResultPath,
    $Combined,
    [System.Text.UTF8Encoding]::new($false)
)

Write-Host ""
Write-Host "MSVC class ABI oracle complete"
Write-Host "Generator: $Generator"
Write-Host "Rows:      $($Records.Count)"
Write-Host "Result:    $ResultPath"
