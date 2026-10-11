# V3-41: diagnostic instrumented V3-38A; paired run measures instrumentation overhead, NOT an optimization.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$ProjectPath,
    [string]$OutputDirectory,
    [ValidateRange(1,100)][int]$Pairs=31,
    [ValidateRange(0,30)][int]$WarmupPairs=2,
    [ValidateRange(1,1024)][int]$SampleStride=32,
    [switch]$SkipBuild,
    [switch]$SkipAllocationProfile
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$RepoRoot=(Resolve-Path -LiteralPath $RepoRoot -ErrorAction Stop).Path
$ProjectPath=(Resolve-Path -LiteralPath $ProjectPath -ErrorAction Stop).Path
if([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory=Join-Path (Join-Path $RepoRoot 'build') 'benchmarks'
}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
& git -C $RepoRoot merge-base --is-ancestor '6f50a4e2a2963f1dab46b993203cc579d71a3415' HEAD
if ($LASTEXITCODE -ne 0) {
    throw 'Required ServerEngineV4 parser checkpoint is not an ancestor of HEAD'
}
& python (Join-Path $PSScriptRoot 'tests/test_v341_source_contract.py')
if($LASTEXITCODE -ne 0) { throw 'V3-41 source verification FAILED' }
$files=@('v341-pairs.csv','v341-phases.csv','v341-type-samples.csv',
    'v341-summary.csv','v341-resolution.csv','v341-fields.csv','v341-review.md')
if(-not $SkipAllocationProfile) {$files+='v341-allocations.csv'}
foreach($name in $files) {
    $path=Join-Path $OutputDirectory $name
    if(Test-Path -LiteralPath $path) {throw "Output must be new (will not overwrite): $path"}
}
$workspace=Join-Path $OutputDirectory '_experimental_v341'
$overlay=Join-Path $workspace 'engine-overlay'
$build=Join-Path $workspace 'build'
$marker=Join-Path $overlay 'v337_direct_range.marker'
if(-not $SkipBuild) {
    if(Test-Path -LiteralPath $overlay) {
        throw "V3-41 overlay already exists; use -SkipBuild or a new OutputDirectory: $overlay"
    }
    New-Item -ItemType Directory -Path $workspace -Force | Out-Null
    & python (Join-Path $PSScriptRoot 'tools/build_v337_sources.py') '--repo' $RepoRoot '--overlay' $overlay
    if($LASTEXITCODE -ne 0) {throw 'V3-41 overlay generation FAILED'}
    & cmake -S $PSScriptRoot -B $build "-DSERVERENGINE_ROOT=$($overlay.Replace('\','/'))" '-DSERVER_ENGINE_ENABLE_IPO=OFF' '-DBUILD_TESTING=OFF'
    if($LASTEXITCODE -ne 0) {throw 'V3-41 cmake configure FAILED'}
} else {
    if(-not (Test-Path -LiteralPath $marker -PathType Leaf)) {throw 'V3-41 -SkipBuild requires existing overlay'}
}
$targets=@('V341RangeTests','V341ScannerTests','V341FieldsInvariantTests',
    'ServerEngineV4HeaderSyntaxV341Perf')
if(-not $SkipAllocationProfile) {$targets+='ServerEngineV4HeaderSyntaxV341Alloc'}
if(-not $SkipBuild) {
    foreach($target in $targets) {
        & cmake --build $build --config Release --target $target --parallel 1
        if($LASTEXITCODE -ne 0) {throw "V3-41 target build FAILED: $target"}
    }
}
$registered=@(& ctest --test-dir $build -C Release -N -R '^V341')
if($LASTEXITCODE -ne 0 -or @($registered | Where-Object {$_ -match '^Total Tests: 3\s*$'}).Count -ne 1) {
    throw 'Expected exactly 3 V341 Release-safe tests'
}
& ctest --test-dir $build -C Release --output-on-failure -R '^V341'
if($LASTEXITCODE -ne 0) {throw 'V3-41 regression tests FAILED'}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$perf=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV341Perf.exe'
$alloc=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV341Alloc.exe'
if(-not(Test-Path -LiteralPath $perf -PathType Leaf)) {throw "Missing executable: $perf"}
& $perf $ProjectPath (Join-Path $OutputDirectory 'v341-pairs.csv') `
    (Join-Path $OutputDirectory 'v341-phases.csv') `
    (Join-Path $OutputDirectory 'v341-type-samples.csv') `
    (Join-Path $OutputDirectory 'v341-summary.csv') `
    (Join-Path $OutputDirectory 'v341-resolution.csv') `
    (Join-Path $OutputDirectory 'v341-fields.csv') `
    $WarmupPairs $Pairs $SampleStride
if($LASTEXITCODE -ne 0) {throw 'V3-41 Header graph parity/perf FAILED'}
if(-not $SkipAllocationProfile) {
    if(-not(Test-Path -LiteralPath $alloc -PathType Leaf)) {throw "Missing executable: $alloc"}
    & $alloc '--alloc-only' $ProjectPath (Join-Path $OutputDirectory 'v341-allocations.csv')
    if($LASTEXITCODE -ne 0) {throw 'V3-41 allocation parity FAILED'}
}
$validation=@((Join-Path $PSScriptRoot 'tools/analyze_v341.py'),
    '--directory',$OutputDirectory,'--pairs',"$Pairs",'--warmups',"$WarmupPairs")
if($SkipAllocationProfile) {$validation+='--no-allocations'}
& python @validation
if($LASTEXITCODE -ne 0) {throw 'V3-41 review FAILED'}
Write-Host "V3-41 VERIFIED pairs=$Pairs reports=$OutputDirectory HeaderGraph=PASS FullPUBLISH=PARTIAL"
Write-Host "Production repository unchanged: $RepoRoot"
