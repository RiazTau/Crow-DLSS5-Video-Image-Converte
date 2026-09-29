param(
    [switch]$ChinaMirror,
    [switch]$NoZip,
    [string]$Version = 'V0.6.6-alpha2'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Deps = Join-Path $Root '.deps'
$Dist = Join-Path $Root 'dist'
$PortableRoot = Join-Path $Root 'portable'
$PortableName = "Crow-DLSS5-Video-Image-Converter-$Version-Portable-x64"
$Out = Join-Path $PortableRoot $PortableName
$Zip = Join-Path $PortableRoot ($PortableName + '.zip')
$BuilderPointer = Join-Path $Deps 'portable-builder-python.path'
$StageTool = Join-Path $PSScriptRoot 'portable_stage.py'
$ZipTool = Join-Path $PSScriptRoot 'zip_portable.py'

function Step([string]$s) { Write-Host ''; Write-Host ('=== ' + $s + ' ===') -ForegroundColor Cyan }
function Ok([string]$s) { Write-Host ('[OK] ' + $s) -ForegroundColor Green }
function Warn([string]$s) { Write-Host ('[WARN] ' + $s) -ForegroundColor Yellow }

function Test-BuilderPython([string]$Exe) {
    if (-not $Exe -or -not (Test-Path $Exe)) { return $false }
    try {
        & $Exe -c "import struct,sys; raise SystemExit(0 if (3,11)<=sys.version_info[:2]<(3,14) and struct.calcsize('P')==8 else 1)" 2>$null
        if ($LASTEXITCODE -ne 0) { return $false }
        & $Exe -m pip --version 2>$null | Out-Null
        return $LASTEXITCODE -eq 0
    } catch { return $false }
}

function Find-BuilderPython {
    if (Test-Path $BuilderPointer) {
        $saved = (Get-Content -LiteralPath $BuilderPointer -Raw).Trim()
        if (Test-BuilderPython $saved) { return (Resolve-Path $saved).Path }
    }
    $candidates = @()
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py) {
        try {
            $resolved = (& $py.Source -3 -c "import sys; print(sys.executable)" 2>$null | Select-Object -First 1)
            if ($resolved) { $candidates += $resolved.Trim() }
        } catch {}
    }
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) { $candidates += $python.Source }
    foreach ($p in @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python311\python.exe')
    )) { if ($p) { $candidates += $p } }
    foreach ($c in ($candidates | Select-Object -Unique)) {
        if (Test-BuilderPython $c) {
            New-Item -ItemType Directory -Force -Path $Deps | Out-Null
            (Resolve-Path $c).Path | Set-Content -LiteralPath $BuilderPointer -Encoding ASCII
            return (Resolve-Path $c).Path
        }
    }
    throw 'x64 Python 3.11-3.13 with pip was not found. Run AUTO_BUILD_CN.bat/AUTO_BUILD.bat first.'
}

function Invoke-Checked([string]$Exe,[string[]]$Arguments) {
    Write-Host ('> ' + $Exe + ' ' + ($Arguments -join ' ')) -ForegroundColor DarkGray
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

function Copy-RequiredFile([string]$Source,[string]$Destination) {
    if (-not (Test-Path $Source)) { throw "Required file is missing: $Source" }
    $parent = Split-Path -Parent $Destination
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Copy-Tree([string]$Source,[string]$Destination) {
    if (-not (Test-Path $Source)) { throw "Required directory is missing: $Source" }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Copy-Item -Path (Join-Path $Source '*') -Destination $Destination -Recurse -Force
}

function Find-Vc143Crt {
    $roots = @()
    foreach ($vswhere in @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )) {
        if ($vswhere -and (Test-Path $vswhere)) {
            try {
                $installs = @(& $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null)
                foreach ($i in $installs) { if ($i) { $roots += (Join-Path $i 'VC\Redist\MSVC') } }
            } catch {}
        }
    }
    $roots += @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Community\VC\Redist\MSVC'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC')
    )
    $hits = @()
    foreach ($r in ($roots | Select-Object -Unique)) {
        if (-not $r -or -not (Test-Path $r)) { continue }
        foreach ($versionDir in @(Get-ChildItem -LiteralPath $r -Directory -ErrorAction SilentlyContinue)) {
            foreach ($crtName in @('Microsoft.VC143.CRT','Microsoft.VC142.CRT')) {
                $candidate = Join-Path $versionDir.FullName ('x64\' + $crtName)
                if ((Test-Path (Join-Path $candidate 'msvcp140.dll')) -and (Test-Path (Join-Path $candidate 'vcruntime140.dll'))) {
                    $hits += [pscustomobject]@{ Version=$versionDir.Name; Path=$candidate }
                }
            }
        }
    }
    if ($hits.Count -eq 0) { throw 'VC143 x64 app-local CRT was not found under Visual Studio VC\Redist\MSVC.' }
    return ($hits | Sort-Object {[version]($_.Version -replace '[^0-9\.]','')} -Descending | Select-Object -First 1).Path
}

