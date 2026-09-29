param(
    [switch]$ForceRedownload,
    [ValidateRange(1,10)][int]$Retries = 3
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Deps = Join-Path $Root '.deps'
$Downloads = Join-Path $Deps 'downloads'
$Destination = Join-Path $Deps 'NVIDIA-nvapi'
$Stage = Join-Path $Deps ('NVIDIA-nvapi.refresh.' + [Guid]::NewGuid().ToString('N'))
$Backup = Join-Path $Deps ('NVIDIA-nvapi.backup.' + [Guid]::NewGuid().ToString('N'))
$Repository = 'https://github.com/NVIDIA/nvapi.git'
$ArchiveUrl = 'https://codeload.github.com/NVIDIA/nvapi/zip/refs/heads/main'

function Test-NvapiSdk([string]$Path) {
    if (-not $Path) { return $false }
    return (Test-Path (Join-Path $Path 'nvapi.h')) -and
           (Test-Path (Join-Path $Path 'NvApiDriverSettings.h')) -and
           (Test-Path (Join-Path $Path 'amd64\nvapi64.lib'))
}

function Remove-Safe([string]$Path) {
    if ($Path -and (Test-Path $Path)) { Remove-Item -Recurse -Force $Path }
}

function Try-GitClone {
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $git) { Write-Warning 'git.exe not found; skipping clone and trying NVIDIA archive download.'; return $false }
    for ($i=1; $i -le $Retries; $i++) {
        Remove-Safe $Stage
        Write-Host "NVAPI git clone attempt $i/$Retries ..." -ForegroundColor Cyan
        & $git.Source -c http.version=HTTP/1.1 clone --depth 1 $Repository $Stage
        if ($LASTEXITCODE -eq 0 -and (Test-NvapiSdk $Stage)) { return $true }
        Write-Warning 'Clone failed or downloaded SDK did not pass validation.'
        Remove-Safe $Stage
        if ($i -lt $Retries) { Start-Sleep -Seconds ([Math]::Min(2*$i,6)) }
    }
    return $false
}

function Try-ArchiveDownload {
    New-Item -ItemType Directory -Force -Path $Downloads | Out-Null
    $zip = Join-Path $Downloads ('nvapi-' + [Guid]::NewGuid().ToString('N') + '.zip')
    $extract = Join-Path $Downloads ('nvapi-' + [Guid]::NewGuid().ToString('N'))
    try {
        Write-Host 'Trying NVIDIA GitHub source archive fallback...' -ForegroundColor Cyan
        Invoke-WebRequest -Uri $ArchiveUrl -OutFile $zip -UseBasicParsing
        New-Item -ItemType Directory -Force -Path $extract | Out-Null
        Expand-Archive -LiteralPath $zip -DestinationPath $extract -Force
        $rootDir = Get-ChildItem -Path $extract -Directory | Select-Object -First 1
        if (-not $rootDir) { throw 'NVAPI archive contained no source directory.' }
        Remove-Safe $Stage
        Move-Item -LiteralPath $rootDir.FullName -Destination $Stage
        if (-not (Test-NvapiSdk $Stage)) { throw 'Downloaded NVAPI archive is incomplete.' }
        return $true
    } catch {
        Write-Warning ('Archive fallback failed: ' + $_.Exception.Message)
        Remove-Safe $Stage
        return $false
    } finally {
        Remove-Item -Force $zip -ErrorAction SilentlyContinue
        Remove-Safe $extract
    }
}

New-Item -ItemType Directory -Force -Path $Deps,$Downloads | Out-Null
$existingValid = Test-NvapiSdk $Destination
if ($existingValid -and -not $ForceRedownload) {
    Write-Host "NVAPI SDK is already valid: $Destination" -ForegroundColor Green
    exit 0
}

Write-Host '============================================================'
Write-Host ' Crow - NVIDIA NVAPI SDK refresh'
Write-Host '============================================================'
Write-Host "Destination : $Destination"
Write-Host 'Required    : nvapi.h, NvApiDriverSettings.h, amd64\nvapi64.lib'
Write-Host 'Existing SDK is not deleted until the replacement is fully validated.'
Write-Host ''

$ok = Try-GitClone
if (-not $ok) { $ok = Try-ArchiveDownload }
if (-not $ok -or -not (Test-NvapiSdk $Stage)) {
    Remove-Safe $Stage
    Write-Host ''
    Write-Error @"
Unable to re-download a valid NVIDIA NVAPI SDK from the official NVIDIA GitHub source.
Any previously valid .deps\NVIDIA-nvapi directory was preserved.

Manual fallback:
1. Download https://github.com/NVIDIA/nvapi on a network that can access GitHub.
2. Extract/copy it to:
   $Destination
3. Verify these files exist:
   nvapi.h
   NvApiDriverSettings.h
   amd64\nvapi64.lib
4. Re-run BUILD.bat and rebuild Crow.
"@
    exit 1
}

$hadExisting = Test-Path $Destination
try {
    if ($hadExisting) { Move-Item -LiteralPath $Destination -Destination $Backup }
    Move-Item -LiteralPath $Stage -Destination $Destination
    if (-not (Test-NvapiSdk $Destination)) { throw 'Installed replacement failed final validation.' }
    Remove-Safe $Backup
} catch {
    Remove-Safe $Destination
    if (Test-Path $Backup) { Move-Item -LiteralPath $Backup -Destination $Destination }
    Remove-Safe $Stage
    Write-Error ('NVAPI refresh installation failed; previous SDK restored. ' + $_.Exception.Message)
    exit 1
}

Write-Host ''
Write-Host '[OK] NVIDIA NVAPI SDK downloaded and validated.' -ForegroundColor Green
Write-Host "     $Destination"
Write-Host 'Re-run a Crow build to compile FG Preset A / B / Latest support.' -ForegroundColor Cyan
exit 0
