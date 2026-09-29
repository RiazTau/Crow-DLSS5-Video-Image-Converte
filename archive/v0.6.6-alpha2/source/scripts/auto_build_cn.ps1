param(
    [switch]$SkipAutoDepth,
    [switch]$SkipVideo,
    [switch]$SkipRuntimePrompt,
    [switch]$NoInstall,
    [switch]$Portable
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Tools = Join-Path $Root '.tools'
$LogDir = Join-Path $Root 'logs'
New-Item -ItemType Directory -Force -Path $Tools,$LogDir | Out-Null
$LogPath = Join-Path $LogDir ('auto-build-cn-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
. (Join-Path $PSScriptRoot 'cn_mirrors.ps1')

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host ('=== ' + $Text + ' ===') -ForegroundColor Cyan
}
function Write-Ok([string]$Text) { Write-Host ('[OK] ' + $Text) -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host ('[WARN] ' + $Text) -ForegroundColor Yellow }
function Write-Fail([string]$Text) { Write-Host ('[ERROR] ' + $Text) -ForegroundColor Red }

function Refresh-ProcessPath {
    $machine = [Environment]::GetEnvironmentVariable('Path','Machine')
    $user = [Environment]::GetEnvironmentVariable('Path','User')
    $parts = @($machine,$user) | Where-Object { $_ }
    if ($parts.Count -gt 0) { $env:Path = ($parts -join ';') }
}

function Add-PathFront([string]$Path) {
    if (-not $Path -or -not (Test-Path $Path)) { return }
    $existing = @($env:Path -split ';' | Where-Object { $_ })
    if ($existing -notcontains $Path) { $env:Path = $Path + ';' + $env:Path }
}

function Invoke-NativeChecked {
    param([Parameter(Mandatory=$true)][string]$Exe,[string[]]$Arguments=@(),[int[]]$AllowedExitCodes=@(0))
    Write-Host ('> ' + $Exe + ' ' + ($Arguments -join ' ')) -ForegroundColor DarkGray
    & $Exe @Arguments
    $code = $LASTEXITCODE
    if ($AllowedExitCodes -notcontains $code) { throw "$Exe failed with exit code $code" }
}

function Test-PeFile([string]$Path,[long]$MinimumBytes=1MB) {
    if (-not (Test-Path $Path)) { return $false }
    $item = Get-Item $Path
    if ($item.Length -lt $MinimumBytes) { return $false }
    $fs = [IO.File]::OpenRead($Path)
    try {
        $a = $fs.ReadByte(); $b = $fs.ReadByte()
        return ($a -eq 0x4D -and $b -eq 0x5A)
    } finally { $fs.Dispose() }
}

function Get-PythonCandidate {
    $candidates = New-Object System.Collections.ArrayList
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) { [void]$candidates.Add($python.Source) }
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py) {
        try {
            $resolved = & $py.Source -3 -c "import sys; print(sys.executable)" 2>$null | Select-Object -First 1
            if ($resolved) { [void]$candidates.Add($resolved) }
        } catch {}
    }
    foreach ($p in @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'),
        (Join-Path $env:ProgramFiles 'Python313\python.exe'),
        (Join-Path $env:ProgramFiles 'Python312\python.exe')
    )) { if ($p -and (Test-Path $p)) { [void]$candidates.Add($p) } }

    foreach ($p in ($candidates | Select-Object -Unique)) {
        try {
            $v = & $p -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}')" 2>$null | Select-Object -First 1
            $ver = [version]$v
            if ($ver -ge [version]'3.11') { return [pscustomobject]@{ Path=$p; Version=$ver } }
        } catch {}
    }
    return $null
}

function Ensure-PythonCN {
    $found = Get-PythonCandidate
    if ($found) {
        Add-PathFront (Split-Path -Parent $found.Path)
        Write-Ok ("Python {0}: {1}" -f $found.Version,$found.Path)
        return $found.Path
    }
    if ($NoInstall) { throw 'Python 3.11+ is missing and -NoInstall was specified.' }

    Write-Step 'Installing Python from USTC mirror'
    $installer = Join-Path $env:TEMP ('python-' + $CNMirrors.PythonVersion + '-amd64.exe')
    Invoke-CNDownload -Uri $CNMirrors.PythonInstaller -OutFile $installer
    if (-not (Test-PeFile -Path $installer -MinimumBytes 10MB)) { throw 'USTC Python installer validation failed (size/PE header).' }
    $sig = Get-AuthenticodeSignature $installer
    Write-Host ('Python installer signature: ' + $sig.Status)
    if ($sig.Status -ne 'Valid') { throw 'Python installer Authenticode signature is not valid. Refusing to run it.' }
    $p = Start-Process -FilePath $installer -ArgumentList '/quiet','InstallAllUsers=0','PrependPath=1','Include_test=0','Include_launcher=1' -Wait -PassThru
    Remove-Item $installer -Force -ErrorAction SilentlyContinue
    if ($p.ExitCode -ne 0) { throw "Python installer failed with exit code $($p.ExitCode)." }
    Refresh-ProcessPath
    $found = Get-PythonCandidate
    if (-not $found) { throw 'Python installation completed, but Python 3.11+ could not be located. Close this console and run AUTO_BUILD_CN.bat again.' }
    Add-PathFront (Split-Path -Parent $found.Path)
    Write-Ok ("Python {0} installed from USTC mirror." -f $found.Version)
    return $found.Path
}

