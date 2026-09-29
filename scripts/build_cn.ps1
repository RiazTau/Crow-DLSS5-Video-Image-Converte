param(
    [switch]$Clean,
    [string]$Configuration = 'Release',
    [string]$NgxSdkDir = '',
    [string]$TinyExrDir = '',
    [string]$NvofSdkDir = '',
    [string]$NvapiSdkDir = '',
    [switch]$Portable,
    [ValidateRange(1,10)][int]$Retries = 3
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Deps = Join-Path $Root '.deps'
$Build = Join-Path $Root 'build'
$Dist = Join-Path $Root 'dist'
. (Join-Path $PSScriptRoot 'cn_mirrors.ps1')

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory=$true)][string]$Exe,
        [string[]]$Arguments=@(),
        [string]$LogFile=''
    )
    Write-Host ('> ' + $Exe + ' ' + ($Arguments -join ' ')) -ForegroundColor DarkGray
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        if ($LogFile) {
            $logDir = Split-Path -Parent $LogFile
            if ($logDir) { New-Item -ItemType Directory -Force -Path $logDir | Out-Null }
            & $Exe @Arguments 2>&1 | Tee-Object -FilePath $LogFile -Append
        } else {
            & $Exe @Arguments
        }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($code -ne 0) {
        if ($LogFile -and (Test-Path $LogFile)) {
            Write-Host ''
            Write-Host ('=== Last 120 lines: ' + $LogFile + ' ===') -ForegroundColor Yellow
            Get-Content -LiteralPath $LogFile -Tail 120 | ForEach-Object { Write-Host $_ }
            Write-Host '=== End compiler diagnostic tail ===' -ForegroundColor Yellow
        }
        throw "$Exe failed with exit code $code"
    }
}

function Test-NgxSdk([string]$Path) {
    if (-not $Path) { return $false }
    $header = Join-Path $Path 'include\nvsdk_ngx.h'
    $lib1 = Join-Path $Path 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib'
    $lib2 = Join-Path $Path 'lib\Windows_x86_64\x86_64\nvsdk_ngx_d.lib'
    return (Test-Path $header) -and ((Test-Path $lib1) -or (Test-Path $lib2))
}

function Test-NvapiSdk([string]$Path) {
    if (-not $Path) { return $false }
    return (Test-Path (Join-Path $Path 'nvapi.h')) -and
           (Test-Path (Join-Path $Path 'NvApiDriverSettings.h')) -and
           (Test-Path (Join-Path $Path 'amd64\nvapi64.lib'))
}

function Test-TinyExr([string]$Path) {
    if (-not $Path) { return $false }
    return (Test-Path (Join-Path $Path 'tinyexr.h')) -and
           (Test-Path (Join-Path $Path 'deps\miniz\miniz.h')) -and
           (Test-Path (Join-Path $Path 'deps\miniz\miniz.c'))
}

function Remove-PartialDirectory([string]$Path) {
    if (Test-Path $Path) { Remove-Item -Recurse -Force $Path }
}

function Clone-WithRetry {
    param([string]$Repository,[string]$Destination,[string]$Branch='',[int]$Attempts=3)
    if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) {
        throw 'git.exe is required. Run BUILD.bat and choose Full build - CN mirrors so MinGit can be prepared.'
    }
    for ($i = 1; $i -le $Attempts; $i++) {
        Remove-PartialDirectory $Destination
        Write-Host "Gitee clone attempt $i/$Attempts : $Repository" -ForegroundColor Cyan
        try {
            $args = @('-c','http.version=HTTP/1.1','clone','--depth','1')
            if ($Branch) { $args += @('--single-branch','--branch',$Branch) }
            $args += @($Repository,$Destination)
            Invoke-NativeChecked -Exe 'git.exe' -Arguments $args
            return $true
        } catch {
            Write-Warning $_.Exception.Message
            Remove-PartialDirectory $Destination
            if ($i -lt $Attempts) { Start-Sleep -Seconds ([Math]::Min(2*$i,6)) }
        }
    }
    return $false
}

if (-not (Get-Command cmake.exe -ErrorAction SilentlyContinue)) {
    throw 'cmake.exe is missing. Run BUILD.bat and choose Full build - CN mirrors first.'
}

New-Item -ItemType Directory -Force -Path $Deps,$Dist | Out-Null
if ($Clean -and (Test-Path $Build)) { Remove-Item -Recurse -Force $Build }

if ($NgxSdkDir) { $Ngx = (Resolve-Path $NgxSdkDir).Path } else { $Ngx = Join-Path $Deps 'NVIDIA-DLSS' }
if ($TinyExrDir) { $Tiny = (Resolve-Path $TinyExrDir).Path } else { $Tiny = Join-Path $Deps 'tinyexr' }
if ($NvapiSdkDir) { $Nvapi = (Resolve-Path $NvapiSdkDir).Path } else { $Nvapi = Join-Path $Deps 'NVIDIA-nvapi' }

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
else { throw 'NVIDIA Optical Flow SDK 5.x is a required build prerequisite in V0.7.2. Launch BUILD.bat and complete the mandatory NVOF selection step.' }


