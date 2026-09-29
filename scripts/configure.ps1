param(
    [string]$Configuration = "Release",
    [string]$NgxSdkDir = "",
    [string]$TinyExrDir = "",
    [string]$NvofSdkDir = "",
    [string]$NvapiSdkDir = ""
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Ngx = if ($NgxSdkDir) { (Resolve-Path $NgxSdkDir).Path } else { Join-Path $Root ".deps\NVIDIA-DLSS" }
$Tiny = if ($TinyExrDir) { (Resolve-Path $TinyExrDir).Path } else { Join-Path $Root ".deps\tinyexr" }
$Nvapi = if ($NvapiSdkDir) { (Resolve-Path $NvapiSdkDir).Path } else { Join-Path $Root ".deps\NVIDIA-nvapi" }

$Nvof = ''
if ($NvofSdkDir) {
    $Nvof = (Resolve-Path $NvofSdkDir).Path
} elseif ($env:NVOF_SDK_DIR -and (Test-Path $env:NVOF_SDK_DIR)) {
    $Nvof = (Resolve-Path $env:NVOF_SDK_DIR).Path
} else {
    $nvofPointer = Join-Path $Root '.deps\nvof-sdk.path'
    if (Test-Path $nvofPointer) {
        $candidate = (Get-Content $nvofPointer -Raw).Trim()
        if ($candidate -and (Test-Path $candidate)) { $Nvof = (Resolve-Path $candidate).Path }
    }
}
if ($Nvof) { Write-Host "Using NVIDIA Optical Flow SDK: $Nvof" }
else { throw 'NVIDIA Optical Flow SDK 5.x is required. Launch BUILD.bat and complete the mandatory NVOF prerequisite step.' }


if (-not (Test-Path (Join-Path $Ngx "include\nvsdk_ngx.h"))) {
    throw "NGX SDK is missing or incomplete: $Ngx. Run .\scripts\build.ps1 first."
}
if (-not (Test-Path (Join-Path $Tiny "tinyexr.h"))) {
    throw "TinyEXR is missing or incomplete: $Tiny. Run .\scripts\build.ps1 first."
}
$NvapiEnabled = (Test-Path (Join-Path $Nvapi "nvapi.h")) -and
                (Test-Path (Join-Path $Nvapi "NvApiDriverSettings.h")) -and
                (Test-Path (Join-Path $Nvapi "amd64\nvapi64.lib"))
if (-not $NvapiEnabled) {
    Write-Warning "NVAPI SDK is missing/incomplete: $Nvapi. Continuing without optional FG model preset overrides."
}

$Build = Join-Path $Root "build"
$cmakeArgs = @(
    "-S", $Root,
    "-B", $Build,
    "-A", "x64",
    "-DNGX_SDK_DIR=$Ngx",
    "-DTINYEXR_DIR=$Tiny"
)
if ($NvapiEnabled) { $cmakeArgs += "-DNVAPI_SDK_DIR=$Nvapi" }
if ($Nvof) { $cmakeArgs += "-DNVOF_SDK_DIR=$Nvof" }
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
