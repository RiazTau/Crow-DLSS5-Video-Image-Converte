param(
    [switch]$Clean,
    [string]$Configuration = "Release",
    [string]$NgxSdkDir = "",
    [string]$TinyExrDir = "",
    [string]$NvofSdkDir = "",
    [switch]$Portable,
    [ValidateRange(1,10)][int]$Retries = 3
)

$ErrorActionPreference = "Stop"

# Resolve the project root explicitly instead of relying on string concatenation.
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Deps = Join-Path $Root ".deps"
$Build = Join-Path $Root "build"
$Dist = Join-Path $Root "dist"
$Downloads = Join-Path $Deps "downloads"

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory=$true)][string]$Exe,
        [string[]]$Arguments = @()
    )
    & $Exe @Arguments
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        $joined = ($Arguments -join " ")
        throw "$Exe $joined failed with exit code $code"
    }
}

function Test-NgxSdk {
    param([string]$Path)
    if (-not $Path) { return $false }
    $header = Join-Path $Path "include\nvsdk_ngx.h"
    $lib1 = Join-Path $Path "lib\Windows_x86_64\x64\nvsdk_ngx_d.lib"
    $lib2 = Join-Path $Path "lib\Windows_x86_64\x86_64\nvsdk_ngx_d.lib"
    return (Test-Path $header) -and ((Test-Path $lib1) -or (Test-Path $lib2))
}

function Test-TinyExr {
    param([string]$Path)
    if (-not $Path) { return $false }
    return (Test-Path (Join-Path $Path "tinyexr.h")) -and
           (Test-Path (Join-Path $Path "deps\miniz\miniz.h")) -and
           (Test-Path (Join-Path $Path "deps\miniz\miniz.c"))
}

function Remove-PartialDirectory {
    param([string]$Path)
    if (Test-Path $Path) {
        Remove-Item -Recurse -Force $Path
    }
}

function Clone-WithRetry {
    param(
        [string]$Repository,
        [string]$Destination,
        [string]$Branch = "",
        [int]$Attempts = 3
    )

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        Write-Warning "git was not found; skipping git clone and using archive fallback."
        return $false
    }

    for ($i = 1; $i -le $Attempts; $i++) {
        Remove-PartialDirectory $Destination
        Write-Host "git clone attempt $i/$Attempts : $Repository" -ForegroundColor Cyan
        try {
            if ($Branch) {
                $gitArgs = @(
                    "-c", "http.version=HTTP/1.1",
                    "clone", "--depth", "1", "--single-branch",
                    "--branch", $Branch, $Repository, $Destination
                )
            } else {
                $gitArgs = @(
                    "-c", "http.version=HTTP/1.1",
                    "clone", "--depth", "1", $Repository, $Destination
                )
            }
            Invoke-NativeChecked -Exe "git" -Arguments $gitArgs
            return $true
        } catch {
            Write-Warning $_.Exception.Message
            Remove-PartialDirectory $Destination
            if ($i -lt $Attempts) { Start-Sleep -Seconds ([Math]::Min(2 * $i, 6)) }
        }
    }
    return $false
}

function Download-ZipFallback {
    param(
        [string]$Url,
        [string]$Destination,
        [string]$ExpectedRootPattern
    )

    New-Item -ItemType Directory -Force -Path $Downloads | Out-Null
    $zip = Join-Path $Downloads (([Guid]::NewGuid().ToString()) + ".zip")
    $extract = Join-Path $Downloads (([Guid]::NewGuid().ToString()) + "_extract")
    try {
        Write-Host "Falling back to source archive: $Url" -ForegroundColor Yellow
        # Windows PowerShell 5.1 can otherwise negotiate legacy TLS on some systems.
        try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
        Invoke-WebRequest -Uri $Url -OutFile $zip -UseBasicParsing
        New-Item -ItemType Directory -Force -Path $extract | Out-Null
        Expand-Archive -Path $zip -DestinationPath $extract -Force
        $root = Get-ChildItem -Path $extract -Directory | Where-Object { $_.Name -like $ExpectedRootPattern } | Select-Object -First 1
        if (-not $root) {
            $root = Get-ChildItem -Path $extract -Directory | Select-Object -First 1
        }
        if (-not $root) { throw "Downloaded archive did not contain a source directory." }
        Remove-PartialDirectory $Destination
        Move-Item -Path $root.FullName -Destination $Destination
        return $true
    } catch {
        Write-Warning ("Archive fallback failed: " + $_.Exception.Message)
        Remove-PartialDirectory $Destination
        return $false
    } finally {
        Remove-Item -Force $zip -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force $extract -ErrorAction SilentlyContinue
    }
}

function Require-Command {
    param([string]$Name)
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command was not found in PATH: $Name"
    }
}

Require-Command cmake

New-Item -ItemType Directory -Force -Path $Deps,$Dist,$Downloads | Out-Null
if ($Clean -and (Test-Path $Build)) { Remove-Item -Recurse -Force $Build }

Write-Host "Project root   : $Root"
Write-Host "Dependency root: $Deps"