if (-not (Test-NgxSdk $Ngx)) {
    if ($NgxSdkDir) { throw "The supplied NGX SDK directory is incomplete: $Ngx" }
    Write-Host 'Fetching NVIDIA DLSS/NGX SDK from the Gitee NVIDIA mirror...' -ForegroundColor Cyan
    $ok = Clone-WithRetry -Repository $CNMirrors.NgxGit -Destination $Ngx -Attempts $Retries
    if (-not $ok -or -not (Test-NgxSdk $Ngx)) {
        throw @"
Unable to obtain a valid NVIDIA DLSS/NGX SDK from the Gitee mirror.
Manual fallback: download/copy a DLSS SDK folder containing:
  include\nvsdk_ngx.h
  lib\Windows_x86_64\x64\nvsdk_ngx_d.lib
then run build_cn.ps1 with -NgxSdkDir.
"@
    }
}

if (-not (Test-TinyExr $Tiny)) {
    if ($TinyExrDir) { throw "The supplied TinyEXR directory is incomplete: $Tiny" }
    Write-Host 'Fetching TinyEXR release branch from the Gitee mirror...' -ForegroundColor Cyan
    $ok = Clone-WithRetry -Repository $CNMirrors.TinyExrGit -Destination $Tiny -Branch 'release' -Attempts $Retries
    if (-not $ok -or -not (Test-TinyExr $Tiny)) {
        Write-Warning 'The release branch was unavailable/incomplete on the mirror; retrying the mirror default branch.'
        $ok = Clone-WithRetry -Repository $CNMirrors.TinyExrGit -Destination $Tiny -Attempts $Retries
    }
    if (-not $ok -or -not (Test-TinyExr $Tiny)) {
        throw @"
Unable to obtain TinyEXR from the Gitee mirror.
Manual fallback: copy a TinyEXR release checkout containing tinyexr.h and deps\miniz\miniz.c,
then run build_cn.ps1 with -TinyExrDir.
"@
    }
}

$NvapiEnabled = Test-NvapiSdk $Nvapi
if (-not $NvapiEnabled) {
    if ($NvapiSdkDir) {
        Write-Warning "The supplied NVAPI SDK directory is incomplete: $Nvapi"
    } else {
        Write-Host 'CN build: optional NVAPI SDK is not auto-downloaded because no verified mainland mirror is available.' -ForegroundColor Yellow
        Write-Host 'Continuing without driver-level FG preset overrides. Place a current NVIDIA/nvapi checkout in .deps\NVIDIA-nvapi to enable them on the next build.' -ForegroundColor DarkYellow
        $NvapiEnabled = $false
    }
    if (-not $NvapiEnabled) {
        Write-Warning 'NVAPI SDK unavailable. Build will CONTINUE normally with FG Model Preset locked to Driver Default.'
        Write-Warning 'NR, FG/MFG, External EXR Motion/Depth and all core conversion features remain enabled.'
        Write-Warning 'To enable Preset A/B/Latest later, copy the current NVIDIA/nvapi SDK to .deps\NVIDIA-nvapi and rebuild.'
        $Nvapi = ''
    }
}

Write-Host 'Dependency validation: PASS' -ForegroundColor Green
Write-Host "  NGX SDK : $Ngx"
Write-Host "  TinyEXR : $Tiny"
if ($NvapiEnabled) { Write-Host "  NVAPI   : $Nvapi (FG preset override enabled)" -ForegroundColor Green }
else { Write-Host '  NVAPI   : unavailable (optional; Driver Default only)' -ForegroundColor Yellow }

$configureArgs = @(
    '-S',$Root,'-B',$Build,
    '-G','Visual Studio 17 2022','-A','x64',
    "-DNGX_SDK_DIR=$Ngx",
    "-DTINYEXR_DIR=$Tiny"
)
if ($NvapiEnabled) { $configureArgs += "-DNVAPI_SDK_DIR=$Nvapi" }
if ($Nvof) { $configureArgs += "-DNVOF_SDK_DIR=$Nvof" }
if ($Portable) {
    $configureArgs += "-DDLSS5_PORTABLE_BUILD=ON"
    Write-Host "Portable build requested: /MD retained for NVIDIA NGX; app-local VC143 CRT packaging required." -ForegroundColor Cyan
}
$cmakeConfigureLog = Join-Path $Root 'logs\cmake-configure-cn.log'
$cmakeBuildLog = Join-Path $Root ('logs\cmake-build-' + $Configuration.ToLowerInvariant() + '-cn.log')
Remove-Item -LiteralPath $cmakeConfigureLog,$cmakeBuildLog -Force -ErrorAction SilentlyContinue
Invoke-NativeChecked -Exe 'cmake.exe' -Arguments $configureArgs -LogFile $cmakeConfigureLog
$crowBuildTargets = @('--build',$Build,'--config',$Configuration,'--parallel','--verbose','--target','crow-cli','crow-image-gui','crow-runtime-selftest','crow-nvof-selftest','crow-nvof-execute-selftest','crow-video-gui','crow-fg-gui')
Invoke-NativeChecked -Exe 'cmake.exe' -Arguments $crowBuildTargets -LogFile $cmakeBuildLog

$required = @(
    (Join-Path $Dist 'tools\Crow-DLSS-Rendering-Tool-CLI.exe'),
    (Join-Path $Dist 'Crow-DLSS-Rendering-Tool-Image.exe'),
    (Join-Path $Dist 'Crow-DLSS-Rendering-Tool.exe'),
    (Join-Path $Dist 'tools\Crow-DLSS-Rendering-Tool-NVOF-Self-Test.exe')
)
foreach ($f in $required) {
    if (-not (Test-Path $f)) { throw "Expected executable was not produced: $f" }
}
& (Join-Path $PSScriptRoot 'finalize_dist.ps1')
Write-Host 'Build complete.' -ForegroundColor Green
