param([string]$InstallRoot)
$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
if (-not $InstallRoot) { $InstallRoot = Join-Path $ProjectRoot 'dist' }
$Url = 'https://hf-mirror.com/onnx-community/depth-anything-v2-small/resolve/c70d1ddbcd93c9bda8098268cc3554adf5e8dd4f/onnx/model_fp16.onnx?download=true'
$Expected = '2df6223f206b5164e21f664ace61dabeb9bb6a49b8b5a3e00510b4807d0f5b04'
$Dir = Join-Path $InstallRoot 'models\depth_anything_v2'
$Dest = Join-Path $Dir 'model_fp16.onnx'
$Temp = "$Dest.download"
New-Item -ItemType Directory -Force -Path $Dir | Out-Null

if (Test-Path $Dest) {
    $hash = (Get-FileHash $Dest -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -eq $Expected) {
        Write-Host "Depth Anything V2 already ready: $Dest" -ForegroundColor Green
        exit 0
    }
    Write-Warning 'Existing model hash does not match; replacing it.'
}
Remove-Item $Temp -Force -ErrorAction SilentlyContinue
Write-Host 'Downloading pinned Depth Anything V2 FP16 model from hf-mirror.com...' -ForegroundColor Cyan
$curl = Get-Command curl.exe -ErrorAction SilentlyContinue
if ($curl) {
    & $curl.Source -L --fail --retry 3 --retry-delay 2 --connect-timeout 15 -o $Temp $Url
    if ($LASTEXITCODE -ne 0) { Remove-Item $Temp -Force -ErrorAction SilentlyContinue; throw 'Model download failed.' }
} else {
    Invoke-WebRequest -Uri $Url -OutFile $Temp -UseBasicParsing -TimeoutSec 300
}
$hash = (Get-FileHash $Temp -Algorithm SHA256).Hash.ToLowerInvariant()
if ($hash -ne $Expected) {
    Remove-Item $Temp -Force -ErrorAction SilentlyContinue
    throw "SHA256 mismatch. Expected $Expected, got $hash"
}
Move-Item $Temp $Dest -Force
Write-Host "Model installed and SHA256 verified: $Dest" -ForegroundColor Green
