param(
    [string]$Source,
    [string]$InstallRoot,
    [ValidateSet('legacy_hook','direct')][string]$CallerMode = 'legacy_hook'
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $InstallRoot) { $InstallRoot = Join-Path $ProjectRoot 'dist' }
if (-not $Source) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Filter = 'Experimental DLSSNR runtime (nvngx_dlssnr.dll)|nvngx_dlssnr.dll|DLL files (*.dll)|*.dll'
    $dialog.Title = 'Select a legally obtained RTX40-compatible nvngx_dlssnr.dll'
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 }
    $Source = $dialog.FileName
}
$Source = (Resolve-Path $Source).Path
if ([IO.Path]::GetFileName($Source).ToLowerInvariant() -ne 'nvngx_dlssnr.dll') { throw 'Selected file must be named nvngx_dlssnr.dll.' }
$RuntimeDir = Join-Path $InstallRoot 'runtime'
New-Item -ItemType Directory -Force -Path $RuntimeDir | Out-Null
$Dest = Join-Path $RuntimeDir 'nvngx_dlssnr.dll'
$hash = (Get-FileHash $Source -Algorithm SHA256).Hash
$sig = Get-AuthenticodeSignature $Source
$version = (Get-Item $Source).VersionInfo.FileVersion
Write-Host 'V0.6.6-alpha2 - RTX40 runtime import (V0.6.2 compatibility layer)' -ForegroundColor Yellow
Write-Host "Source:      $Source"
Write-Host "Version:     $version"
Write-Host "SHA256:      $hash"
Write-Host "Signature:   $($sig.Status) / $($sig.SignerCertificate.Subject)"
Write-Warning 'This branch does not bundle, download, patch, or redistribute NVIDIA runtime binaries. Verify the provenance and license of the file you selected.'
if ($sig.Status -ne 'Valid') { Write-Warning 'The selected runtime is modified/unsigned or its Authenticode signature is not currently valid. This is expected for some community research builds, but increases risk.' }
Copy-Item $Source $Dest -Force
@"
# Crow-DLSS5-Video-Image-Converter V0.6.2 (40-Series Compatibility)
# Delete this file to return to the exact V0.6.1.2 caller behavior.
profile=rtx40-community
experimental=1
caller_mode=$CallerMode
runtime_sha256=$hash
runtime_file_version=$version
"@ | Set-Content -Path (Join-Path $RuntimeDir 'dlssnr-compat.ini') -Encoding ASCII
Write-Host "Imported experimental runtime: $Dest" -ForegroundColor Green
Write-Host "Compatibility profile: caller_mode=$CallerMode" -ForegroundColor Green
Write-Host 'Next: run RTX40_RUNTIME_SELFTEST.bat before processing production images/videos.' -ForegroundColor Cyan
