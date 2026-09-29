param(
    [string]$SdkPath = '',
    [switch]$Required,
    [switch]$PreferSaved
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Deps = Join-Path $Root '.deps'
$Pointer = Join-Path $Deps 'nvof-sdk.path'
New-Item -ItemType Directory -Force -Path $Deps | Out-Null

function Test-NvofSdk([string]$Path) {
    if (-not $Path -or -not (Test-Path -LiteralPath $Path)) { return $false }
    $candidates = @(
        (Join-Path $Path 'NvOFInterface\nvOpticalFlowD3D12.h'),
        (Join-Path $Path 'include\nvOpticalFlowD3D12.h'),
        (Join-Path $Path 'nvOpticalFlowD3D12.h')
    )
    return @($candidates | Where-Object { Test-Path -LiteralPath $_ }).Count -gt 0
}

function Get-SavedNvofPath {
    if (-not (Test-Path -LiteralPath $Pointer)) { return '' }
    try {
        $candidate = (Get-Content -LiteralPath $Pointer -Raw).Trim()
        if (Test-NvofSdk $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    } catch {}
    return ''
}

if (-not $SdkPath -and $PreferSaved) {
    $saved = Get-SavedNvofPath
    if ($saved) {
        $SdkPath = $saved
        Write-Host '[OK] Mandatory NVOF prerequisite already configured.' -ForegroundColor Green
        Write-Host "NVOF SDK: $SdkPath"
    }
}

if (-not $SdkPath) {
    Write-Host ''
    Write-Host '=== Required NVIDIA Optical Flow SDK 5.x ===' -ForegroundColor Cyan
    Write-Host 'NVOF is required for the unified NR/FG build and is not redistributed by this project.' -ForegroundColor Yellow
    Write-Host 'Select the extracted NVIDIA Optical Flow SDK 5.x root (the folder containing NvOFInterface).'
    Write-Host 'If you do not have it yet, the official NVIDIA download page will be opened.'
    try { Start-Process 'https://developer.nvidia.com/opticalflow/download' | Out-Null } catch {}

    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'REQUIRED: Select NVIDIA Optical Flow SDK 5.x root (contains NvOFInterface)'
    $dialog.ShowNewFolderButton = $false
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        if ($Required) {
            throw 'NVIDIA Optical Flow SDK selection is required for this build. Build cancelled.'
        }
        Write-Warning 'No SDK folder selected. Nothing changed.'
        return
    }
    $SdkPath = $dialog.SelectedPath
}

$resolved = (Resolve-Path -LiteralPath $SdkPath).Path
if (-not (Test-NvofSdk $resolved)) {
    throw "Not a valid Optical Flow SDK root: $resolved`nExpected NvOFInterface\nvOpticalFlowD3D12.h (or equivalent include location)."
}

[IO.File]::WriteAllText($Pointer, $resolved, (New-Object Text.UTF8Encoding($false)))
$env:NVOF_SDK_DIR = $resolved
Write-Host "[OK] NVOF SDK prerequisite ready: $resolved" -ForegroundColor Green
Write-Host "Pointer file: $Pointer"
