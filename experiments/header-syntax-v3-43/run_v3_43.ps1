# V3-43: frozen V3-38A vs V3-42 with reversed inclusion-order validation and codegen probes.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$ProjectPath,
    [string]$OutputDirectory,
    [ValidateRange(1,100)][int]$Pairs=31,
    [ValidateRange(0,30)][int]$WarmupPairs=2,
    [ValidateRange(1,1024)][int]$SampleStride=32,
    [switch]$SkipBuild,
    [switch]$SkipAllocationProfile,
    [switch]$SkipReverseOrder,
    [switch]$RequireAssembly
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
& python (Join-Path $PSScriptRoot 'tests/test_v342_source_contract.py')
if($LASTEXITCODE -ne 0) { throw 'V3-43 frozen V3-42 source FAILED' }
& python (Join-Path $PSScriptRoot 'tests/test_v343_source_contract.py')
if($LASTEXITCODE -ne 0) { throw 'V3-43 reverse-order source FAILED' }
& python (Join-Path $PSScriptRoot 'tests/test_v343_access_audit.py')
if($LASTEXITCODE -ne 0) { throw 'V3-43 source verification FAILED' }
$files=@('v343-pairs.csv','v343-phases.csv','v343-type-samples.csv',
    'v343-summary.csv','v343-review.md'
)
if(-not $SkipAllocationProfile) {$files+='v343-allocations.csv'}
if(-not $SkipReverseOrder) {$files+=@('v343-reverse-pairs.csv','v343-reverse-phases.csv','v343-reverse-type-samples.csv','v343-reverse-summary.csv')}
$files+='v343-codegen.md'
foreach($name in $files) {
    $path=Join-Path $OutputDirectory $name
    if(Test-Path -LiteralPath $path) {throw "Output must be new (will not overwrite): $path"}
}
$workspace=Join-Path $OutputDirectory '_experimental_v343'
$overlay=Join-Path $workspace 'engine-overlay'
$build=Join-Path $workspace 'build'
$marker=Join-Path $overlay 'v337_direct_range.marker'
if(-not $SkipBuild) {
    if(Test-Path -LiteralPath $overlay) {
        throw "V3-43 overlay already exists; use -SkipBuild or a new OutputDirectory: $overlay"
    }
    New-Item -ItemType Directory -Path $workspace -Force | Out-Null
    & python (Join-Path $PSScriptRoot 'tools/build_v337_sources.py') '--repo' $RepoRoot '--overlay' $overlay
    if($LASTEXITCODE -ne 0) {throw 'V3-43 overlay generation FAILED'}
    & cmake -S $PSScriptRoot -B $build "-DSERVERENGINE_ROOT=$($overlay.Replace('\','/'))" '-DSERVER_ENGINE_ENABLE_IPO=OFF' '-DBUILD_TESTING=OFF'
    if($LASTEXITCODE -ne 0) {throw 'V3-43 cmake configure FAILED'}
} else {
    if(-not (Test-Path -LiteralPath $marker -PathType Leaf)) {throw 'V3-43 -SkipBuild requires existing overlay'}
}
$targets=@('V343RangeTests','V343ScannerTests','V343TypeSpecTests','V343LifetimeTests','V343CodegenRuntimeTests',
    'ServerEngineV4HeaderSyntaxV343Perf')
if(-not $SkipAllocationProfile) {$targets+='ServerEngineV4HeaderSyntaxV343Alloc'}
if(-not $SkipReverseOrder) {$targets+='ServerEngineV4HeaderSyntaxV343ReversePerf'}
if(-not $SkipBuild) {
    foreach($target in $targets) {
        & cmake --build $build --config Release --target $target --parallel 1
        if($LASTEXITCODE -ne 0) {throw "V3-43 target build FAILED: $target"}
    }
}
$registered=@(& ctest --test-dir $build -C Release -N -R '^V343')
if($LASTEXITCODE -ne 0 -or @($registered | Where-Object {$_ -match '^Total Tests: 5\s*$'}).Count -ne 1) {
    throw 'Expected exactly 5 V343 Release-safe tests'
}
& ctest --test-dir $build -C Release --output-on-failure -R '^V343'
if($LASTEXITCODE -ne 0) {throw 'V3-43 regression tests FAILED'}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$perf=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV343Perf.exe'
$alloc=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV343Alloc.exe'
if(-not(Test-Path -LiteralPath $perf -PathType Leaf)) {throw "Missing executable: $perf"}
& $perf $ProjectPath (Join-Path $OutputDirectory 'v343-pairs.csv') `
    (Join-Path $OutputDirectory 'v343-phases.csv') `
    (Join-Path $OutputDirectory 'v343-type-samples.csv') `
    (Join-Path $OutputDirectory 'v343-summary.csv') `
    $WarmupPairs $Pairs $SampleStride
