# V3-18: read-only complete construction parity audit. No production mutations.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$RepoRoot,
    [Parameter(Mandatory=$true)][string]$ProjectPath,
    [Parameter(Mandatory=$true)][string]$ResultPath,
    [Parameter(Mandatory=$true)][string]$UnresolvedPath,
    [Parameter(Mandatory=$true)][string]$CallablePath,
    [Parameter(Mandatory=$true)][string]$TypeMismatchPath,
    [Parameter(Mandatory=$true)][string]$CanonicalPath,
    [Parameter(Mandatory=$true)][string]$NoNamePath,
    [Parameter(Mandatory=$true)][string]$UnresolvedTypePath,
    [Parameter(Mandatory=$true)][string]$BasePairsPath,
    [Parameter(Mandatory=$true)][string]$OperatorPath,
    [Parameter(Mandatory=$true)][string]$ConstructorPath,
    [Parameter(Mandatory=$true)][string]$ConstructorSummaryPath,
    [Parameter(Mandatory=$true)][string]$ConstructionPath,
    [Parameter(Mandatory=$true)][string]$ConstructionSummaryPath,
    [Parameter(Mandatory=$true)][string]$GraphSummaryPath,
    [Parameter(Mandatory=$true)][string]$GraphDetailsPath,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot -ErrorAction Stop).Path
