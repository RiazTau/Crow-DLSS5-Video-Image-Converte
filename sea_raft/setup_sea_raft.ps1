param(
    [switch]$Force,
    [switch]$ChinaMirror,
    [switch]$SkipModelPrefetch,
    [string]$RepoUrl = ''
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Venv = Join-Path $Root '.venv'
$Py = Join-Path $Venv 'Scripts\python.exe'
$Vendor = Join-Path $Root 'vendor\SEA-RAFT'
$Models = Join-Path $Root 'models'
if ((Test-Path (Join-Path $Root 'china-mirror.flag')) -and -not $ChinaMirror) { $ChinaMirror = $true }
$OfficialRepo = 'https://github.com/princeton-vl/SEA-RAFT.git'
$MirrorRepo = 'https://gitcode.com/gh_mirrors/se/SEA-RAFT.git'
$CudaIndex = 'https://download.pytorch.org/whl/cu130'
$CnCudaFindLinks = 'https://mirrors.aliyun.com/pytorch-wheels/cu130/'
$PyPi = if ($ChinaMirror) { 'https://mirrors.aliyun.com/pypi/simple/' } else { 'https://pypi.org/simple' }
if (-not $RepoUrl) { $RepoUrl = if ($ChinaMirror) { $MirrorRepo } else { $OfficialRepo } }
if ($ChinaMirror) { $env:HF_ENDPOINT = 'https://hf-mirror.com' }

function Invoke-Native([string]$Exe,[string[]]$ArgumentList) {
    Write-Host ('> ' + $Exe + ' ' + ($ArgumentList -join ' ')) -ForegroundColor DarkGray
    $old = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Exe @ArgumentList
        $rc = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $old
    }
    if ($rc -ne 0) { throw "$Exe failed with exit code $rc" }
}

function Find-CompatiblePython {
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($py) {
        foreach ($ver in @('-3.13','-3.12','-3.11','-3.10')) {
            & $py.Source $ver -c "import sys; raise SystemExit(0 if (3,10) <= sys.version_info[:2] <= (3,13) else 1)" 2>$null
            if ($LASTEXITCODE -eq 0) {
                return [pscustomobject]@{Exe=$py.Source; Prefix=@($ver); Name="py $ver"}
            }
        }
    }
    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) {
        & $python.Source -c "import sys; raise SystemExit(0 if (3,10) <= sys.version_info[:2] <= (3,13) else 1)" 2>$null
        if ($LASTEXITCODE -eq 0) {
            return [pscustomobject]@{Exe=$python.Source; Prefix=@(); Name=$python.Source}
        }
    }
    throw 'SEA-RAFT requires Python 3.10-3.13.'
}

if ($Force -and (Test-Path $Venv)) { Remove-Item -Recurse -Force $Venv }
if (-not (Test-Path $Py)) {
    $p = Find-CompatiblePython
    Write-Host "Creating SEA-RAFT venv with $($p.Name)..." -ForegroundColor Cyan
    Invoke-Native $p.Exe (@($p.Prefix) + @('-m','venv',$Venv))
    if (-not (Test-Path -LiteralPath $Py)) {
        throw "SEA-RAFT venv creation returned success but python.exe is missing: $Py"
    }
}

Invoke-Native $Py @('-m','pip','install','--upgrade','pip')

# IMPORTANT: torch/torchvision are deliberately absent from requirements.txt. Installing
# them from ordinary PyPI can replace a working CUDA build with a CPU-only wheel.
function Install-CudaTorchPackages([string[]]$Packages) {
    if ($ChinaMirror) {
        # mirrors.aliyun.com/pytorch-wheels is a wheel directory, not a PEP 503 package
        # index. Use --find-links rather than --index-url; fall back to the official
        # PyTorch CUDA channel if the mirror does not expose a compatible wheel.
        Write-Host ('Trying CN CUDA wheel mirror: ' + $CnCudaFindLinks) -ForegroundColor Cyan
        try {
            Invoke-Native $Py (@('-m','pip','install','--no-index','--find-links',$CnCudaFindLinks) + $Packages)
            return
        } catch {
            Write-Warning ('Aliyun CUDA wheel mirror failed; falling back to official PyTorch cu130 channel: ' + $_.Exception.Message)
        }
    }
    Invoke-Native $Py (@('-m','pip','install') + $Packages + @('--index-url',$CudaIndex))
}

$cudaOk = $false
try {
    & $Py -c "import torch; raise SystemExit(0 if torch.cuda.is_available() else 2)"
    $cudaOk = ($LASTEXITCODE -eq 0)
} catch {}

$torchvisionOk = $false
if ($cudaOk) {
    try {
        & $Py -c "import torchvision; print('torchvision', torchvision.__version__)"
        $torchvisionOk = ($LASTEXITCODE -eq 0)
    } catch {}
}

if (-not $cudaOk) {
    Write-Host 'Installing CUDA-enabled PyTorch + torchvision (cu130)...' -ForegroundColor Cyan
    try { Invoke-Native $Py @('-m','pip','uninstall','-y','torch','torchvision','torchaudio') } catch {}
    Install-CudaTorchPackages @('torch','torchvision')
} elseif (-not $torchvisionOk) {
    # A previously repaired/manual environment can contain a valid CUDA torch build but
    # no torchvision. SEA-RAFT imports torchvision.models for its ResNet backbone, so
    # repair torchvision explicitly instead of incorrectly treating CUDA torch alone as ready.
    Write-Host 'CUDA PyTorch is present but torchvision is missing/broken; repairing torchvision...' -ForegroundColor Cyan
    Install-CudaTorchPackages @('torchvision')
}

