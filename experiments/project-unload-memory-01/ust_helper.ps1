param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference = 'Stop'
$rootPath = [IO.Path]::GetFullPath($Root)
$expected = 'C:\Boris\ServerEngineV4\build\project-unload-memory-01'
if ($rootPath -ne $expected) { throw 'Unexpected diagnostic workspace' }
$keyPath = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\ServerEngineV4UnloadDiag01.exe'
if (Test-Path -LiteralPath $keyPath) { throw 'Diagnostic IFEO entry already exists; refusing to overwrite' }
@{ existed=$false; image='ServerEngineV4UnloadDiag01.exe'; flag='UST only, 0x1000' } | ConvertTo-Json | Set-Content -LiteralPath "$rootPath\ust-backup.json"
try {
    New-Item -Path $keyPath -Force | Out-Null
    New-ItemProperty -LiteralPath $keyPath -Name GlobalFlag -PropertyType String -Value '0x00001000' | Out-Null
    @{ enabled=$true; scope='unique diagnostic image only' } | ConvertTo-Json | Set-Content -LiteralPath "$rootPath\ust-ready.json"
    $deadline = (Get-Date).AddMinutes(20)
    while (!(Test-Path -LiteralPath "$rootPath\ust-done.flag") -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
} finally {
    if (Test-Path -LiteralPath $keyPath) {
        Remove-ItemProperty -LiteralPath $keyPath -Name GlobalFlag -ErrorAction SilentlyContinue
        $item = Get-Item -LiteralPath $keyPath
        if ($item.GetValueNames().Count -eq 0 -and $item.SubKeyCount -eq 0) { Remove-Item -LiteralPath $keyPath }
    }
    @{ restored=(!(Test-Path -LiteralPath $keyPath)); image='ServerEngineV4UnloadDiag01.exe' } | ConvertTo-Json | Set-Content -LiteralPath "$rootPath\ust-restored.json"
}
