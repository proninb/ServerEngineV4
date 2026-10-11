# V3-21 fresh result paths, no overwriting output.
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$RepoRoot,
 [Parameter(Mandatory=$true)][string]$ProjectPath,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$names=@('summary','unresolved','callable','shape','canonical','no-name',
 'unresolved-types','base-pairs','operators','constructor-details','constructor-summary',
 'construction-details','construction-summary','graph-summary','graph-details')
$paths=@($names | ForEach-Object {Join-Path $OutputDirectory ("v321-$_.csv")})
foreach($path in $paths){if(Test-Path -LiteralPath $path){throw "Result file must be new: $path"}}
& (Join-Path $PSScriptRoot 'run_header_syntax_v3_21.ps1') `
 -RepoRoot $RepoRoot -ProjectPath $ProjectPath `
 -ResultPath $paths[0] -UnresolvedPath $paths[1] -CallablePath $paths[2] `
 -TypeMismatchPath $paths[3] -CanonicalPath $paths[4] -NoNamePath $paths[5] `
 -UnresolvedTypePath $paths[6] -BasePairsPath $paths[7] -OperatorPath $paths[8] `
 -ConstructorPath $paths[9] -ConstructorSummaryPath $paths[10] `
 -ConstructionPath $paths[11] -ConstructionSummaryPath $paths[12] `
 -GraphSummaryPath $paths[13] -GraphDetailsPath $paths[14] `
 -SkipBuild:$SkipBuild
if($LASTEXITCODE -ne 0){throw "V3-21 runner failed, exit=$LASTEXITCODE"}
