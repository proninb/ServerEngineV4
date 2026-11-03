param([Parameter(Mandatory=$true)][string]$RunFolder)
$ErrorActionPreference='Stop'
$run=[IO.Path]::GetFullPath($RunFolder)
if (!$run.StartsWith('C:\Boris\ServerEngineV4\build\project-unload-memory-01\lifetime-owners-03\')) { throw 'Unexpected run folder' }
$xperf='C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe'
$kernelOwned=$false
$heapOwned=$false
$logger='SEV4HeapLifetime03'
try {
    $deadline=(Get-Date).AddMinutes(3)
    while (!(Test-Path "$run\target.json")) {
        if ((Get-Date) -gt $deadline) { throw 'Target did not arrive' }
        Start-Sleep -Milliseconds 250
    }
    $target=Get-Content "$run\target.json" -Raw | ConvertFrom-Json
    $process=Get-Process -Id $target.pid
    if ($process.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $target.creation_filetime) { throw 'Process identity mismatch' }
    & $xperf -loggers *> "$run\loggers-before.txt"
    if ((Get-Content "$run\loggers-before.txt" -Raw) -match "NT Kernel Logger|$logger") { throw 'Existing logger; refusing to modify' }
    & $xperf -on BASE -BufferSize 1024 -MinBuffers 64 -MaxBuffers 64 -f "$run\kernel.etl" *> "$run\kernel-start.log"
    if ($LASTEXITCODE -ne 0) { throw 'Kernel start failed' }
    $kernelOwned=$true
    & $xperf -start $logger -heap -Pids $target.pid -BufferSize 1024 -MinBuffers 128 -MaxBuffers 128 -stackwalk HeapAlloc+HeapRealloc -f "$run\heap.etl" *> "$run\heap-start.log"
    if ($LASTEXITCODE -ne 0) { throw 'Heap start failed' }
    $heapOwned=$true
    @{pid=$target.pid;creation_filetime=$target.creation_filetime;started=$true} | ConvertTo-Json | Set-Content "$run\trace-ready.json"
    $deadline=(Get-Date).AddMinutes(15)
    while (!(Test-Path "$run\trace-done.flag")) {
        if ((Get-Date) -gt $deadline) { throw 'Bounded trace timeout' }
        if ((Get-Item "$run\heap.etl").Length -gt 8GB) { throw 'Trace exceeded 8 GiB bound' }
        Start-Sleep -Milliseconds 500
    }
} catch {
    @{error=$_.Exception.Message} | ConvertTo-Json | Set-Content "$run\trace-error.json"
} finally {
    if ($heapOwned) { & $xperf -stop $logger *> "$run\heap-stop.log"; $heapStop=$LASTEXITCODE }
    if ($kernelOwned) { & $xperf -stop 'NT Kernel Logger' *> "$run\kernel-stop.log"; $kernelStop=$LASTEXITCODE }
    if ($heapOwned -and $kernelOwned) {
        & $xperf -merge "$run\kernel.etl" "$run\heap.etl" "$run\merged.etl" *> "$run\merge.log"
        @{heap_stop=$heapStop;kernel_stop=$kernelStop;merge=$LASTEXITCODE} | ConvertTo-Json | Set-Content "$run\trace-stopped.json"
    }
}