$ProjectPath = (Resolve-Path -LiteralPath $ProjectPath -ErrorAction Stop).Path
$ResultPath = [IO.Path]::GetFullPath($ResultPath)
$UnresolvedPath = [IO.Path]::GetFullPath($UnresolvedPath)
$CallablePath = [IO.Path]::GetFullPath($CallablePath)
$TypeMismatchPath = [IO.Path]::GetFullPath($TypeMismatchPath)
$CanonicalPath = [IO.Path]::GetFullPath($CanonicalPath)
$NoNamePath = [IO.Path]::GetFullPath($NoNamePath)
$UnresolvedTypePath = [IO.Path]::GetFullPath($UnresolvedTypePath)
$BasePairsPath = [IO.Path]::GetFullPath($BasePairsPath)
$OperatorPath = [IO.Path]::GetFullPath($OperatorPath)
$ConstructorPath = [IO.Path]::GetFullPath($ConstructorPath)
$ConstructorSummaryPath = [IO.Path]::GetFullPath($ConstructorSummaryPath)
$ConstructionPath = [IO.Path]::GetFullPath($ConstructionPath)
$ConstructionSummaryPath = [IO.Path]::GetFullPath($ConstructionSummaryPath)
$GraphSummaryPath = [IO.Path]::GetFullPath($GraphSummaryPath)
$GraphDetailsPath = [IO.Path]::GetFullPath($GraphDetailsPath)
$paths = @($ResultPath,$UnresolvedPath,$CallablePath,$TypeMismatchPath,$CanonicalPath,$NoNamePath,$UnresolvedTypePath,$BasePairsPath,$OperatorPath,$ConstructorPath,$ConstructorSummaryPath,$ConstructionPath,$ConstructionSummaryPath,$GraphSummaryPath,$GraphDetailsPath)
if (@($paths | Select-Object -Unique).Count -ne 15) {throw 'Output paths must be distinct'}
foreach ($path in $paths) {
    if (Test-Path -LiteralPath $path) {throw "Result file must be new: $path"}
}
$build = Join-Path $PSScriptRoot 'build'
$exe = Join-Path $build 'Release/ServerEngineV4HeaderSyntaxV321.exe'
if (-not $SkipBuild) {
    $repoArgument = $RepoRoot.Replace('\','/')
    & cmake -S $PSScriptRoot -B $build "-DSERVERENGINE_ROOT=$repoArgument" '-DSERVER_ENGINE_ENABLE_IPO=OFF'
    if ($LASTEXITCODE -ne 0) {throw 'V3-21 CMake configure failed'}
    & cmake --build $build --config Release --target ServerEngineV4HeaderSyntaxV321 --parallel 1
    if ($LASTEXITCODE -ne 0) {throw 'V3-21 MSVC build failed'}
}
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {throw "Missing V3-17 executable: $exe"}
foreach ($path in $paths) {New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null}
$lines = @(& $exe $ProjectPath $UnresolvedPath $CallablePath $TypeMismatchPath $CanonicalPath $NoNamePath $UnresolvedTypePath $BasePairsPath $OperatorPath $ConstructorPath $ConstructorSummaryPath $ConstructionPath $ConstructionSummaryPath $GraphSummaryPath $GraphDetailsPath | ForEach-Object {[string]$_})
if ($LASTEXITCODE -ne 0) {throw "V3-18 executable failed, exit=$LASTEXITCODE`n$($lines -join [Environment]::NewLine)"}
$marker = @($lines | Where-Object {$_ -cmatch '^effective_parity=PASS shadow_bridge=PASS header_v2_graph=PASS field_identity=(PASS|GAPS) semantic_type_parity=(PARTIAL|FULL_FIELD_TYPE_PASS) operator_classification=(PASS|PARTIAL) constructor_parity=PARTIAL constructor_target_parity=(PASS|PARTIAL) construction_parity=(FULL_CONSTRUCTION_PARITY_PASS|PARTIAL)$'})
if ($marker.Count -ne 1) {throw 'Missing or duplicate V3-17 gate marker'}
$header='mode,full_type_fields,full_type_checked,full_type_matches,full_type_mismatches,full_type_not_checked,missing_fields,no_name,operators,canonical_samples,canonical_ms,semantic_graph_gate'
$csvLines = @($lines | Where-Object {$_ -ceq $header -or $_ -cmatch '^v316,'})
if ($csvLines.Count -ne 2 -or $csvLines[0] -cne $header) {throw 'Missing or malformed canonical summary CSV'}
$row = @($csvLines | ConvertFrom-Csv)[0]
if ($row.mode -cne 'v316' -or $row.semantic_graph_gate -cne 'NOT_IMPLEMENTED') {throw 'Invalid canonical graph gate'}
$culture = [Globalization.CultureInfo]::InvariantCulture
foreach($field in @('full_type_fields','full_type_checked','full_type_matches','full_type_mismatches',
    'full_type_not_checked','missing_fields','no_name','operators','canonical_samples','canonical_ms')) {
    $str=[string]$row.$field
    if ([string]::IsNullOrWhiteSpace($str)) {throw "Missing metric: $field"}
    $number=[double]::Parse($str,$culture)
    if ([double]::IsNaN($number) -or [double]::IsInfinity($number) -or $number -lt 0) {throw "Invalid metric: $field"}
}
if ([long]$row.full_type_matches+[long]$row.full_type_mismatches -ne [long]$row.full_type_checked) {throw 'Canonical checked accounting invalid'}
if ([long]$row.full_type_checked+[long]$row.full_type_not_checked+[long]$row.missing_fields -ne [long]$row.full_type_fields) {throw 'Canonical full coverage accounting invalid'}
$allDetails = @($UnresolvedPath,$CallablePath,$TypeMismatchPath,$CanonicalPath,$NoNamePath,$UnresolvedTypePath,$OperatorPath,$ConstructorPath,$ConstructionPath)
foreach ($path in $allDetails) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {throw "Missing report: $path"}
    if ((Get-Content -LiteralPath $path -TotalCount 1) -cne 'reason,file_id,source_offset,source_length,record,member,category,detail') {throw "Invalid report header: $path"}
}
$unresolved=@(Import-Csv -LiteralPath $UnresolvedPath)
$callable=@(Import-Csv -LiteralPath $CallablePath)
$canonical=@(Import-Csv -LiteralPath $CanonicalPath)
$noName=@(Import-Csv -LiteralPath $NoNamePath)
$unresolvedType=@(Import-Csv -LiteralPath $UnresolvedTypePath)
$operators=@(Import-Csv -LiteralPath $OperatorPath)
$basePairs=@(Import-Csv -LiteralPath $BasePairsPath)
if ($noName.Count -ne [long]$row.no_name -or $operators.Count -ne [long]$row.operators -or $canonical.Count -ne [long]$row.canonical_samples) {throw 'Canonical/no-name report counts mismatch'}
if ((Get-Content -LiteralPath $BasePairsPath -TotalCount 1) -cne 'expected_base,actual_base,count') {throw 'Base-pair header invalid'}
$pairSum=0L
foreach ($pair in $basePairs) {$pairSum += [long]$pair.count}
$mismatchReason=@($lines | Where-Object {$_ -cmatch '^type_mismatch_reason=base_mismatch count=([0-9]+)$'})
if ($mismatchReason.Count -gt 1) {throw 'Duplicate base-mismatch count'}
$expectedPairs=0L
if ($mismatchReason.Count -eq 1) {$expectedPairs=[long]([regex]::Match($mismatchReason[0], 'count=(\d+)')).Groups[1].Value}
if ($pairSum -ne $expectedPairs) {throw 'Complete base-pair accounting mismatch'}
$unresolvedTypeReason=@($lines | Where-Object {$_ -cmatch '^type_not_checked_reason=unresolved_name count=(\d+)$'})
if ($unresolvedTypeReason.Count -gt 1) {throw 'Duplicate unresolved-name count'}
$expectedUnresolved=0L
if ($unresolvedTypeReason.Count -eq 1) {$expectedUnresolved=[long]([regex]::Match($unresolvedTypeReason[0], 'count=(\d+)')).Groups[1].Value}
if ($unresolvedType.Count -ne $expectedUnresolved) {throw 'Full unresolved-type report accounting mismatch'}
$noNameActual=@($lines | Where-Object {$_ -cmatch '^no_name_actual=(\d+)$'})
$ctorCount=@($lines | Where-Object {$_ -cmatch '^constructor_without_type=(\d+)$'})
if ($noNameActual.Count -ne 1 -or $ctorCount.Count -ne 1) {throw 'Missing constructor/no-name telemetry'}
$actualNameCount=[long]([regex]::Match($noNameActual[0], '=(\d+)')).Groups[1].Value
$actualCtorCount=[long]([regex]::Match($ctorCount[0], '=(\d+)')).Groups[1].Value
if ($actualNameCount -ne [long]$row.no_name -or $noName.Count -ne $actualNameCount) {throw 'No-name categories not separated'}
$legacyHeader='mode,effective_tokens,graph_members,matched_fields,unresolved,unresolved_fields,unresolved_callable,unresolved_other,callable,constructor_shape,method_body_shape,other_callable_shape,callable_graph_collisions,shape_checks,shape_matches,shape_mismatches,builtin_checks,builtin_matches,builtin_mismatches,builtin_not_checked,invalid_type_chains,type_examples,semantic_audit_ms,semantic_graph_gate'
$legacyLines=@($lines | Where-Object {$_ -ceq $legacyHeader -or $_ -cmatch '^v313,'})
if($legacyLines.Count -ne 2) {throw 'V3-13 baseline accounting missing'}
$legacy=@($legacyLines | ConvertFrom-Csv)[0]
if ($unresolved.Count -ne [long]$legacy.unresolved -or $callable.Count -ne [long]$legacy.callable_graph_collisions) {throw 'V3-13 report accounting mismatch'}
if ($actualCtorCount + $actualNameCount + $operators.Count -ne [long]$legacy.unresolved) {throw 'Constructor + operator + no-name count differs from V3-13 unresolved'}
$operatorMetric=@($lines | Where-Object {$_ -cmatch '^operator_declarations=(\d+)$'})
if ($operatorMetric.Count -ne 1 -or [long]([regex]::Match($operatorMetric[0], '=(\d+)')).Groups[1].Value -ne $operators.Count) {throw 'Operator report count mismatch'}
$operatorTotals=0L
foreach($line in $lines) {
    if($line -cmatch '^operator_form=\S+ count=(\d+)$') {$operatorTotals += [long]$Matches[1]}
}
if ($operatorTotals -ne $operators.Count) {throw 'Operator category accounting mismatch'}
if ($actualNameCount -eq 0 -and $marker[0] -cnotmatch 'operator_classification=PASS') {throw 'Missing operator classification PASS'}
if ($actualNameCount -gt 0 -and $marker[0] -cnotmatch 'operator_classification=PARTIAL') {throw 'False operator PASS'}
if ([long]$row.full_type_fields -ne [long]$legacy.matched_fields + [long]$row.missing_fields) {throw 'V3-13/V3-17 field identity mismatch'}
if ([long]$row.full_type_not_checked -eq 0 -and [long]$row.full_type_mismatches -eq 0 -and [long]$row.missing_fields -eq 0 -and $marker[0] -cmatch 'field_identity=PASS') {
    if ($marker[0] -cnotmatch 'semantic_type_parity=FULL_FIELD_TYPE_PASS') {throw 'Expected full type PASS marker'}
} elseif ($marker[0] -cnotmatch 'semantic_type_parity=PARTIAL') {throw 'False full type PASS marker'}
# V3-17: independent per-constructor target audit checks, never full Graph parity.
if (-not (Test-Path -LiteralPath $ConstructorSummaryPath -PathType Leaf)) {throw 'Missing constructor summary'}
$constructorRows=@(Import-Csv -LiteralPath $ConstructorSummaryPath)
$constructorHeader='mode,constructor_shaped,constructor_parsed,constructor_failed,declaration_only,raw_operations,normalized_targets,duplicate_writes,checked,matched,mismatched,not_checked,direct_targets,nested_targets,v2_nested_defaults,v2_unseen_nested_defaults,v2_nonzero_member_constructions,constructor_ms,constructor_graph_gate'
if ((Get-Content -LiteralPath $ConstructorSummaryPath -TotalCount 1) -cne $constructorHeader -or $constructorRows.Count -ne 1) {throw 'Invalid constructor summary CSV'}
$ct=$constructorRows[0]
if($ct.mode -cne 'v317' -or $ct.constructor_graph_gate -cne 'NOT_IMPLEMENTED') {throw 'Constructor Graph parity falsely reported'}
foreach($name in @('constructor_shaped','constructor_parsed','constructor_failed','declaration_only','raw_operations','normalized_targets','duplicate_writes','checked','matched','mismatched','not_checked','direct_targets','nested_targets','v2_nested_defaults','v2_unseen_nested_defaults','v2_nonzero_member_constructions','constructor_ms')) {
    $n=[double]::Parse([string]$ct.$name,$culture)
    if ([double]::IsNaN($n) -or [double]::IsInfinity($n) -or $n -lt 0) {throw "Invalid constructor metric: $name"}
}
if ([long]$ct.constructor_shaped -ne [long]$ct.constructor_parsed + [long]$ct.constructor_failed) {throw 'Incomplete constructor candidate accounting'}
if ([long]$ct.checked -ne [long]$ct.matched + [long]$ct.mismatched) {throw 'Constructor comparison accounting invalid'}
if ([long]$ct.normalized_targets -ne [long]$ct.checked + [long]$ct.not_checked) {throw 'Constructor coverage accounting invalid'}
if ([long]$ct.raw_operations -ne [long]$ct.normalized_targets + [long]$ct.duplicate_writes) {throw 'Constructor last-wins accounting invalid'}
if ([long]$ct.direct_targets + [long]$ct.nested_targets -ne [long]$ct.normalized_targets) {throw 'Constructor direct/nested coverage invalid'}
if ([long]$ct.constructor_shaped -ne [long]$legacy.constructor_shape) {throw 'V3-16/V3-17 constructor count differs'}
$constructorDetails=@(Import-Csv -LiteralPath $ConstructorPath)
if ($constructorDetails.Count -ne [long]$ct.constructor_failed + [long]$ct.mismatched + [long]$ct.not_checked + [long]$ct.v2_unseen_nested_defaults) {throw 'Incomplete constructor detailed diagnostics'}
$ctorMarker=@($lines | Where-Object {$_ -cmatch '^constructor_shaped=([0-9]+) parsed=([0-9]+) incomplete=([0-9]+) operations=([0-9]+) normalized=([0-9]+) graph_only_nested=([0-9]+) checked=([0-9]+) matches=([0-9]+) mismatches=([0-9]+) not_checked=([0-9]+)$'})
if ($ctorMarker.Count -ne 1) {throw 'Constructor console marker missing'}
if ([long]$ct.mismatched -eq 0 -and [long]$ct.not_checked -eq 0 -and [long]$ct.constructor_failed -eq 0 -and [long]$ct.v2_unseen_nested_defaults -eq 0) {
    if ($marker[0] -cnotmatch 'constructor_target_parity=PASS construction_parity=(FULL_CONSTRUCTION_PARITY_PASS|PARTIAL)$') {throw 'Expected constructor target PASS'}
} elseif ($marker[0] -cnotmatch 'constructor_target_parity=PARTIAL construction_parity=(FULL_CONSTRUCTION_PARITY_PASS|PARTIAL)$') {throw 'False constructor target PASS'}
$fs = [IO.FileStream]::new($ResultPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
    $w = [IO.StreamWriter]::new($fs,[Text.UTF8Encoding]::new($false))
    try { $w.Write(($csvLines -join [Environment]::NewLine) + [Environment]::NewLine); $w.Flush() }
    finally { $w.Dispose() }
} finally { $fs.Dispose() }
Write-Host $marker[0]
"Canonical field type checks: $($row.full_type_matches)/$($row.full_type_checked)"
"Canonical type mismatches=$($row.full_type_mismatches); NOT_CHECKED=$($row.full_type_not_checked)"
"Fields=$($row.full_type_fields); missing=$($row.missing_fields); no_name=$($row.no_name); operators=$($row.operators)"
$lines | Where-Object {$_ -cmatch '^(type_not_checked_reason|type_mismatch_reason|unresolved_reason|callable_collision_shape|constructor_without_type|operator_declarations|operator_form|no_name_actual|no_name_category|base_pair_distinct|unresolved_name_report_rows)='} | ForEach-Object {Write-Host $_}
"Summary=$ResultPath"
"CanonicalReport=$CanonicalPath"
"NoNameReport=$NoNamePath"
"UnresolvedReport=$UnresolvedPath"
"CallableReport=$CallablePath"
"TypeSamples=$TypeMismatchPath"

