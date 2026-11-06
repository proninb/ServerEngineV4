param([int]$Warmup = 1000, [int]$Samples = 10000)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$source = Join-Path $root 'control_probe.cpp'
$exe = Join-Path $root 'control_probe.exe'
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Run from x64 Native Tools Command Prompt for VS (cl.exe must be in PATH).'
}
& cl.exe /nologo /O2 /std:c++17 /EHsc /W4 /Fe:$exe $source
if ($LASTEXITCODE -ne 0) { throw 'C++ build failed' }
foreach ($count in @(1, 2, 4, 8, 16)) {
    & $exe $count $Warmup $Samples
    if ($LASTEXITCODE -ne 0) { throw "Probe failed: $count tasks" }
}
