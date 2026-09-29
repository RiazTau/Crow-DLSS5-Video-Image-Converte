param([switch]$Force)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Venv = Join-Path $Root '.venv'
$Py = Join-Path $Venv 'Scripts\python.exe'
$Req = Join-Path $Root 'requirements.txt'
$Index = 'https://mirrors.ustc.edu.cn/pypi/simple'

function Test-Python311([string]$Exe,[string[]]$PrefixArgs=@()) {
    try {
        & $Exe @PrefixArgs -c "import sys; raise SystemExit(0 if sys.version_info >= (3,11) else 1)"
        return $LASTEXITCODE -eq 0
    } catch { return $false }
}

function Find-Python {
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py -and (Test-Python311 -Exe $py.Source -PrefixArgs @('-3'))) {
        return [pscustomobject]@{ Exe=$py.Source; PrefixArgs=@('-3'); Display='py -3' }
    }
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python -and (Test-Python311 -Exe $python.Source)) {
        return [pscustomobject]@{ Exe=$python.Source; PrefixArgs=@(); Display=$python.Source }
    }
    $known = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe')
    )
    foreach ($p in $known) {
        if ((Test-Path $p) -and (Test-Python311 -Exe $p)) {
            return [pscustomobject]@{ Exe=$p; PrefixArgs=@(); Display=$p }
        }
    }
    throw 'Python 3.11 or newer was not found. Run AUTO_BUILD_CN.bat to install Python from the USTC mirror.'
}

function Test-VenvPython {
    if (-not (Test-Path $Py)) { return $false }
    try {
        & $Py -c "import sys; raise SystemExit(0 if sys.version_info >= (3,11) else 1)" 2>$null
        return $LASTEXITCODE -eq 0
    } catch { return $false }
}

if ($Force -and (Test-Path $Venv)) { Remove-Item -Recurse -Force $Venv }
if ((Test-Path $Venv) -and -not (Test-VenvPython)) {
    Write-Warning 'Existing virtual environment is invalid or belongs to another PC. Rebuilding it.'
    Remove-Item -Recurse -Force $Venv
}
if (-not (Test-Path $Py)) {
    $cmd = Find-Python
    Write-Host ('Using Python command: ' + $cmd.Display)
    & $cmd.Exe @($cmd.PrefixArgs) -m venv $Venv
    if ($LASTEXITCODE -ne 0) { throw 'Failed to create Auto Depth virtual environment.' }
}

Write-Host 'Installing Auto Depth / Temporal Python packages from USTC PyPI...' -ForegroundColor Cyan
& $Py -m pip install --upgrade pip -i $Index --disable-pip-version-check
if ($LASTEXITCODE -ne 0) { throw 'pip upgrade failed.' }
& $Py -m pip install -r $Req -i $Index --disable-pip-version-check
if ($LASTEXITCODE -ne 0) { throw 'Auto Depth dependency installation failed.' }
& $Py -c "import cv2, onnxruntime, numpy, PIL; print('Python runtime OK | OpenCV', cv2.__version__); print(onnxruntime.get_available_providers())"
if ($LASTEXITCODE -ne 0) { throw 'Auto Depth / Temporal Python dependency verification failed.' }
Write-Host 'Auto Depth / Temporal runtime ready (USTC PyPI).' -ForegroundColor Green
