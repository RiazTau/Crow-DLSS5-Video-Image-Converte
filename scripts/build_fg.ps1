param(
    [switch]$Clean,
    [string]$Configuration = 'Release',
    [string]$NgxSdkDir = '',
    [string]$TinyExrDir = '',
    [string]$NvofSdkDir = ''
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

# IMPORTANT: Use hashtable splatting for script-to-script named parameters.
# Array splatting is positional in PowerShell; the original alpha1 wrapper used
# @('-Configuration','Release','-Clean'), which could bind 'Release' to NgxSdkDir
# and make build.ps1 try to Resolve-Path <project>\Release.
$buildParams = @{
    Configuration = $Configuration
}
if ($Clean) { $buildParams['Clean'] = $true }
if ($NgxSdkDir) { $buildParams['NgxSdkDir'] = $NgxSdkDir }
if ($TinyExrDir) { $buildParams['TinyExrDir'] = $TinyExrDir }
if ($NvofSdkDir) { $buildParams['NvofSdkDir'] = $NvofSdkDir }

Write-Host '=== Crow - DLSS Rendering Tool FG Diagnostic V0.7.3-alpha1 ===' -ForegroundColor Cyan
Write-Host 'Building standalone 2X DLSS Frame Generation EXE...'
& (Join-Path $PSScriptRoot 'setup_nvof_sdk.ps1') -Required -PreferSaved
& (Join-Path $PSScriptRoot 'build.ps1') @buildParams
if (-not $?) { throw 'Base build script failed.' }

$runtimeParams = @{}
if ($NgxSdkDir) { $runtimeParams['NgxSdkDir'] = $NgxSdkDir }
& (Join-Path $PSScriptRoot 'setup_fg_runtime.ps1') @runtimeParams -Required
if (-not $?) { throw 'DLSS-G runtime staging failed.' }

$videoSetup = Join-Path $Root 'dist\video\setup_video.ps1'
if (Test-Path $videoSetup) {
    Write-Host 'Checking FFmpeg dependency...'
    try {
        & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $videoSetup
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "FFmpeg setup exited with code $LASTEXITCODE. You can re-run dist\video\setup_video.ps1 manually."
        }
    } catch {
        Write-Warning ('FFmpeg setup did not complete automatically: ' + $_.Exception.Message)
    }
}

$fg = Join-Path $Root 'dist\tools\Crow-DLSS-Rendering-Tool-FG-Diagnostic.exe'
if (-not (Test-Path $fg)) { throw "FG executable missing after build: $fg" }

Write-Host ''
Write-Host '[READY] Standalone FG executable:' -ForegroundColor Green
Write-Host "  $fg"
Write-Host 'Use Check Runtime in the UI before the first conversion.'