function Get-CMakeVersion {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if (-not $cmd) { return $null }
    try {
        $line = & $cmd.Source --version 2>$null | Select-Object -First 1
        if ($line -match 'cmake version\s+([0-9]+\.[0-9]+\.[0-9]+)') {
            return [pscustomobject]@{ Path=$cmd.Source; Version=[version]$Matches[1] }
        }
    } catch {}
    return $null
}

function Ensure-CMakeCN([string]$Python) {
    $found = Get-CMakeVersion
    if ($found -and $found.Version -ge [version]'3.24') {
        Write-Ok ("CMake {0}: {1}" -f $found.Version,$found.Path)
        return
    }
    if ($NoInstall) { throw 'CMake 3.24+ is missing and -NoInstall was specified.' }

    Write-Step 'Installing portable CMake from USTC PyPI mirror'
    $venv = Join-Path $Tools 'cmake-venv'
    $venvPy = Join-Path $venv 'Scripts\python.exe'
    if (-not (Test-Path $venvPy)) {
        Invoke-NativeChecked -Exe $Python -Arguments @('-m','venv',$venv)
    }
    Invoke-NativeChecked -Exe $venvPy -Arguments @('-m','pip','install','--upgrade','pip','-i',$CNMirrors.PyPI,'--disable-pip-version-check')
    Invoke-NativeChecked -Exe $venvPy -Arguments @('-m','pip','install','cmake>=3.24,<5','-i',$CNMirrors.PyPI,'--disable-pip-version-check')
    Add-PathFront (Join-Path $venv 'Scripts')
    $found = Get-CMakeVersion
    if (-not $found -or $found.Version -lt [version]'3.24') { throw 'CMake installation from USTC PyPI completed, but cmake.exe 3.24+ is unavailable.' }
    Write-Ok ("Portable CMake {0} ready." -f $found.Version)
}

function Ensure-GitCN {
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if ($git) { Write-Ok ('Git: ' + (& $git.Source --version)); return }
    if ($NoInstall) { throw 'Git is missing and -NoInstall was specified.' }

    Write-Step 'Installing portable MinGit from USTC GitHub-Release mirror'
    $mingit = Join-Path $Tools 'mingit'
    $mingitCmd = Join-Path $mingit 'cmd\git.exe'
    if (Test-Path $mingitCmd) {
        Add-PathFront (Join-Path $mingit 'cmd')
        Write-Ok ('MinGit: ' + (& $mingitCmd --version))
        return
    }

    $indexTemp = Join-Path $env:TEMP 'ustc-mingit-index.html'
    Invoke-CNDownload -Uri $CNMirrors.GitReleaseIndex -OutFile $indexTemp
    $html = Get-Content -LiteralPath $indexTemp -Raw
    Remove-Item $indexTemp -Force -ErrorAction SilentlyContinue
    $matches = [regex]::Matches($html,'MinGit-[0-9\.]+-64-bit\.zip')
    if ($matches.Count -eq 0) { throw 'Could not locate a MinGit x64 archive in the USTC LatestRelease mirror index.' }
    $names = @($matches | ForEach-Object Value | Sort-Object -Unique)
    $name = $names[-1]
    $zip = Join-Path $env:TEMP $name
    Invoke-CNDownload -Uri ($CNMirrors.GitReleaseBase + $name) -OutFile $zip
    if ((Get-Item $zip).Length -lt 5MB) { throw 'Downloaded MinGit archive is unexpectedly small.' }
    if (Test-Path $mingit) { Remove-Item -Recurse -Force $mingit }
    New-Item -ItemType Directory -Force -Path $mingit | Out-Null
    Expand-Archive -LiteralPath $zip -DestinationPath $mingit -Force
    Remove-Item $zip -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path $mingitCmd)) { throw 'MinGit archive extracted, but cmd\git.exe was not found.' }
    Add-PathFront (Join-Path $mingit 'cmd')
    Write-Ok ('Portable MinGit ready: ' + (& $mingitCmd --version))
}

