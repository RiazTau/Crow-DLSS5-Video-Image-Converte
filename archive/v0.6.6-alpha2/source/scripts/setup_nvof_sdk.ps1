param([string]$SdkPath = '')
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Deps = Join-Path $Root '.deps'
$Pointer = Join-Path $Deps 'nvof-sdk.path'
New-Item -ItemType Directory -Force -Path $Deps | Out-Null

function Test-NvofSdk([string]$Path) {
    if (-not $Path -or -not (Test-Path $Path)) { return $false }
    $candidates = @(
        (Join-Path $Path 'NvOFInterface\nvOpticalFlowD3D12.h'),
        (Join-Path $Path 'include\nvOpticalFlowD3D12.h'),
        (Join-Path $Path 'nvOpticalFlowD3D12.h')
    )
    return @($candidates | Where-Object { Test-Path $_ }).Count -gt 0
}

if (-not $SdkPath) {
    Write-Host 'NVIDIA Optical Flow SDK is not redistributed by this project.' -ForegroundColor Yellow
    Write-Host 'Download/accept the NVIDIA SDK license from the official NVIDIA Developer page, extract it, then select the extracted SDK root.'
    try { Start-Process 'https://developer.nvidia.com/opticalflow/download' | Out-Null } catch {}
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select the extracted NVIDIA Optical Flow SDK 5.x root (contains NvOFInterface)'
    $dialog.ShowNewFolderButton = $false
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        Write-Warning 'No SDK folder selected. Nothing changed.'
        exit 2
    }
    $SdkPath = $dialog.SelectedPath
}

$resolved = (Resolve-Path $SdkPath).Path
if (-not (Test-NvofSdk $resolved)) {
    throw "Not a valid Optical Flow SDK root: $resolved`nExpected NvOFInterface\nvOpticalFlowD3D12.h (or equivalent include location)."
}
[IO.File]::WriteAllText($Pointer, $resolved, (New-Object Text.UTF8Encoding($false)))
$env:NVOF_SDK_DIR = $resolved
Write-Host "[OK] NVOF SDK path saved: $resolved" -ForegroundColor Green
Write-Host "Pointer file: $Pointer"
Write-Host 'Re-run AUTO_BUILD.bat or AUTO_BUILD_CN.bat. The build scripts will pass -DNVOF_SDK_DIR automatically.'
Write-Host 'V0.6.6-alpha2 uses the selected SDK 5.x headers to compile the native DirectX 12 Execute bridge. Rebuild after saving this path, then run both NVOF self-tests.' -ForegroundColor Yellow