"UnresolvedTypeReport=$UnresolvedTypePath"
"BasePairsReport=$BasePairsPath"

"OperatorReport=$OperatorPath"
"ConstructorReport=$ConstructorPath"
"ConstructionReport=$ConstructionPath"
"ConstructionSummary=$ConstructionSummaryPath"
"ConstructorSummary=$ConstructorSummaryPath"
"Constructor target parity: $($ct.matched)/$($ct.checked) matches; failed=$($ct.constructor_failed); NOT_CHECKED=$($ct.not_checked)"
$lines | Where-Object {$_ -cmatch '^constructor_reason='} | ForEach-Object {Write-Host $_}


# V3-18 mandatory bidirectional construction coverage report.
if (-not (Test-Path -LiteralPath $ConstructionSummaryPath -PathType Leaf)) {throw 'Missing V3-18 construction summary'}
$full = @(Import-Csv -LiteralPath $ConstructionSummaryPath)
if ($full.Count -ne 1 -or $full[0].mode -cne 'v318') {throw 'Invalid V3-18 construction summary'}
$v = $full[0]
foreach ($f in @('graph_members','v3_fields','explicit_initializers','implicit_initializers',
 'failed_initializers','duplicate_fields','missing_fields','constructor_overrides','compared',
 'matched','mismatched','not_checked','v2_nonzero','v3_nonzero','nested_v2',
 'nested_mismatched','nested_not_checked')) {
  $n = [long]::Parse([string]$v.$f, [Globalization.CultureInfo]::InvariantCulture)
  if ($n -lt 0) {throw "Negative V3-18 metric $f"}
}
if ([long]$v.compared -ne ([long]$v.matched+[long]$v.mismatched)) {throw 'V3-18 compared accounting mismatch'}
if ([long]$v.graph_members -ne ([long]$v.compared+[long]$v.not_checked)) {throw 'V3-18 field coverage mismatch'}
if ([long]$v.v3_fields -ne ([long]$v.explicit_initializers+[long]$v.implicit_initializers+[long]$v.failed_initializers)) {throw 'V3-18 decoded initializer accounting mismatch'}
$pass = ($v.gate -ceq 'FULL_CONSTRUCTION_PARITY_PASS')
if ($pass -and ([long]$v.graph_members -ne [long]$v.matched -or [long]$v.mismatched -ne 0 -or
    [long]$v.not_checked -ne 0 -or [long]$v.missing_fields -ne 0 -or
    [long]$v.duplicate_fields -ne 0 -or [long]$v.failed_initializers -gt 0 -or
    [long]$v.nested_mismatched -ne 0 -or [long]$v.nested_not_checked -ne 0)) {
   throw 'False FULL_CONSTRUCTION_PARITY_PASS'
}
if ($v.gate -cnotin @('FULL_CONSTRUCTION_PARITY_PASS','PARTIAL')) {throw 'Invalid construction gate'}
if ((Get-Content -LiteralPath $ConstructionPath -TotalCount 1) -cne 'reason,file_id,source_offset,source_length,record,member,category,detail') {throw 'Invalid construction detail header'}
$constructionDetails = @(Import-Csv -LiteralPath $ConstructionPath)
if ([long]$v.mismatched -gt $constructionDetails.Count) {throw 'Missing V3-18 construction mismatch diagnostics'}
Write-Host "V3-18 construction parity: $($v.gate), fields $($v.matched)/$($v.graph_members), mismatches=$($v.mismatched), not_checked=$($v.not_checked)"