function Get-VsWherePath {
    foreach ($p in @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    )) { if ($p -and (Test-Path $p)) { return $p } }
    return $null
}

function Get-VsCppInstall {
    $vswhere = Get-VsWherePath
    if (-not $vswhere) { return $null }
    try {
        return (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null | Select-Object -First 1)
    } catch { return $null }
}

function Test-WindowsSdk {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
    if (-not (Test-Path $kits)) { return $false }
    $headers = @(Get-ChildItem -Path $kits -Directory -ErrorAction SilentlyContinue | ForEach-Object { Join-Path $_.FullName 'um\Windows.h' } | Where-Object { Test-Path $_ })
    return $headers.Count -gt 0
}

function Require-VisualStudioManual {
    Write-Step 'Checking Visual Studio C++ toolchain / Windows SDK'
    $vs = Get-VsCppInstall
    if ($vs -and (Test-WindowsSdk)) {
        Write-Ok ('Visual Studio C++ toolchain: ' + $vs)
        Write-Ok 'Windows 10/11 SDK detected.'
        return
    }
    if ($NoInstall) { throw 'Visual Studio 2022 C++ Build Tools and/or Windows SDK are missing.' }
    try { Start-Process 'https://visualstudio.microsoft.com/zh-hans/downloads/' | Out-Null } catch {}
    throw @'
Visual Studio 2022 C++ Build Tools and/or Windows SDK are missing.
This CN-mirror build intentionally does NOT download the proprietary Visual Studio installer from a third-party mirror.
Install it manually from Microsoft, then select:
  - Desktop development with C++
  - MSVC v143 C++ x64/x86 build tools
  - Windows 10/11 SDK
  - C++ CMake tools for Windows (optional; this script also provides CMake)
After installation, run AUTO_BUILD_CN.bat again.
'@
}

function Require-VcRuntimeManual {
    Write-Step 'Checking Microsoft Visual C++ runtime'
    $dll = Join-Path $env:WINDIR 'System32\msvcp140.dll'
    $current = $null
    if (Test-Path $dll) {
        try { $current = [version]((Get-Item $dll).VersionInfo.FileVersion.Split(' ')[0]) } catch {}
    }
    if ($current) { Write-Host "MSVCP140.dll: $current" }
    if ($current -and $current -ge [version]'14.44.0.0') {
        Write-Ok 'VC++ runtime is new enough for this VS2022 toolset.'
        return
    }
    if ($NoInstall) { throw 'VC++ x64 runtime is older than 14.44 or missing.' }
    try { Start-Process 'https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist' | Out-Null } catch {}
    throw @'
Microsoft Visual C++ x64 Redistributable 14.44+ is required.
This proprietary runtime has no trusted public mainland mirror configured by this package, so automatic mirror download is disabled.
The official Microsoft guidance page was opened. Download/install the x64 redistributable manually, reboot if requested, then run AUTO_BUILD_CN.bat again.
'@
}

function Show-GpuAndDriverStatus {
    Write-Step 'Detecting GPU / NVIDIA driver'
    $gpus = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue)
    if ($gpus.Count -eq 0) { Write-Warn 'No display adapter was returned by WMI.'; return }
    foreach ($g in $gpus) { Write-Host ("GPU: {0} | Driver: {1}" -f $g.Name,$g.DriverVersion) }
    $nvidia = @($gpus | Where-Object { $_.Name -match 'NVIDIA|GeForce|RTX' })
    if ($nvidia.Count -eq 0) {
        Write-Warn 'No NVIDIA GPU was detected. Compilation can continue, but DLSSNR processing requires a supported NVIDIA GPU.'
        return
    }
    $smi = Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue
    if ($smi) {
        try {
            $lines = & $smi.Source --query-gpu=name,driver_version --format=csv,noheader 2>$null
            foreach ($line in $lines) { Write-Host ('NVIDIA driver: ' + $line) }
            Write-Ok 'NVIDIA display driver detected.'
        } catch { Write-Warn 'nvidia-smi exists but driver query failed.' }
    } else {
        $hasDriver = @($nvidia | Where-Object { -not [string]::IsNullOrWhiteSpace($_.DriverVersion) }).Count -gt 0
        if (-not $hasDriver) {
            if ($NoInstall) { throw 'NVIDIA display driver appears to be missing.' }
            try { Start-Process 'https://www.nvidia.cn/drivers/' | Out-Null } catch {}
            throw @'
NVIDIA display driver appears to be missing.
GPU drivers are intentionally a manual-install exception in CN-mirror mode.
Install the NVIDIA driver from NVIDIA China, reboot Windows, then run AUTO_BUILD_CN.bat again.
'@
        }
        Write-Warn 'nvidia-smi was not found, but WMI reports an NVIDIA driver version. Continuing.'
    }
    $names = ($nvidia | ForEach-Object Name) -join ' / '
    if ($names -match 'RTX\s*40|4090|4080|4070|4060|4050') {
        Write-Warn 'RTX 40/Ada detected. Current experimental DLSSNR Feature 18 runtimes may return 0xBAD00001 (FeatureNotSupported). This does not prevent compilation.'
    } elseif ($names -match 'RTX\s*50|5090|5080|5070|5060|5050') {
        Write-Ok 'RTX 50/Blackwell detected. Runtime support still depends on nvngx_dlssnr.dll and the NVIDIA driver.'
    }
}