$Builder = Find-BuilderPython
$pyVersion = (& $Builder -c "import sys; print('.'.join(map(str,sys.version_info[:3])))").Trim()
$pyXY = (& $Builder -c "import sys; print(f'{sys.version_info[0]}{sys.version_info[1]}')").Trim()
Ok "Portable builder Python: $Builder ($pyVersion x64)"

Step 'Validating compiled/runtime inputs'
$requiredExe = @(
    'Crow-DLSS5-Video-Image-Converter-CLI.exe',
    'Crow-DLSS5-Video-Image-Converter-Image.exe',
    'Crow-DLSS5-Video-Image-Converter-Video.exe',
    'Crow-DLSS5-Video-Image-Converter-Runtime-Self-Test.exe',
    'Crow-DLSS5-Video-Image-Converter-NVOF-Self-Test.exe',
    'Crow-DLSS5-Video-Image-Converter-NVOF-Execute-Self-Test.exe'
)
foreach ($name in $requiredExe) {
    if (-not (Test-Path (Join-Path $Dist $name))) { throw "Missing compiled output: dist\\$name" }
}
$Runtime = Join-Path $Dist 'runtime\nvngx_dlssnr.dll'
if (-not (Test-Path $Runtime)) {
    throw @'
DLSSNR runtime was not imported.
Portable builds intentionally do not download nvngx_dlssnr.dll automatically.
Import your validated runtime first with RTX40_RUNTIME_IMPORT.bat or scripts\import_runtime.ps1, then retry.
'@
}
$Ffmpeg = Join-Path $Dist 'video\ffmpeg'
if (-not (Test-Path (Join-Path $Ffmpeg 'bin\ffmpeg.exe')) -or -not (Test-Path (Join-Path $Ffmpeg 'bin\ffprobe.exe'))) {
    throw 'FFmpeg is not ready under dist\video\ffmpeg. Run AUTO_BUILD_CN.bat/AUTO_BUILD.bat first.'
}
$Model = Join-Path $Dist 'models\depth_anything_v2\model_fp16.onnx'
if (-not (Test-Path $Model)) { throw 'Depth Anything V2 model is missing under dist\models. Run model setup first.' }
Ok 'Compiled EXEs, DLSSNR runtime, FFmpeg and Depth Anything model are present.'

Step 'Cleaning previous portable tree with long-path-safe Python'
New-Item -ItemType Directory -Force -Path $PortableRoot | Out-Null
Invoke-Checked $Builder @($StageTool,'remove',$Out)
if (Test-Path $Zip) { Remove-Item -LiteralPath $Zip -Force }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

Step 'Staging converter binaries and runtime data'
foreach ($name in $requiredExe) { Copy-RequiredFile (Join-Path $Dist $name) (Join-Path $Out $name) }
Copy-Tree (Join-Path $Dist 'runtime') (Join-Path $Out 'runtime')
Copy-Tree $Ffmpeg (Join-Path $Out 'video\ffmpeg')
Copy-RequiredFile (Join-Path $Root 'video\setup_video.ps1') (Join-Path $Out 'video\setup_video.ps1')
Copy-RequiredFile (Join-Path $Root 'video\setup_video_cn.ps1') (Join-Path $Out 'video\setup_video_cn.ps1')
Copy-Tree (Join-Path $Dist 'models') (Join-Path $Out 'models')
Copy-RequiredFile (Join-Path $Root 'LICENSE') (Join-Path $Out 'LICENSE')
Copy-RequiredFile (Join-Path $Root 'docs\NOTICE.md') (Join-Path $Out 'NOTICE.md')

