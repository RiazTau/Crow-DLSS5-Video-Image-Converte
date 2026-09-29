param(
    [string]$NgxSdkDir = '',
    [switch]$ChinaMirror,
    [switch]$Required
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Deps = Join-Path $Root '.deps'
$Dist = Join-Path $Root 'dist'
$Runtime = Join-Path $Dist 'runtime'
New-Item -ItemType Directory -Force -Path $Deps,$Runtime | Out-Null

function Get-DlssgCandidate([string]$SdkRoot) {
    if (-not $SdkRoot -or -not (Test-Path -LiteralPath $SdkRoot)) { return $null }
    $candidates = @(
        (Join-Path $SdkRoot 'lib\Windows_x86_64\rel\nvngx_dlssg.dll'),
        (Join-Path $SdkRoot 'lib\Windows_x86_64\dev\nvngx_dlssg.dll')
    )
    return ($candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1)
}

function Acquire-DlssSdk([string]$SdkRoot) {
    if (Test-Path -LiteralPath $SdkRoot) { return }
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $git) {
        Write-Warning 'Git is unavailable, so DLSS-G runtime cannot be auto-acquired from NVIDIA/DLSS.'
        return
    }
    $uri = if ($ChinaMirror) { 'https://gitee.com/mirrors_NVIDIA/DLSS.git' } else { 'https://github.com/NVIDIA/DLSS.git' }
    Write-Host ''
    Write-Host '=== Required DLSS-G runtime acquisition ===' -ForegroundColor Cyan
    Write-Host "DLSS-G runtime is not present locally. Acquiring NVIDIA/DLSS from: $uri"
    try {
        & $git.Source -c http.version=HTTP/1.1 clone --depth 1 $uri $SdkRoot
        if ($LASTEXITCODE -ne 0) { throw "git clone exited with code $LASTEXITCODE" }
    } catch {
        Write-Warning ('Automatic NVIDIA/DLSS acquisition failed: ' + $_.Exception.Message)
    }
}

function Select-DlssgRuntime {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Filter = 'NVIDIA DLSS Frame Generation runtime (nvngx_dlssg.dll)|nvngx_dlssg.dll|DLL files (*.dll)|*.dll'
        $dialog.Title = 'REQUIRED: Select nvngx_dlssg.dll for Crow - DLSS Rendering Tool'
        $dialog.InitialDirectory = $Root
        $dialog.CheckFileExists = $true
        $dialog.Multiselect = $false
        if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { return $null }
        if ([IO.Path]::GetFileName($dialog.FileName).ToLowerInvariant() -ne 'nvngx_dlssg.dll') {
            throw 'The selected file must be named nvngx_dlssg.dll.'
        }
        return $dialog.FileName
    } catch {
        if ($Required) { throw }
        Write-Warning ('DLSS-G runtime picker failed: ' + $_.Exception.Message)
        return $null
    }
}

$Sdk = ''
if ($NgxSdkDir) {
    $Sdk = (Resolve-Path -LiteralPath $NgxSdkDir).Path
} else {
    $Sdk = Join-Path $Deps 'NVIDIA-DLSS'
    if (-not (Test-Path -LiteralPath $Sdk)) { Acquire-DlssSdk $Sdk }
}

$Source = Get-DlssgCandidate $Sdk
if (-not $Source) {
    Write-Warning 'Official nvngx_dlssg.dll was not found in the acquired NVIDIA/DLSS SDK.'
    Write-Host 'A DLSS-G runtime is mandatory for Unified NR + FG and FG Diagnostic builds.' -ForegroundColor Yellow
    Write-Host 'Select a trusted nvngx_dlssg.dll now.' -ForegroundColor Yellow
    $Source = Select-DlssgRuntime
}

if (-not $Source) {
    $expected = @(
        (Join-Path $Sdk 'lib\Windows_x86_64\rel\nvngx_dlssg.dll'),
        (Join-Path $Sdk 'lib\Windows_x86_64\dev\nvngx_dlssg.dll')
    )
    throw @"
Required NVIDIA DLSS Frame Generation runtime was not provided.
Expected SDK locations:
  $($expected -join "`n  ")

The build cannot produce a functional FG package without nvngx_dlssg.dll.
"@
}

New-Item -ItemType Directory -Force -Path $Runtime | Out-Null
$Destination = Join-Path $Runtime 'nvngx_dlssg.dll'
Copy-Item -Force -LiteralPath $Source -Destination $Destination

$item = Get-Item -LiteralPath $Destination
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash
$sig = Get-AuthenticodeSignature -LiteralPath $Destination
Write-Host '[OK] Required DLSS-G runtime staged.' -ForegroundColor Green
Write-Host "Source : $Source"
Write-Host "Target : $Destination"
Write-Host "Version: $($item.VersionInfo.FileVersion)"
Write-Host "SHA256 : $hash"
Write-Host "Signer : $($sig.SignerCertificate.Subject)"
Write-Host "Status : $($sig.Status)"
if ($sig.Status -ne 'Valid') {
    Write-Warning 'DLSS-G runtime does not currently report a valid Authenticode signature. Verify provenance before distribution.'
}