if($LASTEXITCODE -ne 0) {throw 'V3-43 Header graph parity/perf FAILED'}
if(-not $SkipReverseOrder) {
    $reverse=Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV343ReversePerf.exe'
    if(-not(Test-Path -LiteralPath $reverse -PathType Leaf)) {throw "Missing executable: $reverse"}
    & $reverse $ProjectPath (Join-Path $OutputDirectory 'v343-reverse-pairs.csv') `
        (Join-Path $OutputDirectory 'v343-reverse-phases.csv') `
        (Join-Path $OutputDirectory 'v343-reverse-type-samples.csv') `
        (Join-Path $OutputDirectory 'v343-reverse-summary.csv') `
        $WarmupPairs $Pairs $SampleStride
    if($LASTEXITCODE -ne 0) {throw 'V3-43 reversed inclusion Header Graph parity FAILED'}
}
if(-not $SkipAllocationProfile) {
    if(-not(Test-Path -LiteralPath $alloc -PathType Leaf)) {throw "Missing executable: $alloc"}
    & $alloc '--alloc-only' $ProjectPath (Join-Path $OutputDirectory 'v343-allocations.csv')
    if($LASTEXITCODE -ne 0) {throw 'V3-43 allocation parity FAILED'}
}
$asm=Join-Path $OutputDirectory 'v343-codegen.md'
$compiler=Get-Command cl.exe -ErrorAction SilentlyContinue
if($null -eq $compiler) {
    if($RequireAssembly) {throw 'cl.exe unavailable: run in x64 Native Tools/Developer PowerShell for VS'}
    Set-Content -LiteralPath $asm -Encoding UTF8 -Value '# V3-43 MSVC codegen SKIPPED - cl.exe not on PATH. Run from Developer PowerShell with -RequireAssembly.'
} else {
    $probeDir=Join-Path $build 'v343-codegen'
    New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
    $source=Join-Path $PSScriptRoot 'tests/v343_codegen_probe.cpp'
    $asmPath=Join-Path $probeDir 'v343-probe.asm'
    $objPath=Join-Path $probeDir 'v343-probe.obj'
    $includeMocks='/I'+(Join-Path $PSScriptRoot 'tests/mock_project')
    $includeBench='/I'+(Join-Path $PSScriptRoot 'benchmarks')
    $includeOverlay='/I'+$overlay
    & $compiler.Source /nologo /std:c++20 /O2 /GL- /EHsc /c /FAcs $includeMocks $includeBench $includeOverlay "/Fo:$objPath" "/Fa:$asmPath" $source
    if($LASTEXITCODE -ne 0) {throw 'V3-43 MSVC assembly compilation failed'}
    & python (Join-Path $PSScriptRoot 'tools/v343_asm_review.py') '--asm' $asmPath '--out' $asm
    if($LASTEXITCODE -ne 0) {throw 'V3-43 MSVC assembly review failed'}
}
$validation=@((Join-Path $PSScriptRoot 'tools/analyze_v343.py'),
    '--directory',$OutputDirectory,'--pairs',"$Pairs",'--warmups',"$WarmupPairs")
if($SkipAllocationProfile) {$validation+='--no-allocations'}
if($SkipReverseOrder) {$validation+='--no-reverse'}
& python @validation
if($LASTEXITCODE -ne 0) {throw 'V3-43 review FAILED'}
Write-Host "V3-43 VERIFIED pairs=$Pairs reports=$OutputDirectory HeaderGraph=PASS FullPUBLISH=PARTIAL"
Write-Host "Production repository unchanged: $RepoRoot"
