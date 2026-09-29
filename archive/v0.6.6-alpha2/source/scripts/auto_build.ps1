param(
    [switch]$SkipAutoDepth,
    [switch]$SkipVideo,
    [switch]$SkipRuntimePrompt,
    [switch]$NoInstall,
    [switch]$NoElevation,
    [switch]$Portable
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$LogDir = Join-Path $Root 'logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$LogPath = Join-Path $LogDir ('auto-build-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')

function Write-Step([string]$Text) {
    Write-Host ''
    Write-Host ('=== ' + $Text + ' ===') -ForegroundColor Cyan
}
function Write-Ok([string]$Text) { Write-Host ('[OK] ' + $Text) -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host ('[WARN] ' + $Text) -ForegroundColor Yellow }
function Write-Fail([string]$Text) { Write-Host ('[ERROR] ' + $Text) -ForegroundColor Red }

function Test-Administrator {
    try {
        $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
        $principal = New-Object Security.Principal.WindowsPrincipal($identity)
        return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    } catch { return $false }
}

function Refresh-ProcessPath {
    $machine = [Environment]::GetEnvironmentVariable('Path','Machine')
    $user = [Environment]::GetEnvironmentVariable('Path','User')
    $parts = @($machine,$user) | Where-Object { $_ }
    if ($parts.Count -gt 0) { $env:Path = ($parts -join ';') }
}

function Invoke-NativeChecked {
    param([Parameter(Mandatory=$true)][string]$Exe,[string[]]$Arguments=@(),[switch]$Allow3010)
    Write-Host ('> ' + $Exe + ' ' + ($Arguments -join ' ')) -ForegroundColor DarkGray
    & $Exe @Arguments
    $code = $LASTEXITCODE
    if ($Allow3010 -and ($code -eq 3010)) { return }
    if ($code -ne 0) { throw "$Exe failed with exit code $code" }
}

function Get-VsWherePath {
    $candidates = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    ) | Where-Object { $_ -and (Test-Path $_) }
    return ($candidates | Select-Object -First 1)
}

function Test-VsCppToolchain {
    $vswhere = Get-VsWherePath
    if (-not $vswhere) { return $false }
    $path = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    return -not [string]::IsNullOrWhiteSpace(($path | Select-Object -First 1))
}

function Ensure-Winget {
    $cmd = Get-Command winget.exe -ErrorAction SilentlyContinue
    if ($cmd) { Write-Ok ('winget: ' + $cmd.Source); return $cmd.Source }
    Write-Warn 'winget / App Installer is not available.'
    try { Start-Process 'ms-windows-store://pdp/?ProductId=9NBLGGH4NNS1' | Out-Null } catch {}
    throw @'
winget is required for automatic installation of missing build tools.
The Microsoft Store App Installer page has been opened when possible.
Install/update "App Installer", reopen this package, and run AUTO_BUILD.bat again.
'@
}

function Ensure-WingetPackage {
    param([string]$Id,[string]$DisplayName,[string]$CommandName,[string[]]$ExtraArgs=@())
    if ($CommandName -and (Get-Command $CommandName -ErrorAction SilentlyContinue)) {
        Write-Ok "$DisplayName already available."
        return
    }
    if ($NoInstall) { throw "$DisplayName is missing and -NoInstall was specified." }
    $winget = Ensure-Winget
    Write-Step "Installing $DisplayName"
    $args = @('install','--id',$Id,'--exact','--accept-package-agreements','--accept-source-agreements','--disable-interactivity') + $ExtraArgs
    Invoke-NativeChecked -Exe $winget -Arguments $args
    Refresh-ProcessPath
    if ($CommandName -and -not (Get-Command $CommandName -ErrorAction SilentlyContinue)) {
        throw "$DisplayName installation completed, but '$CommandName' is still not visible. Close this console and rerun AUTO_BUILD.bat."
    }
    Write-Ok "$DisplayName installed."
}

