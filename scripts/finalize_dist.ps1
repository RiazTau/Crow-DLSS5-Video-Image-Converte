param()
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Dist = Join-Path $Root 'dist'
$Tools = Join-Path $Dist 'tools'
$ToolsSource = Join-Path $Root 'tools\dist'
New-Item -ItemType Directory -Force -Path $Dist,$Tools | Out-Null

$toolExecutables = @(
    'Crow-DLSS-Rendering-Tool-CLI.exe',
    'Crow-DLSS-Rendering-Tool-FG-Diagnostic.exe',
    'Crow-DLSS-Rendering-Tool-Runtime-Self-Test.exe',
    'Crow-DLSS-Rendering-Tool-NVOF-Self-Test.exe',
    'Crow-DLSS-Rendering-Tool-NVOF-Execute-Self-Test.exe',
    'test-nvof-postprocess.exe'
)
foreach ($name in $toolExecutables) {
    $old = Join-Path $Dist $name
    $new = Join-Path $Tools $name
    if (Test-Path $old) { Move-Item -Force $old $new }
}

# No loose command scripts are allowed at the dist root.
foreach ($pattern in @('*.bat','*.cmd','*.ps1')) {
    Get-ChildItem -Path $Dist -File -Filter $pattern -ErrorAction SilentlyContinue | ForEach-Object {
        Move-Item -Force $_.FullName (Join-Path $Tools $_.Name)
    }
}

if (Test-Path $ToolsSource) {
    Get-ChildItem -Path $ToolsSource -File | ForEach-Object {
        Copy-Item -Force $_.FullName (Join-Path $Tools $_.Name)
    }
}

Write-Host '[OK] dist layout normalized.' -ForegroundColor Green
Write-Host '  Main applications : dist\*.exe'
Write-Host '  Diagnostics/tools : dist\tools\'
Write-Host '  Runtime           : dist\runtime\'
