# V0.7.0-alpha1 — FG Build Hotfix 1

## Symptom

`BUILD_FG.bat` could fail before dependency acquisition/compilation with an error similar to:

```text
Resolve-Path : Cannot find path '<project>\Release'
scripts\build.ps1:150
$Ngx = (Resolve-Path $NgxSdkDir).Path
```

## Root cause

The standalone FG wrapper forwarded named arguments to `build.ps1` using a normal PowerShell array:

```powershell
@('-Configuration', 'Release', '-Clean')
```

Array splatting is positional when passed to another PowerShell script. The tokens are not reparsed as a command-line string, so a value such as `Release` could be bound to the wrong parameter (`NgxSdkDir`).

## Fix

The wrapper now uses hashtable splatting:

```powershell
$buildParams = @{ Configuration = $Configuration }
if ($Clean) { $buildParams['Clean'] = $true }
& .\build.ps1 @buildParams
```

The same correction is applied to `setup_fg_runtime.ps1` forwarding.

## Scope

This is a build-wrapper-only hotfix. It does not change the DLSS-G, NVOF, video timeline, UI, or runtime processing implementation.
