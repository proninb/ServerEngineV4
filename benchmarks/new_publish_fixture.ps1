# Generate deterministic input only. Run PUBLISH/audit separately so generation
# and correctness checks are excluded from lifecycle timing.
param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [ValidateSet('simple', 'mixed')][string]$Scenario = 'simple',
    [ValidateRange(1, 10000000)][int]$TypeCount = 100000,
    [ValidateRange(1, 1024)][int]$FileCount = 128
)
$ErrorActionPreference = 'Stop'
$fixtureRoot = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $fixtureRoot) {
    throw "Output directory must be new: $fixtureRoot"
}
[IO.Directory]::CreateDirectory($fixtureRoot) | Out-Null
[IO.Directory]::CreateDirectory((Join-Path $fixtureRoot 'headers')) | Out-Null
$fixtureUtf8 = [Text.UTF8Encoding]::new($false)
$fixtureEntries = [Collections.Generic.List[object]]::new()
$fixtureFiles = [Math]::Min($TypeCount, $FileCount)
if ($Scenario -eq 'mixed') {
    [IO.Directory]::CreateDirectory((Join-Path $fixtureRoot 'sources')) | Out-Null
    [IO.File]::WriteAllText((Join-Path $fixtureRoot 'headers/common.hpp'),
        "#ifndef BENCH_COMMON`n#define BENCH_COMMON`nstruct Shared { int shared; };`n#endif`n", $fixtureUtf8)
}
for ($fixtureFile = 0; $fixtureFile -lt $fixtureFiles; ++$fixtureFile) {
    $fixtureName = 'types_{0:D4}.hpp' -f $fixtureFile
    $fixtureHeader = [Text.StringBuilder]::new()
    $fixtureSource = [Text.StringBuilder]::new()
    if ($Scenario -eq 'mixed') {
        [void]$fixtureHeader.Append("#include `"common.hpp`"`n#include `"common.hpp`"`n")
    }
    $fixtureBegin = [int][Math]::Floor([double]$TypeCount * $fixtureFile / $fixtureFiles)
    $fixtureEnd = [int][Math]::Floor([double]$TypeCount * ($fixtureFile + 1) / $fixtureFiles)
    for ($fixtureIndex = $fixtureBegin; $fixtureIndex -lt $fixtureEnd; ++$fixtureIndex) {
        $fixtureType = 'T{0:D9}' -f $fixtureIndex
        if ($Scenario -eq 'simple') {
            [void]$fixtureHeader.Append("struct $fixtureType { int value; };`n")
        }
        else {
            [void]$fixtureHeader.Append("struct $fixtureType {`n")
            for ($fixtureMember = 0; $fixtureMember -lt 16; ++$fixtureMember) {
                [void]$fixtureHeader.Append("int field_$fixtureMember = $fixtureMember;`n")
            }
            [void]$fixtureHeader.Append("int& input;`n")
            [void]$fixtureHeader.Append("$fixtureType() : field_0(7), field_1(11), input(field_0) {} };`n")
            [void]$fixtureSource.Append("$fixtureType a$fixtureIndex;`n$fixtureType b$fixtureIndex;`na$fixtureIndex.input = b$fixtureIndex.field_0;`n")
        }
    }
    [IO.File]::WriteAllText((Join-Path $fixtureRoot "headers/$fixtureName"), $fixtureHeader.ToString(), $fixtureUtf8)
    if ($Scenario -eq 'mixed') {
        $fixtureSourceName = 'objects_{0:D4}.cpp' -f $fixtureFile
        [IO.File]::WriteAllText((Join-Path $fixtureRoot "sources/$fixtureSourceName"), $fixtureSource.ToString(), $fixtureUtf8)
        # Exercise the Header-before-Source semantic contract independently of
        # the order in which roots appear in project.json.
        $fixtureEntries.Add([ordered]@{name=$fixtureSourceName; type='source'; path="sources/$fixtureSourceName"})
    }
    $fixtureEntries.Add([ordered]@{name=$fixtureName; type='header'; path="headers/$fixtureName"})
}
$fixtureConfiguration = [ordered]@{
    version = 1
    name = "PublishBenchmark_${Scenario}_$TypeCount"
    project = @($fixtureEntries.ToArray())
    preprocessor = [ordered]@{predefines = @()}
}
$fixtureProject = Join-Path $fixtureRoot 'project.json'
[IO.File]::WriteAllText($fixtureProject, ($fixtureConfiguration | ConvertTo-Json -Depth 8), $fixtureUtf8)
$fixtureExpected = $TypeCount
if ($Scenario -eq 'mixed') { ++$fixtureExpected }
Write-Output "project=$fixtureProject"
Write-Output "expected_types=$fixtureExpected"
