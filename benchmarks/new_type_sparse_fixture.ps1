param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][ValidateRange(2,1000000)][int]$TypeCount,
    [ValidateRange(1,4096)][int]$BulkFileCount = 128
)

$ErrorActionPreference = 'Stop'

$root = [IO.Path]::GetFullPath($OutputDirectory)

if (Test-Path -LiteralPath $root) {
    throw "Output directory must be new: $root"
}

[IO.Directory]::CreateDirectory($root) | Out-Null
$bulk = Join-Path $root 'bulk'
[IO.Directory]::CreateDirectory($bulk) | Out-Null

$utf8 = [Text.UTF8Encoding]::new($false)

[IO.File]::WriteAllText(
    (Join-Path $root 'changed.hpp'),
    "struct T000000000 { int value; };`n",
    $utf8)

$remaining = $TypeCount - 1
$files = [Math]::Min($remaining, $BulkFileCount)

$entries = [Collections.Generic.List[object]]::new()
$entries.Add([ordered]@{
    name = 'changed.hpp'
    type = 'header'
    path = 'changed.hpp'
})

for ($file = 0; $file -lt $files; ++$file) {
    $name = 'types_{0:D4}.hpp' -f $file
    $path = Join-Path $bulk $name

    [long]$begin =
        1 + [long][Math]::Floor(
            ([double]$remaining * $file) / $files)

    [long]$end =
        1 + [long][Math]::Floor(
            ([double]$remaining * ($file + 1)) / $files)

    $builder = [Text.StringBuilder]::new()

    for ($index = $begin; $index -lt $end; ++$index) {
        [void]$builder.AppendFormat(
            "struct T{0:D9} {{ int value; }};`n",
            $index)
    }

    [IO.File]::WriteAllText(
        $path,
        $builder.ToString(),
        $utf8)

    $entries.Add([ordered]@{
        name = $name
        type = 'header'
        path = "bulk/$name"
    })
}

$config = [ordered]@{
    version = 1
    name = "TypeSparse_$TypeCount"
    project = $entries
    preprocessor = [ordered]@{
        predefines = @()
    }
}

[IO.File]::WriteAllText(
    (Join-Path $root 'project.json'),
    ($config | ConvertTo-Json -Depth 8),
    $utf8)

Write-Host "Generated $TypeCount independent types: $root"
