param(
    [string]$BuildDirectory = 'build',
    [int[]]$TypeCounts = @(1000,10000,100000),
    [ValidateRange(1,1000)][int]$ObjectsPerType = 10,
    [ValidateRange(1,128)][int]$MemoryLimitGiB = 10,
    [ValidateRange(1,86400)][int]$TimeoutSeconds = 1800,
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$exe = Join-Path (Join-Path $repo $BuildDirectory) 'Release/ServerEngineV4PublishBenchmark.exe'
if (!(Test-Path -LiteralPath $exe)) { throw "Missing executable: $exe" }
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repo ('build/object-scale-' + [Guid]::NewGuid().ToString('N')) }
$root = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $root) { throw "Output directory must be new: $root" }
[IO.Directory]::CreateDirectory($root) | Out-Null
$snapshot = Join-Path $root 'ServerEngineV4PublishBenchmark.exe'
Copy-Item -LiteralPath $exe -Destination $snapshot
$exe = $snapshot
$rows = [Collections.Generic.List[object]]::new()
@{commit=(& git -C $repo rev-parse HEAD); executable=$exe; executable_sha256=(Get-FileHash $exe).Hash; memory_limit_gib=$MemoryLimitGiB; timeout_seconds=$TimeoutSeconds; objects_per_type=$ObjectsPerType; started_utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $root 'environment.json')
Write-Host "Results: $root"
foreach ($types in $TypeCounts) {
    $fixture = Join-Path $root "types_$types"
    & (Join-Path $PSScriptRoot 'new_object_scale_fixture.ps1') -OutputDirectory $fixture -TypeCount $types -ObjectsPerType $ObjectsPerType
    $project = Join-Path $fixture 'project.json'
    $lineage = $false
    foreach ($label in @('publish','rebuild','build_unchanged','audit','build_changed','audit_changed')) {
        $mode = $label.Split('_')[0]
        $row = [ordered]@{types=$types; objects=([long]$types*$ObjectsPerType); operation=$label; status=''; total_ms=''; peak_ws_bytes=''; compiled_bytes=''; monitored_private_peak=0; monitored_ws_peak=0; process_elapsed_ms=''; exit_code=''}
        if (($mode -eq 'build' -or $mode -eq 'audit') -and !$lineage) {
            $row.status = 'skipped_no_lineage'
        } else {
            if ($label -eq 'build_changed') {
                # Change one source root, preserving all counts. This changes the
                # first object's persistent identity and exercises changed BUILD.
                $source = Join-Path $fixture 'sources/objects_0000.cpp'
                $content = [IO.File]::ReadAllText($source)
                $content = $content.Replace(' o000000000;', ' changed000000000;')
                [IO.File]::WriteAllText($source, $content, [Text.UTF8Encoding]::new($false))
            }
            $stdout = Join-Path $fixture "$label.stdout.txt"
            $stderr = Join-Path $fixture "$label.stderr.txt"
            $p = Start-Process -FilePath $exe -ArgumentList @($mode, ('"' + $project + '"'), $types) -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
            $watch = [Diagnostics.Stopwatch]::StartNew()
            while (!$p.HasExited) {
                $p.Refresh()
                $row.monitored_private_peak = [Math]::Max([long]$row.monitored_private_peak, $p.PrivateMemorySize64)
                $row.monitored_ws_peak = [Math]::Max([long]$row.monitored_ws_peak, $p.WorkingSet64)
                if ([Math]::Max($p.PrivateMemorySize64, $p.WorkingSet64) -gt ([long]$MemoryLimitGiB*1GB)) { $row.status='memory_limit'; $p.Kill(); break }
                if ($watch.Elapsed.TotalSeconds -gt $TimeoutSeconds) { $row.status='timeout'; $p.Kill(); break }
                Start-Sleep -Milliseconds 250
            }
            $p.WaitForExit()
            $row.process_elapsed_ms = $watch.Elapsed.TotalMilliseconds
            $row.exit_code = $p.ExitCode
            if (!$row.status) {
                $output = [IO.File]::ReadAllText($stdout)
                $metrics = @{}
                foreach ($pair in ($output.Trim() -split ',')) {
                    $kv = $pair -split '=',2
                    if ($kv.Count -eq 2) { $metrics[$kv[0]]=$kv[1] }
                }
                if ($p.ExitCode -ne 0) { $row.status='failed' }
                elseif ($metrics['types'] -ne "$types" -or $metrics['objects'] -ne "$($row.objects)") { $row.status='count_mismatch' }
                else {
                    $row.status='ok'
                    foreach ($key in @('total_ms','peak_ws_bytes','compiled_bytes')) { $row[$key]=$metrics[$key] }
                }
            }
            if ($mode -eq 'rebuild') { $lineage = $row.status -eq 'ok' }
            if ($label -eq 'build_changed' -and $row.status -ne 'ok') { $lineage=$false }
        }
        $rows.Add([pscustomobject]$row)
        $rows | Export-Csv (Join-Path $root 'results.csv') -NoTypeInformation
        Write-Host ($row | ConvertTo-Json -Compress)
    }
}