function Ensure-VisualStudioBuildTools {
    if (Test-VsCppToolchain) { Write-Ok 'Visual Studio 2022 C++ Build Tools detected.'; return }
    if ($NoInstall) { throw 'Visual Studio 2022 C++ Build Tools are missing and -NoInstall was specified.' }
    $winget = Ensure-Winget
    Write-Step 'Installing Visual Studio 2022 C++ Build Tools'
    Write-Host 'This is a large installation and may take several minutes.'
    $override = '--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
    Invoke-NativeChecked -Exe $winget -Arguments @(
        'install','--id','Microsoft.VisualStudio.2022.BuildTools','--exact',
        '--accept-package-agreements','--accept-source-agreements','--disable-interactivity',
        '--override',$override
    )
    Refresh-ProcessPath
    if (-not (Test-VsCppToolchain)) {
        try { Start-Process 'https://visualstudio.microsoft.com/downloads/' | Out-Null } catch {}
        throw @'
Visual Studio Build Tools was installed but the C++ x64/x86 toolchain was not detected.
Open Visual Studio Installer -> Build Tools 2022 -> Modify, then install:
  - Desktop development with C++
  - MSVC v143 C++ x64/x86 build tools
  - Windows 10/11 SDK
  - C++ CMake tools for Windows
Then rerun AUTO_BUILD.bat.
'@
    }
    Write-Ok 'Visual Studio 2022 C++ Build Tools installed.'
}

function Get-PythonVersion {
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if (-not $python) { return $null }
    try {
        $v = & $python.Source -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}')" 2>$null
        return [version]($v | Select-Object -First 1)
    } catch { return $null }
}

function Ensure-Python {
    $v = Get-PythonVersion
    if ($v -and $v -ge [version]'3.11') { Write-Ok "Python $v detected."; return }
    if ($NoInstall) { throw 'Python 3.11+ is missing and -NoInstall was specified.' }
    $winget = Ensure-Winget
    Write-Step 'Installing Python 3.12 x64'
    Invoke-NativeChecked -Exe $winget -Arguments @(
        'install','--id','Python.Python.3.12','--exact',
        '--accept-package-agreements','--accept-source-agreements','--disable-interactivity'
    )
    Refresh-ProcessPath
    $v = Get-PythonVersion
    if (-not $v -or $v -lt [version]'3.11') {
        throw 'Python installation completed, but Python 3.11+ is still unavailable in PATH. Close this console and rerun AUTO_BUILD.bat.'
    }
    Write-Ok "Python $v installed."
}

function Ensure-VcRuntime {
    Write-Step 'Checking Microsoft Visual C++ runtime'
    $dll = Join-Path $env:WINDIR 'System32\msvcp140.dll'
    $current = $null
    if (Test-Path $dll) {
        try { $current = [version]((Get-Item $dll).VersionInfo.FileVersion.Split(' ')[0]) } catch {}
    }
    if ($current) { Write-Host "MSVCP140.dll: $current" }
    if ($current -and $current -ge [version]'14.44.0.0') { Write-Ok 'VC++ runtime is new enough for the VS2022 toolset used by this project.'; return }
    if ($NoInstall) { Write-Warn 'VC++ runtime is old/missing and -NoInstall was specified.'; return }
    $tmp = Join-Path $env:TEMP 'vc_redist.x64.exe'
    Write-Host 'Downloading latest Microsoft VC++ x64 Redistributable...'
    Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile $tmp -UseBasicParsing
    $p = Start-Process -FilePath $tmp -ArgumentList '/install','/quiet','/norestart' -Wait -PassThru
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    if ($p.ExitCode -notin @(0,1638,3010)) { throw "VC++ Redistributable installer failed with exit code $($p.ExitCode)." }
    Write-Ok 'VC++ x64 Redistributable installed/updated.'
}

function Show-GpuAndDriverStatus {
    Write-Step 'Detecting GPU / NVIDIA driver'
    $gpus = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue)
    if ($gpus.Count -eq 0) { Write-Warn 'No display adapter was returned by WMI.'; return }
    foreach ($g in $gpus) {
        Write-Host ("GPU: {0} | Driver: {1}" -f $g.Name,$g.DriverVersion)
    }
    $nvidia = @($gpus | Where-Object { $_.Name -match 'NVIDIA|GeForce|RTX' })
    if ($nvidia.Count -eq 0) {
        Write-Warn 'No NVIDIA GPU was detected. The project can compile, but DLSSNR runtime processing requires a supported NVIDIA GPU.'
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
        Write-Warn 'nvidia-smi was not found. A driver may still be installed, but automatic validation is limited.'
        $hasDriverVersion = @($nvidia | Where-Object { -not [string]::IsNullOrWhiteSpace($_.DriverVersion) }).Count -gt 0
        if (-not $hasDriverVersion) {
            Write-Warn 'NVIDIA display driver appears to be missing. Driver installation is intentionally manual.'
            try { Start-Process 'https://www.nvidia.com/Download/index.aspx' | Out-Null } catch {}
            throw 'Install the NVIDIA display driver from the NVIDIA page that was opened, reboot Windows, then rerun AUTO_BUILD.bat.'
        }
    }
    $names = ($nvidia | ForEach-Object Name) -join ' / '
    if ($names -match 'RTX\s*40|4090|4080|4070|4060|4050') {
        Write-Warn 'RTX 40/Ada detected. Current experimental DLSSNR Feature 18 runtimes may return 0xBAD00001 (FeatureNotSupported). This does not prevent source compilation.'
    } elseif ($names -match 'RTX\s*50|5090|5080|5070|5060|5050') {
        Write-Ok 'RTX 50/Blackwell detected. Runtime support still depends on the selected nvngx_dlssnr.dll and NVIDIA driver.'
    }
}

