# V0.1.1 — VS2022 NGX linker hotfix

## Symptom

The V0.1 build log may select:

`lib/Windows_x86_64/vs2010/x64/nvsdk_ngx_d.lib`

and then fail with `LNK2038` (`_MSC_VER 1600` vs modern MSVC) plus unresolved CRT symbols such as `__iob_func`.

## Cause

V0.1 recursively globbed all copies of `nvsdk_ngx_d.lib` and selected the first match. NVIDIA's repository keeps a historical VS2010 library alongside its current x64 import library.

## Fix

V0.1.1 explicitly selects, in order:

1. `lib/Windows_x86_64/x64/nvsdk_ngx_d.lib`
2. `lib/Windows_x86_64/x86_64/nvsdk_ngx_d.lib` (alternate SDK layout)

Any `vs2010` path is rejected.

## Upgrade an existing checkout

Extract V0.1.1 over the old project folder. Keep `.deps/NVIDIA-DLSS`; there is no need to download the SDK again.

Then run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1 -Clean
```

During CMake configure, confirm the line is similar to:

```text
-- NGX library: .../.deps/NVIDIA-DLSS/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib
```

It must **not** contain `vs2010`.