# V3-21 native Graph producer: explicit PARTIAL until all semantic domains covered.
if(-not (Test-Path -LiteralPath $GraphSummaryPath -PathType Leaf)){throw 'Missing V3-21 Graph summary'}
$gr=@(Import-Csv -LiteralPath $GraphSummaryPath)
if($gr.Count -ne 1 -or $gr[0].mode -cne 'v321'){throw 'V3-21 invalid Graph summary'}
foreach($key in @('v3_records','v3_aliases','v3_fields','v3_constructors','v3_direct_writes','v3_nested_writes','method_return_declarations','method_return_derived_created','method_no_return',
  'v3_types','v2_types','v3_members','v2_members','producer_unchecked',
  'type_matches','type_missing','extra_types','member_matches','member_mismatch',
  'construction_matches','construction_mismatch','base_matches','base_mismatch',
  'polymorphic_mismatch','nested_matches','nested_mismatch','v3_derived','v2_derived','identity_mismatch','type_where_mismatch','member_where_mismatch','derived_where_mismatch','derived_extra_v3','derived_extra_v2')){
  [void][long]::Parse([string]$gr[0].$key,[Globalization.CultureInfo]::InvariantCulture)
}
if([long]$gr[0].method_return_derived_created -gt [long]$gr[0].v3_derived){throw 'Method-derived count exceeds Graph total'}
if($gr[0].header_graph_gate -cnotin @('HEADER_GRAPH_PARITY_PASS','PARTIAL')){throw 'Invalid Header Graph gate'}
$graphZeroRequired=@('producer_unchecked','type_missing','extra_types','member_mismatch',
 'construction_mismatch','base_mismatch','polymorphic_mismatch','nested_mismatch',
 'identity_mismatch','type_where_mismatch','member_where_mismatch',
 'derived_where_mismatch','derived_extra_v3','derived_extra_v2')