function Select-DlssNrRuntime {
    if ($SkipRuntimePrompt) { return $null }
    Write-Step 'Select DLSSNR runtime'
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Filter = 'NVIDIA DLSS Neural Rendering runtime (nvngx_dlssnr.dll)|nvngx_dlssnr.dll|DLL files (*.dll)|*.dll'
        $dialog.Title = 'Select nvngx_dlssnr.dll for Crow-DLSS5-Video-Image-Converter'
        $dialog.InitialDirectory = $Root
        $dialog.CheckFileExists = $true
        $dialog.Multiselect = $false
        $result = $dialog.ShowDialog()
        if ($result -ne [System.Windows.Forms.DialogResult]::OK) {
            Write-Warn 'DLSSNR runtime selection was cancelled. Build will continue without importing a runtime.'
            return $null
        }
        $file = $dialog.FileName
        if ([IO.Path]::GetFileName($file).ToLowerInvariant() -ne 'nvngx_dlssnr.dll') {
            throw 'The selected file must be named nvngx_dlssnr.dll.'
        }
        $sig = Get-AuthenticodeSignature $file
        $hash = (Get-FileHash $file -Algorithm SHA256).Hash
        $ver = (Get-Item $file).VersionInfo.FileVersion
        Write-Host "Runtime: $file"
        Write-Host "Version: $ver"
        Write-Host "SHA256 : $hash"
        Write-Host "Signature: $($sig.Status)"
        if ($sig.Status -ne 'Valid') { Write-Warn 'Selected DLL does not currently report a valid Authenticode signature. Verify its provenance before use.' }
        return $file
    } catch {
        Write-Warn ('Runtime picker failed: ' + $_.Exception.Message)
        return $null
    }
}

try {
    Start-Transcript -Path $LogPath -Append | Out-Null
} catch {}