if ($ChinaMirror) {
    try {
        Invoke-Native $Py @('-m','pip','install','-r',(Join-Path $Root 'requirements.txt'),'-i',$PyPi)
    } catch {
        Write-Warning ('Aliyun PyPI failed; falling back to official PyPI: ' + $_.Exception.Message)
        Invoke-Native $Py @('-m','pip','install','-r',(Join-Path $Root 'requirements.txt'),'-i','https://pypi.org/simple')
    }
} else {
    Invoke-Native $Py @('-m','pip','install','-r',(Join-Path $Root 'requirements.txt'),'-i',$PyPi)
}
Invoke-Native $Py @('-c',"import torch, torchvision; print('PyTorch',torch.__version__); print('torchvision',torchvision.__version__); print('CUDA build:',torch.version.cuda); print('CUDA available:',torch.cuda.is_available()); print('GPU:',torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'NONE'); raise SystemExit(0 if torch.cuda.is_available() else 2)")

if (-not (Test-Path (Join-Path $Vendor 'core\raft.py'))) {
    $git = Get-Command git.exe -ErrorAction SilentlyContinue
    if (-not $git) { throw 'Git is required to fetch SEA-RAFT.' }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Vendor) | Out-Null
    if (Test-Path $Vendor) { Remove-Item -Recurse -Force $Vendor }
    try {
        Invoke-Native $git.Source @('clone','--depth','1',$RepoUrl,$Vendor)
    } catch {
        if ($RepoUrl -ne $OfficialRepo) {
            Write-Warning 'SEA-RAFT mirror failed; falling back to official GitHub.'
            if (Test-Path $Vendor) { Remove-Item -Recurse -Force $Vendor }
            Invoke-Native $git.Source @('clone','--depth','1',$OfficialRepo,$Vendor)
        } else { throw }
    }
}

New-Item -ItemType Directory -Force -Path $Models | Out-Null
if (-not $SkipModelPrefetch) {
    Write-Host 'Prefetching SEA-RAFT Spring S/M models...' -ForegroundColor Cyan
    $code = @"
from huggingface_hub import snapshot_download
from pathlib import Path
root=Path(r'$Models')
for repo,name in [
 ('MemorySlices/Tartan-C-T-TSKH-spring540x960-S','spring-S'),
 ('MemorySlices/Tartan-C-T-TSKH-spring540x960-M','spring-M')]:
    snapshot_download(repo_id=repo, local_dir=str(root/name), allow_patterns=['*.safetensors','*.json','README.md'])
print('SEA-RAFT models ready:', root)
"@
    try {
        Invoke-Native $Py @('-c',$code)
    } catch {
        if ($ChinaMirror) {
            Write-Warning ('hf-mirror model prefetch failed; retrying official Hugging Face: ' + $_.Exception.Message)
            $oldEndpoint = $env:HF_ENDPOINT
            try {
                $env:HF_ENDPOINT = 'https://huggingface.co'
                Invoke-Native $Py @('-c',$code)
            } catch {
                Write-Warning ('Official Hugging Face model prefetch also failed; first SEA-RAFT run can retry: ' + $_.Exception.Message)
            } finally {
                $env:HF_ENDPOINT = $oldEndpoint
            }
        } else {
            Write-Warning ('Model prefetch failed; first SEA-RAFT run can retry via Hugging Face: ' + $_.Exception.Message)
        }
    }
}

# Validate cached official Hub checkpoints against the exact vendored SEA-RAFT source before
# declaring the runtime ready. This catches model/source loader mismatches during setup instead
# of surfacing them later as a video conversion pipe failure. The worker deliberately mirrors
# upstream PyTorchModelHubMixin semantics (strict=False) for published safetensors checkpoints.
$Worker = Join-Path $Root 'sea_raft_video.py'
foreach ($entry in @(
    @{ Name='Spring-S'; Cfg=(Join-Path $Root 'spring-S.json'); ModelDir=(Join-Path $Models 'spring-S') },
    @{ Name='Spring-M'; Cfg=(Join-Path $Root 'spring-M.json'); ModelDir=(Join-Path $Models 'spring-M') }
)) {
    $modelFile = Join-Path $entry.ModelDir 'model.safetensors'
    if (Test-Path $modelFile) {
        Write-Host ("Validating SEA-RAFT {0} checkpoint load..." -f $entry.Name) -ForegroundColor Cyan
        Invoke-Native $Py @($Worker,'--repo',$Vendor,'--cfg',$entry.Cfg,'--url',$entry.ModelDir,'--device','cuda','--check-only')
    } else {
        Write-Warning ("SEA-RAFT {0} cached checkpoint is not present; model-load validation is deferred until first use." -f $entry.Name)
    }
}

Write-Host ''
Write-Host 'SEA-RAFT runtime ready.' -ForegroundColor Green
Write-Host "Runtime: $Root"
Write-Host 'Models: Spring-S / Spring-M; default Spring-M.'
