param([switch]$NoZip)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$NvofPointer = Join-Path $Root '.deps\nvof-sdk.path'
if (-not $env:NVOF_SDK_DIR -and -not (Test-Path $NvofPointer)) {
    throw 'NVIDIA Optical Flow SDK is not configured for this alpha2 build. Run NVOF_SDK_SETUP.bat first; the SDK is build-time only and will not be copied into the portable package.'
}
$Runtime = Join-Path $Root 'dist\runtime\nvngx_dlssnr.dll'
if (-not (Test-Path $Runtime)) {
    Write-Host 'A DLSSNR runtime is required for the portable package. Select your validated nvngx_dlssnr.dll now; it will only be copied locally and is never downloaded by this project.' -ForegroundColor Yellow
    & (Join-Path $Root 'scripts\import_runtime.ps1')
    if (-not (Test-Path $Runtime)) { throw 'DLSSNR runtime import was cancelled or failed.' }
}
Write-Host 'Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Portable CN Build' -ForegroundColor Cyan
Write-Host 'Phase 1: refresh Release build + FFmpeg + AutoDepth/model through mainland mirrors.'
& (Join-Path $Root 'scripts\auto_build_cn.ps1') -Portable -SkipRuntimePrompt
if ($LASTEXITCODE -ne 0) { throw "AUTO_BUILD_CN portable preparation failed with exit code $LASTEXITCODE" }
Write-Host 'Phase 2: create relocatable portable tree and Zip64 archive.'
& (Join-Path $Root 'scripts\make_portable.ps1') -ChinaMirror -NoZip:$NoZip
if ($LASTEXITCODE -ne 0) { throw "Portable packaging failed with exit code $LASTEXITCODE" }