try {
    Write-Host 'Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Native NVOF D3D12 Execute / Adaptive Stable Motion - Automatic Build Bootstrap' -ForegroundColor White
    Write-Host "Project: $Root"
    Write-Host "Log    : $LogPath"

    # AUTO_BUILD.bat already launches with -ExecutionPolicy Bypass. This makes the
    # current process explicit without permanently changing CurrentUser/LocalMachine.
    try {
        Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force -ErrorAction Stop
        Write-Ok 'PowerShell execution policy for this process: Bypass'
    } catch {
        Write-Warn ('Could not set Process execution policy: ' + $_.Exception.Message)
        Write-Warn 'AUTO_BUILD.bat also uses -ExecutionPolicy Bypass. If Group Policy blocks scripts, use an administrator-managed exception.'
    }

    if (-not $NoElevation -and -not (Test-Administrator)) {
        Write-Step 'Administrator permission required'
        Write-Host 'Build tool / VC++ runtime installation may require elevation. Requesting UAC... (AUTO_BUILD.bat normally handles this before PowerShell starts.)'
        $shell = (Get-Process -Id $PID).Path
        if (-not $shell) { $shell = 'powershell.exe' }
        $argList = @('-NoLogo','-NoProfile','-ExecutionPolicy','Bypass','-File',('"' + $PSCommandPath + '"'),'-NoElevation')
        if ($Portable) { $argList += '-Portable' }
        if ($SkipAutoDepth) { $argList += '-SkipAutoDepth' }
        if ($SkipVideo) { $argList += '-SkipVideo' }
        if ($SkipRuntimePrompt) { $argList += '-SkipRuntimePrompt' }
        if ($NoInstall) { $argList += '-NoInstall' }
        try {
            $p = Start-Process -FilePath $shell -ArgumentList ($argList -join ' ') -Verb RunAs -Wait -PassThru
            exit $p.ExitCode
        } catch {
            throw @'
Administrator elevation was cancelled or failed.
Run AUTO_BUILD.bat as Administrator, or run:
  powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\auto_build.ps1
from an elevated PowerShell window.
'@
        }
    }

    Write-Step 'System overview'
    Write-Host "Windows: $([Environment]::OSVersion.VersionString)"
    Write-Host "64-bit OS: $([Environment]::Is64BitOperatingSystem)"
    if (-not [Environment]::Is64BitOperatingSystem) { throw 'This project requires 64-bit Windows.' }
    Show-GpuAndDriverStatus

    Refresh-ProcessPath
    Write-Step 'Checking build prerequisites'
    Ensure-WingetPackage -Id 'Git.Git' -DisplayName 'Git' -CommandName 'git.exe'
    Ensure-WingetPackage -Id 'Kitware.CMake' -DisplayName 'CMake' -CommandName 'cmake.exe'
    Ensure-VisualStudioBuildTools
    Ensure-Python
    Ensure-VcRuntime
    Refresh-ProcessPath

    Write-Ok ('Git   : ' + (& git --version))
    Write-Ok ('CMake : ' + ((& cmake --version | Select-Object -First 1)))
    $pyv = Get-PythonVersion
    Write-Ok ("Python: $pyv")

    # Let the user choose the proprietary/experimental runtime. The project does
    # not download it automatically.
    $runtime = Select-DlssNrRuntime

    Write-Step 'Compiling source'
    $buildScript = Join-Path $Root 'scripts\build.ps1'
    # IMPORTANT: do not pass '-Clean' through an array splat to a PowerShell script.
    # Array splatting is positional here, so '-Clean' can bind to $Configuration,
    # producing `cmake --build ... --config -Clean`. Invoke the switch explicitly.
    $buildParams = @{ Clean = $true; Configuration = 'Release' }
    if ($Portable) { $buildParams.Portable = $true }
    & $buildScript @buildParams

    if ($runtime) {
        Write-Step 'Importing selected DLSSNR runtime'
        & (Join-Path $Root 'scripts\import_runtime.ps1') -Source $runtime
    }

    if (-not $SkipVideo) {
        Write-Step 'Setting up FFmpeg for video converter'
        # Run the dist copy so FFmpeg is installed where the compiled video GUI expects it.
        $videoSetup = Join-Path $Root 'dist\video\setup_video.ps1'
        if (-not (Test-Path $videoSetup)) { throw "Video setup script was not copied to dist: $videoSetup" }
        & $videoSetup
        if ($LASTEXITCODE -ne 0) { throw "Video dependency setup failed with exit code $LASTEXITCODE" }
    }

    if (-not $SkipAutoDepth) {
        Write-Step 'Setting up Auto Depth / Temporal runtime'
        # Run the dist copy so .venv is created under dist\auto_depth.
        $autoDepthSetup = Join-Path $Root 'dist\auto_depth\setup_auto_depth.ps1'
        if (-not (Test-Path $autoDepthSetup)) { throw "Auto Depth setup script was not copied to dist: $autoDepthSetup" }
        & $autoDepthSetup
        if ($LASTEXITCODE -ne 0) { throw "Auto Depth / Temporal setup failed with exit code $LASTEXITCODE" }
    }

    Write-Step 'Final verification'
    $required = @(
        (Join-Path $Root 'dist\Crow-DLSS5-Video-Image-Converter-CLI.exe'),
        (Join-Path $Root 'dist\Crow-DLSS5-Video-Image-Converter-Image.exe'),
        (Join-Path $Root 'dist\Crow-DLSS5-Video-Image-Converter-Video.exe')
    )
    foreach ($f in $required) {
        if (-not (Test-Path $f)) { throw "Missing compiled output: $f" }
        Write-Ok $f
    }
    $runtimeDest = Join-Path $Root 'dist\runtime\nvngx_dlssnr.dll'
    if (Test-Path $runtimeDest) { Write-Ok "DLSSNR runtime: $runtimeDest" }
    else { Write-Warn 'DLSSNR runtime was not imported. Run scripts\import_runtime.ps1 before processing.' }

    Write-Host ''
    Write-Host '============================================================' -ForegroundColor Green
    Write-Host 'AUTOMATIC BUILD COMPLETE' -ForegroundColor Green
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
