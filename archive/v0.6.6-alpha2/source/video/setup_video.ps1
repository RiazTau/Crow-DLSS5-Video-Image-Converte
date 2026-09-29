param([switch]$ForceDownload)
$ErrorActionPreference = 'Stop'

$VideoRoot = $PSScriptRoot
$TargetBin = Join-Path $VideoRoot 'ffmpeg\bin'
$Ffmpeg = Join-Path $TargetBin 'ffmpeg.exe'
$Ffprobe = Join-Path $TargetBin 'ffprobe.exe'

# V0.6.1.2 uses Gyan's full static build because the Essentials / ffmpeg-static
# build may omit libdav1d. libdav1d is our first-choice AV1 software decoder.
# Pin the archive and SHA-256 so AutoBuild does not silently change decoder
# behavior when an upstream 'latest' asset changes.
$FullBuildUrl = 'https://github.com/GyanD/codexffmpeg/releases/download/8.1.2/ffmpeg-8.1.2-full_build.zip'
$FullBuildSha256 = 'B8CDEFAB5F50590A076C27C2B56B0294A0E6154FADED28BA1BA05EBC4F801F57'

function Test-Executable([string]$Exe) {
    if (-not (Test-Path $Exe)) { return $false }
    try {
        & $Exe -version 2>$null | Select-Object -First 1 | Out-Null
        return ($LASTEXITCODE -eq 0)
    } catch { return $false }
}

function Get-Decoders([string]$Exe) {
    try { return (& $Exe -hide_banner -decoders 2>$null | Out-String) }
    catch { return '' }
}

function Show-Capabilities([string]$Exe) {
    $decoders = Get-Decoders $Exe
    $encoders = ''
    try { $encoders = (& $Exe -hide_banner -encoders 2>$null | Out-String) } catch {}
    Write-Host 'Decoder capability summary:' -ForegroundColor Cyan
    Write-Host ("  libdav1d   : {0}" -f ($(if ($decoders -match '(?m)^\s*V\S*\s+libdav1d\s') {'YES'} else {'NO'})))
    Write-Host ("  av1_cuvid  : {0}" -f ($(if ($decoders -match '(?m)^\s*V\S*\s+av1_cuvid\s') {'YES'} else {'NO'})))
    Write-Host ("  av1_qsv    : {0}" -f ($(if ($decoders -match '(?m)^\s*V\S*\s+av1_qsv\s') {'YES'} else {'NO'})))
    Write-Host ("  libaom-av1 : {0}" -f ($(if ($decoders -match '(?m)^\s*V\S*\s+libaom-av1\s') {'YES'} else {'NO'})))
    Write-Host ("  h264_nvenc : {0}" -f ($(if ($encoders -match 'h264_nvenc') {'YES'} else {'NO'})))
}

function Test-HasDav1d([string]$Exe) {
    $decoders = Get-Decoders $Exe
    return ($decoders -match '(?m)^\s*V\S*\s+libdav1d\s')
}

function Download-File([string]$Uri,[string]$Dest) {
    Remove-Item $Dest -Force -ErrorAction SilentlyContinue
    $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
    if ($curl) {
        & $curl.Source -L --fail --retry 3 --retry-delay 2 --connect-timeout 20 -o $Dest $Uri
        if ($LASTEXITCODE -ne 0) { Remove-Item $Dest -Force -ErrorAction SilentlyContinue; throw "curl failed for $Uri" }
    } else {
        try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
        Invoke-WebRequest -Uri $Uri -OutFile $Dest -UseBasicParsing -TimeoutSec 300
    }
    if (-not (Test-Path $Dest)) { throw "Download did not create $Dest" }
    if ((Get-Item $Dest).Length -lt 50MB) { throw "Downloaded FFmpeg archive is unexpectedly small: $Uri" }
    $hash = (Get-FileHash -LiteralPath $Dest -Algorithm SHA256).Hash.ToUpperInvariant()
    if ($hash -ne $FullBuildSha256) {
        Remove-Item $Dest -Force -ErrorAction SilentlyContinue
        throw "FFmpeg archive SHA-256 mismatch. Expected $FullBuildSha256 but received $hash"
    }
}

