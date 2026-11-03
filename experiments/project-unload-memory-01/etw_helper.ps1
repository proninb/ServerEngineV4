param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference = 'Stop'
$rootPath = [IO.Path]::GetFullPath($Root)
if ($rootPath -ne 'C:\Boris\ServerEngineV4\build\project-unload-memory-01') { throw 'Unexpected diagnostic root' }
$xperf = 'C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe'
$owned = $false
try {
    & $xperf -loggers *> "$rootPath\etw-before-loggers.txt"
    $existing = Get-Content -LiteralPath "$rootPath\etw-before-loggers.txt" -Raw
    if ($existing -match 'NT Kernel Logger') { throw 'Existing kernel logger: refusing to touch it' }
    & $xperf -on PROC_THREAD+LOADER+VIRT_ALLOC -stackwalk VirtualAlloc+VirtualFree -BufferSize 1024 -MinBuffers 64 -MaxBuffers 128 -f "$rootPath\memory-raw.etl" *> "$rootPath\etw-start.log"
    if ($LASTEXITCODE -ne 0) { throw "xperf start failed: $LASTEXITCODE" }
    $owned = $true
    @{ started=$true; logger='NT Kernel Logger'; owns=$true } | ConvertTo-Json | Set-Content -LiteralPath "$rootPath\etw-ready.json"
    $deadline = (Get-Date).AddMinutes(8)
    while (!(Test-Path -LiteralPath "$rootPath\etw-done.flag") -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
} catch {
    @{ error=$_.Exception.Message } | ConvertTo-Json | Set-Content -LiteralPath "$rootPath\etw-error.json"
} finally {
    if ($owned) {
        & $xperf -stop 'NT Kernel Logger' -d "$rootPath\memory.etl" *> "$rootPath\etw-stop.log"
        @{ exit_code=$LASTEXITCODE; owned=$true; stopped=($LASTEXITCODE -eq 0) } | ConvertTo-Json | Set-Content -LiteralPath "$rootPath\etw-stopped.json"
    }
}
