param(
    [string]$Source,
    [string]$InstallRoot
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $InstallRoot) { $InstallRoot = Join-Path $ProjectRoot 'dist' }
if (-not $Source) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Filter = 'NVIDIA DLSS Neural Rendering runtime (nvngx_dlssnr.dll)|nvngx_dlssnr.dll|DLL files (*.dll)|*.dll'
    $dialog.Title = 'Select your nvngx_dlssnr.dll'
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 }
    $Source = $dialog.FileName
}
$Source = (Resolve-Path $Source).Path
if ([IO.Path]::GetFileName($Source).ToLowerInvariant() -ne 'nvngx_dlssnr.dll') {
    throw 'Selected file must be named nvngx_dlssnr.dll.'
}
$RuntimeDir = Join-Path $InstallRoot 'runtime'
$Dest = Join-Path $RuntimeDir 'nvngx_dlssnr.dll'
New-Item -ItemType Directory -Force -Path $RuntimeDir | Out-Null
$hash = (Get-FileHash $Source -Algorithm SHA256).Hash
$sig = Get-AuthenticodeSignature $Source
$version = (Get-Item $Source).VersionInfo.FileVersion
Write-Host "Source:    $Source"
Write-Host "Version:   $version"
Write-Host "SHA256:    $hash"
Write-Host "Signature: $($sig.Status) / $($sig.SignerCertificate.Subject)"
if ($sig.Status -ne 'Valid') {
    Write-Warning 'The selected DLL does not have a currently valid Authenticode signature. Community-modified research runtimes may intentionally show this state; verify its provenance yourself.'
}
Copy-Item $Source $Dest -Force
Write-Host "Imported runtime: $Dest"
