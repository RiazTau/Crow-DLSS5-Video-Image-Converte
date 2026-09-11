# Mainland-China mirror configuration for the alternative AutoBuild path.
# Keep this file ASCII-only so Windows PowerShell 5.1 can parse it reliably.

$CNMirrors = [ordered]@{
    UstcRoot          = 'https://mirrors.ustc.edu.cn'
    PyPI              = 'https://mirrors.ustc.edu.cn/pypi/simple'
    PythonVersion     = '3.13.15'
    PythonInstaller   = 'https://mirrors.ustc.edu.cn/python/3.13.15/python-3.13.15-amd64.exe'
    GitReleaseIndex   = 'https://mirrors.ustc.edu.cn/github-release/git-for-windows/git/LatestRelease/'
    GitReleaseBase    = 'https://mirrors.ustc.edu.cn/github-release/git-for-windows/git/LatestRelease/'
    NgxGit            = 'https://gitee.com/mirrors_NVIDIA/DLSS.git'
    TinyExrGit        = 'https://gitee.com/mirrors_syoyo/tinyexr.git'
    FfmpegBase        = 'https://registry.npmmirror.com/-/binary/ffmpeg-static/b6.1.1'
    DepthModel        = 'https://hf-mirror.com/onnx-community/depth-anything-v2-small/resolve/c70d1ddbcd93c9bda8098268cc3554adf5e8dd4f/onnx/model_fp16.onnx?download=true'
}

function Invoke-CNDownload {
    param(
        [Parameter(Mandatory=$true)][string]$Uri,
        [Parameter(Mandatory=$true)][string]$OutFile,
        [int]$Retries = 3
    )
    $parent = Split-Path -Parent $OutFile
    if ($parent) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
    Remove-Item -LiteralPath $OutFile -Force -ErrorAction SilentlyContinue

    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & $curl.Source -L --fail --retry $Retries --retry-delay 2 --connect-timeout 15 -o $OutFile $Uri
        $curlCode = $LASTEXITCODE
        if ($curlCode -eq 0 -and (Test-Path -LiteralPath $OutFile)) { return }
        Remove-Item -LiteralPath $OutFile -Force -ErrorAction SilentlyContinue

        # Windows Schannel may report CRYPT_E_REVOCATION_OFFLINE when the CRL/
        # OCSP endpoint cannot be reached. Retry without disabling TLS or the
        # certificate chain: only make revocation-endpoint availability best-effort.
        Write-Warning ("curl failed with exit code {0}; retrying with --ssl-revoke-best-effort." -f $curlCode)
        & $curl.Source --ssl-revoke-best-effort -L --fail --retry $Retries --retry-delay 2 --connect-timeout 15 -o $OutFile $Uri
        $curlCode = $LASTEXITCODE
        if ($curlCode -eq 0 -and (Test-Path -LiteralPath $OutFile)) { return }
        Remove-Item -LiteralPath $OutFile -Force -ErrorAction SilentlyContinue
        Write-Warning ("curl best-effort revocation retry failed with exit code {0}; falling back to Invoke-WebRequest." -f $curlCode)
    }

    $last = $null
    for ($i = 1; $i -le $Retries; $i++) {
        try {
            Invoke-WebRequest -Uri $Uri -OutFile $OutFile -UseBasicParsing -TimeoutSec 120
            if (Test-Path -LiteralPath $OutFile) { return }
        } catch {
            $last = $_.Exception
            Remove-Item -LiteralPath $OutFile -Force -ErrorAction SilentlyContinue
            if ($i -lt $Retries) { Start-Sleep -Seconds ([Math]::Min(2 * $i, 6)) }
        }
    }
    if ($last) { throw $last }
    throw "Download failed: $Uri"
}
