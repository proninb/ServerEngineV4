# Use dedicated benchmark fixtures: PUBLISH replaces their persisted artifacts.
param(
    [Parameter(Mandatory = $true)][string]$BaselineExe,
    [Parameter(Mandatory = $true)][string]$CandidateExe,
    [Parameter(Mandatory = $true)][string]$Project,
    [Parameter(Mandatory = $true)][ValidateRange(0, 2147483647)][int]$ExpectedTypes,
    [Parameter(Mandatory = $true)][string]$ResultPath,
    [ValidateRange(1, 101)][int]$Runs = 7
)
$ErrorActionPreference = 'Stop'
$compareExecutables = @{
    baseline = (Resolve-Path -LiteralPath $BaselineExe).Path
    candidate = (Resolve-Path -LiteralPath $CandidateExe).Path
}
$compareProject = (Resolve-Path -LiteralPath $Project).Path
$compareArtifact = Join-Path (Split-Path -Parent $compareProject) (
    '.serverengine/' + (Split-Path -Leaf $compareProject) + '/compiled.bin')
$compareResult =
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath(
        $ResultPath)
if (Test-Path -LiteralPath $compareResult) { throw "Result file must be new: $compareResult" }
[IO.Directory]::CreateDirectory((Split-Path -Parent $compareResult)) | Out-Null
$compareRows = [Collections.Generic.List[object]]::new()
$compareHash = $null
$compareCulture = [Globalization.CultureInfo]::InvariantCulture
foreach ($compareRun in 0..$Runs) {
    $compareOrder = @('baseline', 'candidate')
    if ($compareRun % 2 -eq 0 -and $compareRun -gt 0) { [array]::Reverse($compareOrder) }
    foreach ($compareVariant in $compareOrder) {
        $compareExe = $compareExecutables[$compareVariant]
        $compareOutput = & $compareExe publish $compareProject $ExpectedTypes
        if ($LASTEXITCODE -ne 0) { throw "PUBLISH failed: $compareVariant" }
        $compareFields = @{}
        foreach ($compareField in ($compareOutput -split ',')) {
            $comparePair = $compareField -split '=', 2
            if ($comparePair.Count -eq 2) { $compareFields[$comparePair[0]] = $comparePair[1] }
        }
        if (!$compareFields.ContainsKey('total_ms') -or !$compareFields.ContainsKey('peak_ws_bytes')) {
            throw "Missing benchmark metrics: $compareOutput"
        }
        # Audit with the baseline executable so the candidate cannot validate
        # its own changed format or accidentally relaxed audit implementation.
        & $compareExecutables.baseline audit $compareProject $ExpectedTypes | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Cold audit failed: $compareVariant" }
        $compareCurrentHash = (Get-FileHash -LiteralPath $compareArtifact -Algorithm SHA256).Hash
        if ($null -eq $compareHash) { $compareHash = $compareCurrentHash }
        if ($compareCurrentHash -ne $compareHash) { throw "Artifact bytes differ: $compareVariant run $compareRun" }
        if ($compareRun -eq 0) { continue }
        $compareRow = [pscustomobject][ordered]@{
            variant = $compareVariant
            run = $compareRun
            total_ms = [double]::Parse($compareFields.total_ms, $compareCulture)
            peak_ws_bytes = [long]::Parse($compareFields.peak_ws_bytes, $compareCulture)
            sha256 = $compareCurrentHash
            metrics = $compareOutput
        }
        $compareRows.Add($compareRow)
        $compareRows | Export-Csv -LiteralPath $compareResult -NoTypeInformation
        Write-Output "variant=$compareVariant,run=$compareRun,$compareOutput"
    }
}
foreach ($compareVariant in @('baseline', 'candidate')) {
    $compareTimes = @($compareRows | Where-Object variant -eq $compareVariant | Sort-Object total_ms | ForEach-Object total_ms)
    $compareMiddle = [int][Math]::Floor($Runs / 2)
    $compareMedian = $compareTimes[$compareMiddle]
    if ($Runs % 2 -eq 0) { $compareMedian = ($compareMedian + $compareTimes[$compareMiddle - 1]) / 2 }
    Write-Output "variant=$compareVariant,median_ms=$compareMedian"
}
Write-Output "results=$compareResult"
