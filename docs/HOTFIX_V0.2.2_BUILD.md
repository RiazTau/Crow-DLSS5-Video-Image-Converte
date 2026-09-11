# V0.2.2 Build Hotfix

Fixes Windows PowerShell 5.1 argument forwarding in `scripts/build.ps1`.

## Symptom

After dependencies validate successfully, the build stopped with:

```text
Invoke-NativeChecked : 找不到与参数名称“S”匹配的参数。
```

## Cause

`Invoke-NativeChecked cmake -S ...` allowed PowerShell to parse `-S` as a parameter of the wrapper function instead of an argument for `cmake.exe`.

## Fix

All native calls now use explicit argument arrays:

```powershell
Invoke-NativeChecked -Exe "cmake" -Arguments @(
    "-S", $Root,
    "-B", $Build,
    "-A", "x64"
)
```

Git clone parameters are handled the same way.

## Rebuild

Your downloaded `.deps` are valid and do not need to be downloaded again. Run:

```powershell
.\scripts\build.ps1 -Clean
```