$autoOut = Join-Path $Out 'auto_depth'
New-Item -ItemType Directory -Force -Path $autoOut | Out-Null
foreach ($name in @('auto_depth.py','auto_depth_video.py','dis_flow_video.py','requirements.txt','setup_auto_depth.ps1','setup_auto_depth_cn.ps1')) {
    Copy-RequiredFile (Join-Path $Root ('auto_depth\' + $name)) (Join-Path $autoOut $name)
}

Step 'Bundling app-local VC143 CRT (/MD, NVIDIA NGX compatible)'
$crt = Find-Vc143Crt
Write-Host "VC CRT: $crt"
$crtDlls = @(Get-ChildItem -LiteralPath $crt -Filter '*.dll' -File)
if ($crtDlls.Count -eq 0) { throw "No CRT DLLs were found in $crt" }
foreach ($dll in $crtDlls) { Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $Out $dll.Name) -Force }
foreach ($essential in @('msvcp140.dll','vcruntime140.dll')) {
    if (-not (Test-Path (Join-Path $Out $essential))) { throw "App-local CRT is incomplete: $essential missing" }
}
Ok ("App-local CRT DLLs bundled: " + $crtDlls.Count)

Step 'Creating relocatable embedded Python runtime'
$embedUrl = if ($ChinaMirror) {
    "https://mirrors.ustc.edu.cn/python/$pyVersion/python-$pyVersion-embed-amd64.zip"
} else {
    "https://www.python.org/ftp/python/$pyVersion/python-$pyVersion-embed-amd64.zip"
}
$downloadDir = Join-Path $Deps 'downloads'
New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
$embedZip = Join-Path $downloadDir "python-$pyVersion-embed-amd64.zip"
if (-not (Test-Path $embedZip) -or (Get-Item $embedZip).Length -lt 5MB) {
    Write-Host "Downloading: $embedUrl"
    Invoke-WebRequest -UseBasicParsing -Uri $embedUrl -OutFile $embedZip
}
if ((Get-Item $embedZip).Length -lt 5MB) { throw 'Embedded Python archive is unexpectedly small.' }
$pyScripts = Join-Path $autoOut '.venv\Scripts'
New-Item -ItemType Directory -Force -Path $pyScripts | Out-Null
Expand-Archive -LiteralPath $embedZip -DestinationPath $pyScripts -Force
$targetPy = Join-Path $pyScripts 'python.exe'
if (-not (Test-Path $targetPy)) { throw 'Embedded Python extraction did not produce python.exe.' }
# python.exe is a child process whose application directory is this Scripts folder.
# Copy the same app-local VC runtime here as well, otherwise Python extension
# modules (cv2/onnxruntime) can fail on a clean target PC without VC++ Redist.
foreach ($dll in $crtDlls) {
    Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $pyScripts $dll.Name) -Force
}
$pth = Join-Path $pyScripts ("python$pyXY._pth")
if (-not (Test-Path $pth)) { throw "Embedded Python path file missing: $pth" }
@(
    "python$pyXY.zip",
    '.',
    'Lib\\site-packages',
    'import site'
) | Set-Content -LiteralPath $pth -Encoding ASCII
$site = Join-Path $pyScripts 'Lib\site-packages'
New-Item -ItemType Directory -Force -Path $site | Out-Null

$indexArgs = @()
if ($ChinaMirror) { $indexArgs = @('-i','https://mirrors.ustc.edu.cn/pypi/simple') }
Invoke-Checked $Builder (@('-m','pip','install','--upgrade','--disable-pip-version-check','--target',$site,'pip') + $indexArgs)
Invoke-Checked $Builder (@('-m','pip','install','--upgrade','--disable-pip-version-check','--target',$site,'-r',(Join-Path $Root 'auto_depth\requirements.txt')) + $indexArgs)
Invoke-Checked $Builder @($StageTool,'prune',$site)
Invoke-Checked $targetPy @('-c',"import struct,cv2,onnxruntime,numpy,PIL; assert struct.calcsize('P')==8; print('Portable Python OK'); print('OpenCV',cv2.__version__); print(onnxruntime.get_available_providers())")
Ok 'Relocatable embedded Python + AutoDepth/DIS dependencies verified.'

Step 'Writing portable README and manifest'
$readme = @"
Crow-DLSS5-Video-Image-Converter $Version - Portable x64
================================================

Main applications:
  Crow-DLSS5-Video-Image-Converter-Image.exe
  Crow-DLSS5-Video-Image-Converter-Video.exe

This package is designed to run without a separate Python installation and
without a separate Microsoft VC++ Redistributable installation. The converter
remains /MD because NVIDIA NGX is built against the dynamic MSVC runtime; the
matching VC143 CRT DLLs are therefore shipped app-local beside the EXEs.

Bundled:
  - Auto Depth / DIS embedded Python runtime
  - ONNX Runtime DirectML, OpenCV, NumPy, Pillow
  - FFmpeg / ffprobe
  - Depth Anything V2 model
  - the manually imported nvngx_dlssnr.dll

Not bundled:
  - NVIDIA display driver (required on the target PC)
  - NVIDIA Optical Flow SDK headers (build-time only)
  - nvofapi64.dll (provided by the NVIDIA driver)

NVOF requires a supported NVIDIA GPU/driver. Run "Crow-DLSS5-Video-Image-Converter-NVOF-Self-Test.exe"
and "Crow-DLSS5-Video-Image-Converter-NVOF-Execute-Self-Test.exe" on a new PC if NVOF availability is in doubt.

The entire folder may be moved to another drive/path. Do not move individual
runtime subfolders away from the EXEs.
"@
Set-Content -LiteralPath (Join-Path $Out 'PORTABLE_README.txt') -Value $readme -Encoding UTF8
Invoke-Checked $Builder @($StageTool,'manifest',$Out,(Join-Path $Out 'PORTABLE_MANIFEST.json'),$Version)

if (-not $NoZip) {
    Step 'Creating Zip64 portable archive (no PowerShell Compress-Archive)'
    Invoke-Checked $Builder @($ZipTool,$Out,$Zip)
}

Write-Host ''
Write-Host '============================================================' -ForegroundColor Green
Write-Host 'PORTABLE PACKAGE READY' -ForegroundColor Green
Write-Host "Folder : $Out"
if (-not $NoZip) { Write-Host "ZIP    : $Zip" }
Write-Host '============================================================' -ForegroundColor Green