if($gr[0].header_graph_gate -ceq 'HEADER_GRAPH_PARITY_PASS'){
 foreach($key in $graphZeroRequired){
  if([long]$gr[0].$key -ne 0){throw "False Header Graph PASS: $key=$($gr[0].$key)"}
 }
 if([long]$gr[0].v3_types -ne [long]$gr[0].v2_types -or
    [long]$gr[0].v3_members -ne [long]$gr[0].v2_members -or
    [long]$gr[0].v3_derived -ne [long]$gr[0].v2_derived -or
    [long]$gr[0].member_matches -ne [long]$gr[0].v2_members){
   throw 'False Header Graph PASS: totals differ'
 }
}

if($gr[0].full_graph_gate -cne 'PARTIAL'){
 throw 'Invalid V3-21 full Graph gate'
}
if((Get-Content -LiteralPath $GraphDetailsPath -TotalCount 1) -cne 'reason,file_id,source_offset,source_length,owner,symbol,detail'){
 throw 'Invalid V3-21 Graph detail header'
}
Write-Host "V3-21 method returns: $($gr[0].method_return_declarations), new derived types=$($gr[0].method_return_derived_created), constructors/destructors skipped=$($gr[0].method_no_return)"
Write-Host "V3-21 native Header Graph: $($gr[0].v3_types)/$($gr[0].v2_types) types, $($gr[0].v3_members)/$($gr[0].v2_members) members, $($gr[0].v3_producer_ms) ms; header_gate=$($gr[0].header_graph_gate); full_PUBLISH_gate=$($gr[0].full_graph_gate)"
