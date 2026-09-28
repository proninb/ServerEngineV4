param(
    [ValidateRange(1, 21)]
    [int]$Runs = 5,

    [string]$ResultPath = "build/runtime-lookup-v1.csv"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$exe = Join-Path $root "build\Release\ServerEngineV4RuntimeLookupBenchmark.exe"

if (-not (Test-Path $exe)) {
    throw "Benchmark executable not found: $exe"
}

$result = [System.IO.Path]::GetFullPath(
    (Join-Path $root $ResultPath)
)

if (Test-Path $result) {
    throw "Result file must be new: $result"
}

$directory = Split-Path -Parent $result

if ($directory -and -not (Test-Path $directory)) {
    New-Item -ItemType Directory -Path $directory | Out-Null
}

$cases = @(
    [pscustomobject]@{ members = 1;    depth = 1; iterations = 1000000 },
    [pscustomobject]@{ members = 8;    depth = 1; iterations = 1000000 },
    [pscustomobject]@{ members = 32;   depth = 1; iterations = 1000000 },
    [pscustomobject]@{ members = 128;  depth = 1; iterations = 300000  },
    [pscustomobject]@{ members = 512;  depth = 1; iterations = 100000  },
    [pscustomobject]@{ members = 1024; depth = 1; iterations = 50000   },

    [pscustomobject]@{ members = 1;    depth = 2; iterations = 1000000 },
    [pscustomobject]@{ members = 8;    depth = 2; iterations = 1000000 },
    [pscustomobject]@{ members = 32;   depth = 2; iterations = 750000  },
    [pscustomobject]@{ members = 128;  depth = 2; iterations = 250000  },
    [pscustomobject]@{ members = 512;  depth = 2; iterations = 75000   },
    [pscustomobject]@{ members = 1024; depth = 2; iterations = 40000   },

    [pscustomobject]@{ members = 1;    depth = 4; iterations = 750000  },
    [pscustomobject]@{ members = 8;    depth = 4; iterations = 750000  },
    [pscustomobject]@{ members = 32;   depth = 4; iterations = 500000  },
    [pscustomobject]@{ members = 128;  depth = 4; iterations = 150000  },
    [pscustomobject]@{ members = 512;  depth = 4; iterations = 50000   },
    [pscustomobject]@{ members = 1024; depth = 4; iterations = 25000   },

    [pscustomobject]@{ members = 1;    depth = 8; iterations = 500000  },
    [pscustomobject]@{ members = 8;    depth = 8; iterations = 500000  },
    [pscustomobject]@{ members = 32;   depth = 8; iterations = 300000  },
    [pscustomobject]@{ members = 128;  depth = 8; iterations = 100000  },
    [pscustomobject]@{ members = 512;  depth = 8; iterations = 30000   },
    [pscustomobject]@{ members = 1024; depth = 8; iterations = 15000   }
)

$culture = [Globalization.CultureInfo]::InvariantCulture
$rows = @()

function Parse-Fields([string]$line) {
    $fields = @{}

    foreach ($part in $line.Split(',')) {
        $pair = $part.Split('=', 2)

        if ($pair.Count -ne 2) {
            throw "Invalid benchmark field: $part"
        }

        $fields[$pair[0]] = $pair[1]
    }

    return $fields
}

function Median([object[]]$values) {
    $sorted = @($values | Sort-Object)
    $middle = [int][Math]::Floor($sorted.Count / 2)

    if ($sorted.Count % 2 -eq 0) {
        return (
            $sorted[$middle - 1] +
            $sorted[$middle]
        ) / 2
    }

    return $sorted[$middle]
}

foreach ($case in $cases) {
    for ($run = 1; $run -le $Runs; ++$run) {
        $line = & $exe `
            $case.members `
            $case.depth `
            $case.iterations

        if ($LASTEXITCODE -ne 0) {
            throw (
                "Benchmark failed: members={0}, depth={1}, run={2}" -f
                $case.members,
                $case.depth,
                $run
            )
        }

        Write-Output $line

        $fields = Parse-Fields $line

        $required = @(
            'members',
            'depth',
            'iterations',
            'find_string_ns',
            'find_identity_ns',
            'find_object_ns',
            'find_member_first_ns',
            'find_member_middle_ns',
            'find_member_last_ns',
            'find_member_missing_ns',
            'object_offset_ns',
            'member_offset_ns',
            'full_path_ns',
            'get_runtime_value_ns',
            'runtime_bytes',
            'compiled_bytes'
        )

        foreach ($name in $required) {
            if (-not $fields.ContainsKey($name)) {
                throw "Missing benchmark field: $name"
            }
        }

        $rows += [pscustomobject]@{
            members = [int]::Parse($fields.members, $culture)
            depth = [int]::Parse($fields.depth, $culture)
            run = $run
            iterations = [long]::Parse($fields.iterations, $culture)
            find_string_ns = [double]::Parse($fields.find_string_ns, $culture)
            find_identity_ns = [double]::Parse($fields.find_identity_ns, $culture)
            find_object_ns = [double]::Parse($fields.find_object_ns, $culture)
            find_member_first_ns = [double]::Parse($fields.find_member_first_ns, $culture)
            find_member_middle_ns = [double]::Parse($fields.find_member_middle_ns, $culture)
            find_member_last_ns = [double]::Parse($fields.find_member_last_ns, $culture)
            find_member_missing_ns = [double]::Parse($fields.find_member_missing_ns, $culture)
            object_offset_ns = [double]::Parse($fields.object_offset_ns, $culture)
            member_offset_ns = [double]::Parse($fields.member_offset_ns, $culture)
            full_path_ns = [double]::Parse($fields.full_path_ns, $culture)
            get_runtime_value_ns = [double]::Parse($fields.get_runtime_value_ns, $culture)
            runtime_bytes = [long]::Parse($fields.runtime_bytes, $culture)
            compiled_bytes = [long]::Parse($fields.compiled_bytes, $culture)
        }
    }
}

$rows |
    Export-Csv `
        -Path $result `
        -NoTypeInformation `
        -Encoding UTF8

Write-Output ""
Write-Output "Medians:"

foreach ($case in $cases) {
    $caseRows = @(
        $rows |
        Where-Object {
            $_.members -eq $case.members -and
            $_.depth -eq $case.depth
        }
    )

    $last = $caseRows[-1]

    Write-Output (
        "median,members={0},depth={1},find_member_first_ns={2:F3},find_member_middle_ns={3:F3},find_member_last_ns={4:F3},find_member_missing_ns={5:F3},full_path_ns={6:F3},get_runtime_value_ns={7:F3},find_string_ns={8:F3},find_identity_ns={9:F3},find_object_ns={10:F3},object_offset_ns={11:F3},member_offset_ns={12:F3},runtime_bytes={13},compiled_bytes={14}" -f
        $case.members,
        $case.depth,
        (Median @($caseRows.find_member_first_ns)),
        (Median @($caseRows.find_member_middle_ns)),
        (Median @($caseRows.find_member_last_ns)),
        (Median @($caseRows.find_member_missing_ns)),
        (Median @($caseRows.full_path_ns)),
        (Median @($caseRows.get_runtime_value_ns)),
        (Median @($caseRows.find_string_ns)),
        (Median @($caseRows.find_identity_ns)),
        (Median @($caseRows.find_object_ns)),
        (Median @($caseRows.object_offset_ns)),
        (Median @($caseRows.member_offset_ns)),
        $last.runtime_bytes,
        $last.compiled_bytes
    )
}

Write-Output ""
Write-Output "Results: $result"
