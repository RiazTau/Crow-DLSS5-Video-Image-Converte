param([Parameter(Mandatory=$true)][ValidateSet('legacy_hook','direct')][string]$CallerMode, [string]$InstallRoot)
$ErrorActionPreference='Stop'
$ProjectRoot=Split-Path -Parent $PSScriptRoot
if(-not $InstallRoot){$InstallRoot=Join-Path $ProjectRoot 'dist'}
$RuntimeDir=Join-Path $InstallRoot 'runtime'
$Profile=Join-Path $RuntimeDir 'dlssnr-compat.ini'
if(-not (Test-Path $Profile)){throw "Compatibility profile not found: $Profile"}
$lines=Get-Content $Profile
$found=$false
$lines=$lines | ForEach-Object { if($_ -match '^\s*caller_mode\s*='){ $found=$true; "caller_mode=$CallerMode" } else { $_ } }
if(-not $found){$lines += "caller_mode=$CallerMode"}
$lines | Set-Content $Profile -Encoding ASCII
Write-Host "RTX40 experimental caller_mode set to $CallerMode" -ForegroundColor Green
