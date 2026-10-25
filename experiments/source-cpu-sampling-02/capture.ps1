param([int]$Blocks = 8, [int]$Repeats = 6,
      [string]$Executable = '')
$ErrorActionPreference = 'Stop'
$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = if ($Executable) { [System.IO.Path]::GetFullPath($Executable) } else {
    Join-Path $repo 'build/source-cpu-sampling-02/frozen-current-v2/ServerEngineV4PublishBenchmark.exe'
}
$fixture = Join-Path $repo 'build/publish-subsecond-01/fixture/project.json'
$destination = Join-Path $repo ('build/source-cpu-sampling-02/capture-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$python = (Get-Command python -ErrorAction Stop).Source
& $python (Join-Path $PSScriptRoot 'collect.py') --exe $exe --project $fixture --out $destination --blocks $Blocks --repeats $Repeats --capture
if ($LASTEXITCODE -ne 0) { throw "CPU capture failed ($LASTEXITCODE). Inspect $destination/manifest.json" }
Write-Output "Saved capture: $destination"
