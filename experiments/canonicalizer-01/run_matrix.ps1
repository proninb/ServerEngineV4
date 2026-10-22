param([Parameter(Mandatory=$true)][string]$Executable,[Parameter(Mandatory=$true)][string]$Results,[Parameter(Mandatory=$true)][string]$UnitTrace,[Parameter(Mandatory=$true)][string]$ScopeTrace)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Force $Results | Out-Null
$exe=(Resolve-Path -LiteralPath $Executable).Path
$datasets=@(@{name='unitproxl';input=(Resolve-Path -LiteralPath $UnitTrace).Path;rounds=7},@{name='scopes';input=(Resolve-Path -LiteralPath $ScopeTrace).Path;rounds=5},@{name='synthetic-flat';input='synthetic-flat';rounds=5},@{name='synthetic-nested';input='synthetic-nested';rounds=5})
$cases=@(foreach($algorithm in 'sort','index','hierarchical'){foreach($workers in 1,2,4,8,16){@{algorithm=$algorithm;workers=$workers}}})
$rows=[Collections.Generic.List[object]]::new()
function Run-Case($data,$case,$round,$diagnostic){
 $tag="$($data.name)-$($case.algorithm)-w$($case.workers)-r$round-d$diagnostic"
 $argsList=@($data.input,$case.algorithm,[string]$case.workers)
 if($diagnostic){$argsList+='diagnostic'}
 $line=& $exe @argsList
 if($LASTEXITCODE -ne 0){throw "Kernel failed: $tag"}
 $line|Set-Content (Join-Path $Results "$tag.txt")
 $fields=[ordered]@{dataset=$data.name;round=$round}
 foreach($part in ($line -split ',')){$pair=$part -split '=',2;if($pair.Count -eq 2){$fields[$pair[0]]=$pair[1]}}
 if($fields.validation -ne 'passed' -or $fields.producers -ne '16'){throw "Invalid result: $tag"}
 if($data.name -eq 'unitproxl' -and ($fields.strings -ne '188630' -or $fields.identities -ne '161612')){throw 'Real corpus inventory mismatch'}
 if($round -eq 0 -and !$diagnostic){return}
 $rows.Add([pscustomobject]$fields)
 $rows|Export-Csv (Join-Path $Results 'measurements.csv') -NoTypeInformation
}
foreach($data in $datasets){
 Run-Case $data @{algorithm='reference';workers=1} -1 $true
 foreach($round in 0..$data.rounds){
  $order=@(0..($cases.Count-1)|ForEach-Object{($_+3*$round)%$cases.Count})
  if($round%2){[array]::Reverse($order)}
  foreach($index in $order){Run-Case $data $cases[$index] $round $false}
  "dataset=$($data.name),round=$round complete"
 }
 foreach($case in $cases){Run-Case $data $case -2 $true}
}
$manifest=[ordered]@{executable_sha256=(Get-FileHash -LiteralPath $exe).Hash;unitproxl_trace_sha256=(Get-FileHash -LiteralPath $UnitTrace).Hash;scope_trace_sha256=(Get-FileHash -LiteralPath $ScopeTrace).Hash;logical_producers=16;timing_diagnostics=$false;validation='all runs validate identity/string projection and binding queries';process_architecture=$env:PROCESSOR_ARCHITECTURE;logical_processors=$env:NUMBER_OF_PROCESSORS}
$manifest|ConvertTo-Json -Depth 5|Set-Content (Join-Path $Results 'manifest.json')
