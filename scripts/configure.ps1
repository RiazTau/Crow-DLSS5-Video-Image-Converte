param(
    [string]$Configuration = "Release",
    [string]$NgxSdkDir = "",
    [string]$TinyExrDir = "",
    [string]$NvofSdkDir = ""
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Ngx = if ($NgxSdkDir) { (Resolve-Path $NgxSdkDir).Path } else { Join-Path $Root ".deps\NVIDIA-DLSS" }
$Tiny = if ($TinyExrDir) { (Resolve-Path $TinyExrDir).Path } else { Join-Path $Root ".deps\tinyexr" }

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
else { Write-Host 'NVOF SDK not configured; NVOF runtime probe/foundation will still build.' -ForegroundColor Yellow }


if (-not (Test-Path (Join-Path $Ngx "include\nvsdk_ngx.h"))) {
    throw "NGX SDK is missing or incomplete: $Ngx. Run .\scripts\build.ps1 first."
}
if (-not (Test-Path (Join-Path $Tiny "tinyexr.h"))) {
    throw "TinyEXR is missing or incomplete: $Tiny. Run .\scripts\build.ps1 first."
}

$Build = Join-Path $Root "build"
$cmakeArgs = @(
    "-S", $Root,
    "-B", $Build,
    "-A", "x64",
    "-DNGX_SDK_DIR=$Ngx",
    "-DTINYEXR_DIR=$Tiny"
)
if ($Nvof) { $cmakeArgs += "-DNVOF_SDK_DIR=$Nvof" }
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed with exit code $LASTEXITCODE" }
