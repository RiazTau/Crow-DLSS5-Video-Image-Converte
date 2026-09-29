param(
    [switch]$Clean,
    [string]$Configuration = 'Release',
    [string]$NgxSdkDir = '',
    [string]$TinyExrDir = '',
    [string]$NvofSdkDir = '',
    [ValidateRange(1,10)][int]$Retries = 3
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

$buildCn = Join-Path $PSScriptRoot 'build_cn.ps1'
$mirrorConfig = Join-Path $PSScriptRoot 'cn_mirrors.ps1'
$runtimeSetup = Join-Path $PSScriptRoot 'setup_fg_runtime.ps1'
$videoSetup = Join-Path $Root 'video\setup_video_cn.ps1'

foreach ($required in @($buildCn, $mirrorConfig, $runtimeSetup)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required project script is missing: $required`nUse the current source tree and launch FG diagnostic builds through the root BUILD.bat."
    }
}

. $mirrorConfig

Write-Host '=== Crow - DLSS Rendering Tool FG Diagnostic V0.7.3-alpha1 - CN Mirror Build ===' -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'setup_nvof_sdk.ps1') -Required -PreferSaved
Write-Host "Project root   : $Root"
Write-Host 'Dependency policy:'
Write-Host "  DLSS/NGX : $($CNMirrors.NgxGit)"
Write-Host "  TinyEXR  : $($CNMirrors.TinyExrGit)"
Write-Host "  FFmpeg   : $($CNMirrors.FfmpegBase)"
Write-Host ''

# IMPORTANT: named hashtable splatting is intentional.
# Do not replace this with an array such as @('-Configuration','Release'),
# because PowerShell script-to-script array splatting is positional and was
# the cause of the alpha1 'Resolve-Path ...\\Release' build failure.
$buildParams = @{
    Configuration = $Configuration
    Retries       = $Retries
}
if ($Clean)       { $buildParams['Clean'] = $true }
if ($NgxSdkDir)   { $buildParams['NgxSdkDir'] = $NgxSdkDir }
if ($TinyExrDir)  { $buildParams['TinyExrDir'] = $TinyExrDir }
if ($NvofSdkDir)  { $buildParams['NvofSdkDir'] = $NvofSdkDir }

Write-Host '[1/3] Building source with mainland-China mirrors...' -ForegroundColor Cyan
& $buildCn @buildParams
if (-not $?) { throw 'CN base build script failed.' }

$fgExe = Join-Path $Root 'dist\tools\Crow-DLSS-Rendering-Tool-FG-Diagnostic.exe'
if (-not (Test-Path -LiteralPath $fgExe)) {
    throw "FG executable was not produced: $fgExe"
}

Write-Host '[2/3] Staging official NVIDIA DLSS-G runtime from the mirrored SDK checkout...' -ForegroundColor Cyan
$runtimeParams = @{}
if ($NgxSdkDir) { $runtimeParams['NgxSdkDir'] = $NgxSdkDir }
& $runtimeSetup @runtimeParams -ChinaMirror -Required
if (-not $?) { throw 'DLSS-G runtime staging failed.' }

Write-Host '[3/3] Preparing FFmpeg from npmmirror...' -ForegroundColor Cyan
if (Test-Path -LiteralPath $videoSetup) {
    try {
        & $videoSetup
        if (-not $?) {
            Write-Warning 'FFmpeg CN setup returned a failure. The FG EXE is built, but video conversion requires FFmpeg/ffprobe.'
        }
    } catch {
        Write-Warning ('FFmpeg CN setup did not complete: ' + $_.Exception.Message)
        Write-Warning 'You can re-run video\setup_video_cn.ps1 manually.'
    }
} else {
    Write-Warning "CN FFmpeg setup script was not found: $videoSetup"
}

Write-Host ''
Write-Host '[READY] FG CN mirror build completed.' -ForegroundColor Green
Write-Host "  EXE     : $fgExe"
Write-Host "  Runtime : $(Join-Path $Root 'dist\runtime\nvngx_dlssg.dll')"
Write-Host 'Run dist\tools\Crow-DLSS-Rendering-Tool-FG-Diagnostic.exe, then use Check Runtime before the first conversion.'
