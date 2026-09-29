param(
    [string]$Root = ''
)

$ErrorActionPreference = 'SilentlyContinue'
if (-not $Root) { $Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path }
$logDir = Join-Path $Root 'logs'
if (-not (Test-Path -LiteralPath $logDir)) { exit 0 }

$latest = Get-ChildItem -LiteralPath $logDir -File |
    Where-Object { $_.Name -like 'auto-build*.log' -or $_.Name -like 'cmake-build*.log' -or $_.Name -like 'cmake-configure*.log' } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if ($latest) {
    Write-Host ('Opening latest build diagnostic log: ' + $latest.FullName) -ForegroundColor Yellow
    Start-Process -FilePath 'notepad.exe' -ArgumentList @($latest.FullName) | Out-Null
}
exit 0