function Select-DlssNrRuntime {
    if ($SkipRuntimePrompt) { return $null }
    Write-Step 'Select optional stable DLSSNR runtime'
    Write-Warn 'RTX40 experimental runtime users: CANCEL this picker and run RTX40_RUNTIME_IMPORT.bat after AutoBuild completes.'
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Filter = 'NVIDIA DLSS Neural Rendering runtime (nvngx_dlssnr.dll)|nvngx_dlssnr.dll|DLL files (*.dll)|*.dll'
        $dialog.Title = 'Optional stable runtime import - RTX40 experimental users should Cancel'
        $dialog.InitialDirectory = $Root
        $dialog.CheckFileExists = $true
        $dialog.Multiselect = $false
        $result = $dialog.ShowDialog()
        if ($result -ne [System.Windows.Forms.DialogResult]::OK) {
            Write-Warn 'DLSSNR runtime selection was cancelled. Build will continue without importing a runtime.'
            return $null
        }
        $file = $dialog.FileName
        if ([IO.Path]::GetFileName($file).ToLowerInvariant() -ne 'nvngx_dlssnr.dll') { throw 'The selected file must be named nvngx_dlssnr.dll.' }
        $sig = Get-AuthenticodeSignature $file
        $hash = (Get-FileHash $file -Algorithm SHA256).Hash
        $ver = (Get-Item $file).VersionInfo.FileVersion
        Write-Host "Runtime: $file"
        Write-Host "Version: $ver"
        Write-Host "SHA256 : $hash"
        Write-Host "Signature: $($sig.Status)"
        if ($sig.Status -ne 'Valid') { Write-Warn 'The selected DLL does not report a valid Authenticode signature. Verify provenance before use.' }
        return $file
    } catch {
        Write-Warn ('Runtime picker failed: ' + $_.Exception.Message)
        return $null
    }
}

try { Start-Transcript -Path $LogPath -Append | Out-Null } catch {}

