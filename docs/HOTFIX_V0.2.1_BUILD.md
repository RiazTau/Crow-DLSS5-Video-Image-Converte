# V0.2.1 Fresh Build Hotfix

This hotfix addresses the V0.2 fresh-build failure where `git clone` failed but Windows PowerShell 5.1 continued into CMake.

## Fresh online build

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1 -Clean
```

The build now:

1. Creates `<project>\.deps` explicitly.
2. Retries each Git clone up to 3 times with HTTP/1.1.
3. Falls back to GitHub codeload ZIP archives.
4. Validates the expected NGX header/library and TinyEXR/miniz files.
5. Runs CMake only after both dependency checks pass.

## Fresh manual/offline build

If GitHub cannot be reached from the build machine, download the two dependency source trees using another connection, extract them anywhere, then run:

```powershell
.\scripts\build.ps1 -Clean `
  -NgxSdkDir "D:\Deps\DLSS" `
  -TinyExrDir "D:\Deps\tinyexr"
```

The NGX directory must contain:

```text
include\nvsdk_ngx.h
lib\Windows_x86_64\x64\nvsdk_ngx_d.lib
```

(or the supported `x86_64` library-layout alternative).

The TinyEXR directory must contain:

```text
tinyexr.h
deps\miniz\miniz.h
deps\miniz\miniz.c
```

`nvngx_dlssnr.dll` remains a runtime dependency imported separately after compilation.
