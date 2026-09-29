from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
cmake=(ROOT/'CMakeLists.txt').read_text(encoding='utf-8')
cn=(ROOT/'scripts/build_cn.ps1').read_text(encoding='utf-8-sig')
global_build=(ROOT/'scripts/build.ps1').read_text(encoding='utf-8-sig')
for target in ('test-nvof-postprocess','test-temporal-motion-consensus','test-spatial-motion-consensus'):
    assert f'add_executable({target} EXCLUDE_FROM_ALL' in cmake
assert 'cmake-build-' in cn and 'Tee-Object -FilePath $LogFile -Append' in cn
assert "'--verbose'" in cn
assert 'Get-Content -LiteralPath $LogFile -Tail 120' in cn
assert 'cmake-build-' in global_build and 'Tee-Object -FilePath $LogFile -Append' in global_build
assert 'Get-Content -LiteralPath $LogFile -Tail 120' in global_build
print('PASS: V0.7.2-alpha4 build diagnostics hotfix contract')

for target in ('crow-video-gui','crow-fg-gui','crow-nvof-selftest'):
    assert target in cn and target in global_build
