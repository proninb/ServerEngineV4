param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath($Directory)
if (-not $root.StartsWith('C:\Boris\ServerEngineV4\build\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Recorder output must be in workspace build/' }
$admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$wpr=Join-Path $env:SystemRoot 'System32\wpr.exe'
$instance='CWSourceCritical01'+[Guid]::NewGuid().ToString('N').Substring(0,10)
$owned=$false
$last=-1
$deadline=[DateTime]::UtcNow.AddMinutes(20)
@{administrator=$admin;pid=$PID;instance=$instance}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'recorder-ready.json')
if (-not $admin) { throw 'WPR requires elevated recorder; benchmark stays medium-integrity.' }
try {
 while ([DateTime]::UtcNow -lt $deadline) {
  $request=Join-Path $root 'request.json'
  if (-not (Test-Path -LiteralPath $request)) { Start-Sleep -Milliseconds 100; continue }
  try {$q=Get-Content -LiteralPath $request -Raw|ConvertFrom-Json} catch {Start-Sleep -Milliseconds 100;continue}
  $seq=[int]$q.seq
  if ($seq -le $last) {Start-Sleep -Milliseconds 100;continue}
  $last=$seq
  if ($seq -lt 0 -or $seq -gt 100) {throw 'Invalid request sequence'}
  $block=[int]$q.block
  if ($block -lt 0 -or $block -gt 16) {throw 'Invalid capture block'}
  $status=0
  switch ([string]$q.action) {
   'start' {
    if ($owned) {throw 'Recorder already owns a session'}
    & $wpr -start CPU -filemode -recordtempto $root -instancename $instance *> (Join-Path $root "b$block-wpr-start.txt")
    $status=$LASTEXITCODE
    if ($status -eq 0) {$owned=$true}
   }
   'stop' {
    if (-not $owned) {throw 'Recorder owns no session'}
    & $wpr -stop (Join-Path $root "b$block.etl") -instancename $instance *> (Join-Path $root "b$block-wpr-stop.txt")
    $status=$LASTEXITCODE
    if ($status -eq 0) {$owned=$false}
   }
   'finish' {
    if ($owned) {throw 'Stop own session before finish'}
    @{seq=$seq;action='finish';exit_code=0}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'response.json')
    return
   }
   default {throw 'Unknown recorder action'}
  }
  @{seq=$seq;action=$q.action;exit_code=$status;owned=$owned}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'response.json')
  if ($status -ne 0) {throw "WPR failed with $status"}
 }
 throw 'Recorder session timed out'
} catch {
 @{error=$_.Exception.Message;seq=$last;owned=$owned}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $root 'recorder-error.json')
} finally {
 if ($owned) {
  & $wpr -stop (Join-Path $root 'recovered.etl') -instancename $instance *> (Join-Path $root 'recovery-stop.txt')
 }
}