try {
    Write-Host 'Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Native NVOF D3D12 Execute / Adaptive Stable Motion - Mainland China Mirror AutoBuild' -ForegroundColor White
    Write-Host "Project: $Root"
    Write-Host "Log    : $LogPath"
    Write-Host 'Automatic download policy: USTC / Gitee / npmmirror / hf-mirror only.'
    Write-Host 'Manual exceptions: Visual Studio/Windows SDK, VC++ runtime, NVIDIA driver, nvngx_dlssnr.dll.'
    if ($Portable) { Write-Host 'Portable packaging requested: binaries stay /MD for NGX and VC143 CRT is bundled app-local during packaging.' -ForegroundColor Cyan }

    try {
        Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force -ErrorAction Stop
        Write-Ok 'PowerShell execution policy for this process: Bypass'
    } catch {
        Write-Warn ('Could not set Process execution policy: ' + $_.Exception.Message)
        Write-Warn 'AUTO_BUILD_CN.bat also starts PowerShell with -ExecutionPolicy Bypass. MachinePolicy/UserPolicy cannot be overridden by this package.'
    }

    Write-Step 'System overview'
    Write-Host "Windows: $([Environment]::OSVersion.VersionString)"
    Write-Host "64-bit OS: $([Environment]::Is64BitOperatingSystem)"
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'This project requires 64-bit Windows.' }
    Show-GpuAndDriverStatus

    Refresh-ProcessPath
    Write-Step 'Checking / preparing open-source build prerequisites'
    $python = Ensure-PythonCN
    Ensure-CMakeCN -Python $python
    Ensure-GitCN
    Require-VisualStudioManual
    Require-VcRuntimeManual

    Write-Ok ('Git   : ' + (& git.exe --version))
    Write-Ok ('CMake : ' + ((& cmake.exe --version | Select-Object -First 1)))
    $pyFound = Get-PythonCandidate
    Write-Ok ("Python: {0}" -f $pyFound.Version)

    $runtime = Select-DlssNrRuntime

    Write-Step 'Compiling source with mainland mirror dependencies'
    $buildScript = Join-Path $Root 'scripts\build_cn.ps1'
    $buildParams = @{ Clean = $true; Configuration = 'Release' }
    if ($Portable) { $buildParams.Portable = $true }
    & $buildScript @buildParams

    if ($runtime) {
        Write-Step 'Importing selected DLSSNR runtime'
        & (Join-Path $Root 'scripts\import_runtime.ps1') -Source $runtime
    }

    if (-not $SkipVideo) {
        Write-Step 'Setting up FFmpeg from npmmirror'
        $srcSetup = Join-Path $Root 'video\setup_video_cn.ps1'
        $dstDir = Join-Path $Root 'dist\video'
        $dstSetup = Join-Path $dstDir 'setup_video_cn.ps1'
        $dstGuiSetup = Join-Path $dstDir 'setup_video.ps1'
        New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
        Copy-Item -LiteralPath $srcSetup -Destination $dstSetup -Force
        # The GUI button always launches setup_video.ps1. In the CN package,
        # make that generic entry point the mirror-safe script as well.
        Copy-Item -LiteralPath $srcSetup -Destination $dstGuiSetup -Force
        try {
            & $dstSetup
            if ($LASTEXITCODE -ne 0) { throw "FFmpeg setup exited with code $LASTEXITCODE" }
        } catch {
            Write-Warn ('FFmpeg setup did not complete: ' + $_.Exception.Message)
            Write-Warn 'Compilation already succeeded. Continuing so RTX40 Runtime Self-Test and image processing can still be validated.'
            Write-Warn 'Video conversion will remain unavailable until dist\video\setup_video.ps1 succeeds or ffmpeg.exe/ffprobe.exe are supplied manually.'
        }
    }

    if (-not $SkipAutoDepth) {
        Write-Step 'Setting up Auto Depth / Temporal runtime from USTC PyPI'
        $srcSetup = Join-Path $Root 'auto_depth\setup_auto_depth_cn.ps1'
        $dstDir = Join-Path $Root 'dist\auto_depth'
        $dstSetup = Join-Path $dstDir 'setup_auto_depth_cn.ps1'
        New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
        Copy-Item -LiteralPath $srcSetup -Destination $dstSetup -Force
        & $dstSetup

        Write-Step 'Installing Depth Anything V2 model from hf-mirror'
        & (Join-Path $Root 'scripts\setup_models_cn.ps1') -InstallRoot (Join-Path $Root 'dist')
    }

    Write-Step 'Final verification'
    foreach ($f in @(
        (Join-Path $Root 'dist\Crow-DLSS5-Video-Image-Converter-CLI.exe'),
        (Join-Path $Root 'dist\Crow-DLSS5-Video-Image-Converter-Image.exe'),
        (Join-Path $Root 'dist\Crow-DLSS5-Video-Image-Converter-Video.exe'),
        (Join-Path $Root 'dist\Crow-DLSS5-Video-Image-Converter-Runtime-Self-Test.exe')
    )) {
        if (-not (Test-Path $f)) { throw "Missing compiled output: $f" }
        Write-Ok $f
    }
    $runtimeDest = Join-Path $Root 'dist\runtime\nvngx_dlssnr.dll'
    if (Test-Path $runtimeDest) { Write-Ok "DLSSNR runtime: $runtimeDest" }
    else { Write-Warn 'DLSSNR runtime was not imported. Run scripts\import_runtime.ps1 before processing.' }

    Write-Host ''
    Write-Host '============================================================' -ForegroundColor Green
    Write-Host 'MAINLAND CHINA MIRROR AUTO BUILD COMPLETE' -ForegroundColor Green
    Write-Host 'Image GUI: dist\Crow-DLSS5-Video-Image-Converter-Image.exe'
    Write-Host 'Video GUI: dist\Crow-DLSS5-Video-Image-Converter-Video.exe'
    Write-Host "Log      : $LogPath"
    Write-Host '============================================================' -ForegroundColor Green
    exit 0
}
catch {
    Write-Host ''
    Write-Fail $_.Exception.Message
    Write-Host "Diagnostic log: $LogPath" -ForegroundColor Yellow
    exit 1
}
finally {
    try { Stop-Transcript | Out-Null } catch {}
}
