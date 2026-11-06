param(
    [int]$Pairs = 5,
    [int]$Duration = 30
)

$ErrorActionPreference = "Stop"
$root = "C:\Boris\ServerEngineV4"
$release = "$root\build\task-perf-35-isolated\B-perf34\_build\Release"
$server = "$release\ServerEngineV4.exe"
$config = "$root\build\tcp-pgo-release-02\tcp-integration\candidate\server.json"
$project = "$root\build\tcp-pgo-release-02\fixtures\unit\project.json"
$runner = "$root\task-performance-01\task_performance_01.py"
$out = "$root\build\task-scheduler-cpu-42\$(Get-Date -Format yyyyMMdd-HHmmss)"

New-Item -ItemType Directory -Force $out | Out-Null

foreach ($path in @($server, $config, $project, $runner)) {
    if (!(Test-Path $path)) {
        throw "Missing: $path"
    }
}

$names = @(
    "TASK_SCHEDULER_TIMER",
    "TASK_SCHEDULER_THREAD",
    "TASK_SCHEDULER_EXECUTION",
    "TASK_SCHEDULER_PERIOD_MS",
    "TASK_SCHEDULER_DEADLINE",
    "TASK_SCHEDULER_DEADLINE_CSV",
    "TASK_SHM_PERF07_DIR",
    "TASK_SCHEDULER_TASK_CSV",
    "TASK_PERF02_CSV"
)

$old = @{}
foreach ($name in $names) {
    $old[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
}

function Percentile($values, [double]$p) {
    $a = @($values | Sort-Object)
    if (!$a.Count) { return 0.0 }
    $index = [Math]::Ceiling($p * $a.Count) - 1
    return [double]$a[[Math]::Max(0, $index)]
}

try {
    $env:TASK_SCHEDULER_TIMER = "HRTIMER"
    $env:TASK_SCHEDULER_THREAD = "1"
    $env:TASK_SCHEDULER_EXECUTION = "LOCKSTEP"
    $env:TASK_SCHEDULER_PERIOD_MS = "2"
    $env:TASK_SCHEDULER_DEADLINE = "1"

    $results = @()

    for ($pair = 1; $pair -le $Pairs; $pair++) {
        $order = if ($pair % 2) {
            @("OFF", "ON")
        } else {
            @("ON", "OFF")
        }

        foreach ($mode in $order) {
            $tag = "{0:D2}-{1}" -f $pair, $mode
            $runDir = Join-Path $out $tag
            New-Item -ItemType Directory -Force $runDir | Out-Null

            $deadline = Join-Path $runDir "deadline.csv"
            $throughput = Join-Path $runDir "throughput.csv"
            $log = Join-Path $runDir "run.log"

            $env:TASK_SCHEDULER_DEADLINE_CSV = $deadline
            Remove-Item Env:TASK_SHM_PERF07_DIR -ErrorAction SilentlyContinue
            Remove-Item Env:TASK_SCHEDULER_TASK_CSV -ErrorAction SilentlyContinue
            Remove-Item Env:TASK_PERF02_CSV -ErrorAction SilentlyContinue

            if ($mode -eq "ON") {
                $qpc = Join-Path $runDir "qpc"
                New-Item -ItemType Directory -Force $qpc | Out-Null
                $env:TASK_SHM_PERF07_DIR = $qpc
            }

            Write-Host "=== CPU-42 $tag ==="

            & python $runner `
                --server $server `
                --config $config `
                --project $project `
                --fixture-dir $release `
                --duration $Duration `
                --warmup 2 `
                --periods 2 `
                --counts 4 `
                --output $throughput 2>&1 |
                Tee-Object -FilePath $log

            if ($LASTEXITCODE -ne 0) {
                throw "Benchmark failed: $tag"
            }

            $t = @(Import-Csv $throughput)
            $d = @(Import-Csv $deadline)

            if (!$t.Count -or !$d.Count) {
                throw "Missing measurements: $tag"
            }

            $speed = [double]$t[-1].cycles_per_s
            $barriers = @($d | ForEach-Object {
                [double]$_.barrier_duration_us
            })
            $misses = @($d | Where-Object {
                [int]$_.deadline_miss -ne 0
            }).Count

            $skipped = 0
            foreach ($row in $d) {
                $skipped += [long]$row.skipped_periods
            }

            $results += [pscustomobject]@{
                pair = $pair
                mode = $mode
                cycles_per_s = $speed
                barrier_p99_us = Percentile $barriers 0.99
                barrier_max_us = ($barriers | Measure-Object -Maximum).Maximum
                deadline_misses = $misses
                skipped_periods = $skipped
            }

            $results[-1] | Format-List
        }
    }

    $results | Export-Csv (Join-Path $out "runs.csv") -NoTypeInformation

    $summary = foreach ($mode in @("OFF", "ON")) {
        $subset = @($results | Where-Object mode -EQ $mode)
        [pscustomobject]@{
            mode = $mode
            runs = $subset.Count
            mean_cycles_per_s = [Math]::Round(
                ($subset | Measure-Object cycles_per_s -Average).Average, 3)
            worst_barrier_p99_us = ($subset |
                Measure-Object barrier_p99_us -Maximum).Maximum
            worst_barrier_max_us = ($subset |
                Measure-Object barrier_max_us -Maximum).Maximum
            total_deadline_misses = ($subset |
                Measure-Object deadline_misses -Sum).Sum
            total_skipped_periods = ($subset |
                Measure-Object skipped_periods -Sum).Sum
        }
    }

    $summary | Export-Csv (Join-Path $out "summary.csv") -NoTypeInformation
    $summary | Format-Table -AutoSize
    Write-Host "Results: $out"
}
finally {
    foreach ($name in $names) {
        [Environment]::SetEnvironmentVariable(
            $name, $old[$name], "Process")
    }
}