function Install-FullFfmpeg {
    New-Item -ItemType Directory -Force -Path $TargetBin | Out-Null
    $zip = Join-Path $VideoRoot 'ffmpeg-full-download.zip'
    $extract = Join-Path $VideoRoot 'ffmpeg-full-extract'
    Remove-Item $extract -Recurse -Force -ErrorAction SilentlyContinue
    try {
        Write-Host 'Downloading Gyan FFmpeg 8.1.2 full static build (libdav1d + NVDEC/NVENC)...' -ForegroundColor Cyan
        Download-File -Uri $FullBuildUrl -Dest $zip
        New-Item -ItemType Directory -Force -Path $extract | Out-Null
        Expand-Archive -Path $zip -DestinationPath $extract -Force
        $ffmpegSource = Get-ChildItem -Path $extract -Filter 'ffmpeg.exe' -File -Recurse | Select-Object -First 1
        $ffprobeSource = Get-ChildItem -Path $extract -Filter 'ffprobe.exe' -File -Recurse | Select-Object -First 1
        if (-not $ffmpegSource -or -not $ffprobeSource) { throw 'Downloaded archive did not contain ffmpeg.exe and ffprobe.exe.' }
        Copy-Item $ffmpegSource.FullName $Ffmpeg -Force
        Copy-Item $ffprobeSource.FullName $Ffprobe -Force
    } finally {
        Remove-Item $zip -Force -ErrorAction SilentlyContinue
        Remove-Item $extract -Recurse -Force -ErrorAction SilentlyContinue
    }
}

$localUsable = (Test-Executable $Ffmpeg) -and (Test-Executable $Ffprobe)
if (-not $ForceDownload -and $localUsable -and (Test-HasDav1d $Ffmpeg)) {
    & $Ffmpeg -version | Select-Object -First 1
    Write-Host 'Local full FFmpeg is already ready.' -ForegroundColor Green
    Show-Capabilities $Ffmpeg
    exit 0
}

$pathFfmpeg = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
$pathFfprobe = Get-Command ffprobe.exe -ErrorAction SilentlyContinue
$pathUsable = $false
if ($pathFfmpeg -and $pathFfprobe) {
    $pathUsable = (Test-Executable $pathFfmpeg.Source) -and (Test-Executable $pathFfprobe.Source)
}
if (-not $ForceDownload -and -not $localUsable -and $pathUsable -and (Test-HasDav1d $pathFfmpeg.Source)) {
    Write-Host 'A PATH FFmpeg with libdav1d is already available.' -ForegroundColor Green
    Show-Capabilities $pathFfmpeg.Source
    exit 0
}

if ($localUsable -and -not (Test-HasDav1d $Ffmpeg)) {
    Write-Warning 'Existing local FFmpeg does not advertise libdav1d. V0.6.1.2 will upgrade the local video runtime for robust AV1 fallback.'
}

try {
    Install-FullFfmpeg
} catch {
    if ($localUsable) {
        Write-Warning ('Full FFmpeg upgrade failed; keeping the existing local FFmpeg. ' + $_.Exception.Message)
        Write-Warning 'V0.6.1.2 decoder preflight can still use av1_cuvid/native/libaom when available, but difficult AV1 streams may require a manual full FFmpeg build with libdav1d.'
        Show-Capabilities $Ffmpeg
        exit 0
    }
    if ($pathUsable) {
        Write-Warning ('Full FFmpeg download failed; falling back to PATH FFmpeg. ' + $_.Exception.Message)
        Show-Capabilities $pathFfmpeg.Source
        exit 0
    }
    throw
}

if (-not (Test-Executable $Ffmpeg)) { throw 'ffmpeg.exe verification failed.' }
if (-not (Test-Executable $Ffprobe)) { throw 'ffprobe.exe verification failed.' }
& $Ffmpeg -version | Select-Object -First 1
Show-Capabilities $Ffmpeg
if (-not (Test-HasDav1d $Ffmpeg)) {
    Write-Warning 'The installed full build unexpectedly does not advertise libdav1d. AV1 preflight will continue with hardware/native fallbacks.'
}
Write-Host 'Video dependencies ready (V0.6.1.2 full FFmpeg).' -ForegroundColor Green