# Optional explicit/offline dependency roots. A completely fresh build can still
# point at manually downloaded SDK folders when GitHub is unavailable.
if ($NgxSdkDir) {
    $Ngx = (Resolve-Path $NgxSdkDir).Path
    Write-Host "Using supplied NGX SDK: $Ngx"
} else {
    $Ngx = Join-Path $Deps "NVIDIA-DLSS"
}

if ($TinyExrDir) {
    $Tiny = (Resolve-Path $TinyExrDir).Path
    Write-Host "Using supplied TinyEXR: $Tiny"
} else {
    $Tiny = Join-Path $Deps "tinyexr"
}

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


if (-not (Test-NgxSdk $Ngx)) {
    if ($NgxSdkDir) { throw "The supplied NGX SDK directory is incomplete: $Ngx" }
    Write-Host "Fetching NVIDIA DLSS/NGX SDK headers and import library..."
    $ok = Clone-WithRetry -Repository "https://github.com/NVIDIA/DLSS.git" -Destination $Ngx -Attempts $Retries
    if (-not $ok) {
        $ok = Download-ZipFallback -Url "https://codeload.github.com/NVIDIA/DLSS/zip/refs/heads/main" -Destination $Ngx -ExpectedRootPattern "DLSS-*"
    }
    if (-not $ok -or -not (Test-NgxSdk $Ngx)) {
        throw @"
Unable to obtain the NVIDIA DLSS/NGX SDK.
This is a network/dependency acquisition failure, not a compiler error.

Manual fallback:
1. Download NVIDIA/DLSS source from GitHub on any working connection.
2. Extract it to a folder containing include\nvsdk_ngx.h and lib\Windows_x86_64\x64\nvsdk_ngx_d.lib.
3. Re-run:
   .\scripts\build.ps1 -Clean -NgxSdkDir "C:\path\to\DLSS"
"@
    }
}

if (-not (Test-TinyExr $Tiny)) {
    if ($TinyExrDir) { throw "The supplied TinyEXR directory is incomplete: $Tiny" }
    Write-Host "Fetching TinyEXR release branch..."
    $ok = Clone-WithRetry -Repository "https://github.com/syoyo/tinyexr.git" -Destination $Tiny -Branch "release" -Attempts $Retries
    if (-not $ok) {
        $ok = Download-ZipFallback -Url "https://codeload.github.com/syoyo/tinyexr/zip/refs/heads/release" -Destination $Tiny -ExpectedRootPattern "tinyexr-*"
    }
    if (-not $ok -or -not (Test-TinyExr $Tiny)) {
        throw @"
Unable to obtain TinyEXR.

Manual fallback:
1. Download the syoyo/tinyexr 'release' branch source archive.
2. Extract it to a folder containing tinyexr.h and deps\miniz\miniz.c.
3. Re-run:
   .\scripts\build.ps1 -Clean -TinyExrDir "C:\path\to\tinyexr"
"@
    }
}

Write-Host "Dependency validation: PASS" -ForegroundColor Green
Write-Host "  NGX SDK : $Ngx"
Write-Host "  TinyEXR : $Tiny"

# V0.5 AutoBuild: pin the Visual Studio generator explicitly.
# Without -G, CMake can inherit/select NMake Makefiles, which does not accept -A x64.
$configureArgs = @(
    "-S", $Root,
    "-B", $Build,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-DNGX_SDK_DIR=$Ngx",
    "-DTINYEXR_DIR=$Tiny"
)
if ($Nvof) { $configureArgs += "-DNVOF_SDK_DIR=$Nvof" }
if ($Portable) {
    $configureArgs += "-DDLSS5_PORTABLE_BUILD=ON"
    Write-Host "Portable build requested: /MD retained for NVIDIA NGX; app-local VC143 CRT packaging required." -ForegroundColor Cyan
}
Invoke-NativeChecked -Exe "cmake" -Arguments $configureArgs

$buildArgs = @(
    "--build", $Build,
    "--config", $Configuration,
    "--parallel"
)
Invoke-NativeChecked -Exe "cmake" -Arguments $buildArgs

$Cli = Join-Path $Dist "Crow-DLSS5-Video-Image-Converter-CLI.exe"
$Gui = Join-Path $Dist "Crow-DLSS5-Video-Image-Converter-Image.exe"
$VideoGui = Join-Path $Dist "Crow-DLSS5-Video-Image-Converter-Video.exe"
$NvofSelfTest = Join-Path $Dist "Crow-DLSS5-Video-Image-Converter-NVOF-Self-Test.exe"
if (-not (Test-Path $Cli)) { throw "Expected executable was not produced: $Cli" }
if (-not (Test-Path $Gui)) { throw "Expected GUI executable was not produced: $Gui" }
if (-not (Test-Path $VideoGui)) { throw "Expected video GUI executable was not produced: $VideoGui" }
if (-not (Test-Path $NvofSelfTest)) { throw "Expected NVOF self-test executable was not produced: $NvofSelfTest" }
Write-Host "Build complete:" -ForegroundColor Green
Write-Host "  CLI      : $Cli"
Write-Host "  Image GUI: $Gui"
Write-Host "  Video GUI: $VideoGui"
Write-Host "  NVOF test: $NvofSelfTest"
