param([switch]$NoZip)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Write-Host 'Crow - DLSS Rendering Tool V0.7.3-alpha1 - Portable Build' -ForegroundColor Cyan
Write-Host 'Phase 0: validate mandatory build prerequisites.'
& (Join-Path $Root 'scripts\setup_nvof_sdk.ps1') -Required -PreferSaved

$Runtime = Join-Path $Root 'dist\runtime\nvngx_dlssnr.dll'
if (-not (Test-Path $Runtime)) {
    Write-Host 'A DLSSNR runtime is required for the portable package. Select your validated nvngx_dlssnr.dll now; it will only be copied locally and is never auto-downloaded by this project.' -ForegroundColor Yellow
    & (Join-Path $Root 'scripts\import_runtime.ps1')
    if (-not (Test-Path $Runtime)) { throw 'DLSSNR runtime import was cancelled or failed.' }
}

Write-Host 'Phase 1: refresh Release build + dependencies.'
& (Join-Path $Root 'scripts\auto_build.ps1') -Portable -SkipRuntimePrompt
if ($LASTEXITCODE -ne 0) { throw "AUTO_BUILD portable preparation failed with exit code $LASTEXITCODE" }

Write-Host 'Phase 1b: require DLSS-G runtime for unified/FG processing.' -ForegroundColor Cyan
& (Join-Path $Root 'scripts\setup_fg_runtime.ps1') -Required
if ($LASTEXITCODE -ne 0) { throw "DLSS-G runtime staging failed with exit code $LASTEXITCODE" }
$DlssgRuntime = Join-Path $Root 'dist\runtime\nvngx_dlssg.dll'
if (-not (Test-Path $DlssgRuntime)) { throw "Required DLSS-G runtime is missing: $DlssgRuntime" }

Write-Host 'Phase 2: create relocatable portable tree and Zip64 archive.'
& (Join-Path $Root 'scripts\make_portable.ps1') -NoZip:$NoZip
if ($LASTEXITCODE -ne 0) { throw "Portable packaging failed with exit code $LASTEXITCODE" }
