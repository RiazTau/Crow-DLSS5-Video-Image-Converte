param([switch]$ForceDownload)
$ErrorActionPreference = 'Stop'
$VideoRoot = $PSScriptRoot
$TargetBin = Join-Path $VideoRoot 'ffmpeg\bin'
$Ffmpeg = Join-Path $TargetBin 'ffmpeg.exe'
$Ffprobe = Join-Path $TargetBin 'ffprobe.exe'
$Base = 'https://registry.npmmirror.com/-/binary/ffmpeg-static/b6.1.1'
$FfmpegUrl = "$Base/ffmpeg-win32-x64"
$FfprobeUrl = "$Base/ffprobe-win32-x64"


function Show-DecoderCapabilities([string]$Exe) {
    $decoders = ''
    try { $decoders = (& $Exe -hide_banner -decoders 2>$null | Out-String) } catch {}
    Write-Host 'AV1 decoder capability summary:' -ForegroundColor Cyan
    Write-Host ("  libdav1d   : {0}" -f ($(if ($decoders -match '(?m)^\s*V\S*\s+libdav1d\s') {'YES'} else {'NO'})))
    Write-Host ("  av1_cuvid  : {0}" -f ($(if ($decoders -match '(?m)^\s*V\S*\s+av1_cuvid\s') {'YES'} else {'NO'})))
    Write-Host ("  av1_qsv    : {0}" -f ($(if ($decoders -match '(?m)^\s*V\S*\s+av1_qsv\s') {'YES'} else {'NO'})))
    Write-Host ("  libaom-av1 : {0}" -f ($(if ($decoders -match '(?m)^\s*V\S*\s+libaom-av1\s') {'YES'} else {'NO'})))
    if ($decoders -notmatch '(?m)^\s*V\S*\s+libdav1d\s') {
        Write-Warning 'The mainland mirror FFmpeg does not advertise libdav1d. V0.6.1.2 will use av1_cuvid / av1_qsv / native AV1 / libaom with real multi-frame preflight. For AV1 files that still fail, manually replace video\ffmpeg\bin\ffmpeg.exe and ffprobe.exe with a full Windows FFmpeg build containing libdav1d.'
    }
}

function Invoke-CurlDownload([string]$CurlExe,[string]$Uri,[string]$OutFile,[switch]$BestEffortRevocation) {
    $args = @('-L','--fail','--retry','3','--retry-delay','2','--connect-timeout','15','-o',$OutFile,$Uri)
    if ($BestEffortRevocation) {
        # Windows' Schannel can fail with CRYPT_E_REVOCATION_OFFLINE when the
        # certificate revocation endpoint is temporarily unreachable. This
        # keeps certificate validation enabled while treating an unreachable
        # revocation distribution point as best-effort instead of fatal.
        $args = @('--ssl-revoke-best-effort') + $args
    }
    & $CurlExe @args
    return $LASTEXITCODE
}

function Download-Checked([string]$Uri,[string]$Dest) {
    $tmp = "$Dest.part"
    Remove-Item $tmp -Force -ErrorAction SilentlyContinue
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    $downloaded = $false
    if ($curl) {
        $code = Invoke-CurlDownload -CurlExe $curl.Source -Uri $Uri -OutFile $tmp
        if ($code -eq 0 -and (Test-Path $tmp)) {
            $downloaded = $true
        } else {
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
            Write-Warning ("curl failed with exit code {0}. Retrying with Schannel best-effort revocation checking..." -f $code)
            $code = Invoke-CurlDownload -CurlExe $curl.Source -Uri $Uri -OutFile $tmp -BestEffortRevocation
            if ($code -eq 0 -and (Test-Path $tmp)) {
                $downloaded = $true
            } else {
                Remove-Item $tmp -Force -ErrorAction SilentlyContinue
                Write-Warning ("curl retry failed with exit code {0}. Falling back to Invoke-WebRequest..." -f $code)
            }
        }
    }
    if (-not $downloaded) {
        try {
            Invoke-WebRequest -Uri $Uri -OutFile $tmp -UseBasicParsing -TimeoutSec 180
            $downloaded = Test-Path $tmp
        } catch {
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
            throw ("FFmpeg download failed after curl + Schannel best-effort + Invoke-WebRequest fallbacks: " + $_.Exception.Message)
        }
    }
    if (-not $downloaded -or -not (Test-Path $tmp)) { throw "Download did not create $tmp" }
    if ((Get-Item $tmp).Length -lt 10MB) { Remove-Item $tmp -Force; throw "Downloaded file is unexpectedly small: $Uri" }
    $bytes = [IO.File]::ReadAllBytes($tmp)
    if ($bytes.Length -lt 2 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        Remove-Item $tmp -Force
        throw "Downloaded file is not a Windows PE executable: $Uri"
    }
    Move-Item $tmp $Dest -Force
}

if (-not $ForceDownload) {
    if ((Test-Path $Ffmpeg) -and (Test-Path $Ffprobe)) {
        & $Ffmpeg -version | Select-Object -First 1
        & $Ffprobe -version | Select-Object -First 1
        Write-Host 'Local FFmpeg is already ready.' -ForegroundColor Green
        Show-DecoderCapabilities $Ffmpeg
        exit 0
    }
    $pathFfmpeg = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    $pathFfprobe = Get-Command ffprobe.exe -ErrorAction SilentlyContinue
    if ($pathFfmpeg -and $pathFfprobe) {
        Write-Host 'FFmpeg and ffprobe are already available through PATH.' -ForegroundColor Green
        Show-DecoderCapabilities $pathFfmpeg.Source
        exit 0
    }
}

New-Item -ItemType Directory -Force -Path $TargetBin | Out-Null
Write-Host 'Downloading FFmpeg 6.1.1 static binaries from npmmirror...' -ForegroundColor Cyan
Download-Checked -Uri $FfmpegUrl -Dest $Ffmpeg
Download-Checked -Uri $FfprobeUrl -Dest $Ffprobe

& $Ffmpeg -version | Select-Object -First 1
if ($LASTEXITCODE -ne 0) { throw 'ffmpeg.exe verification failed.' }
& $Ffprobe -version | Select-Object -First 1
if ($LASTEXITCODE -ne 0) { throw 'ffprobe.exe verification failed.' }

$encoders = (& $Ffmpeg -hide_banner -encoders 2>$null | Out-String)
if ($encoders -notmatch 'h264_nvenc') {
    Write-Warning 'This mirrored FFmpeg build does not advertise h264_nvenc. CPU H.264 may still work, but NVENC output will be unavailable.'
}
Show-DecoderCapabilities $Ffmpeg
Write-Host 'Video dependencies ready (npmmirror, V0.6.1.2 decoder preflight enabled).' -ForegroundColor Green
