from pathlib import Path

root = Path(__file__).resolve().parents[1]
runner_h = (root / 'src/DlssNrRunner.h').read_text(encoding='utf-8')
runner_cpp = (root / 'src/DlssNrRunner.cpp').read_text(encoding='utf-8')
compat_cpp = (root / 'src/RuntimeCompat.cpp').read_text(encoding='utf-8')
selftest = (root / 'src/tools/RuntimeSelfTest.cpp').read_text(encoding='utf-8')
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
importer = (root / 'scripts/import_runtime_rtx40.ps1').read_text(encoding='utf-8')

assert 'RuntimeCallerMode::LegacyHook' in compat_cpp, 'stable default must be legacy_hook'
assert 'if (std::filesystem::exists(result.configPath))' in compat_cpp, 'compatibility must be opt-in via sidecar'
assert 'if (_runtimeCompat.callerMode == RuntimeCallerMode::LegacyHook)' in runner_cpp
assert '_callerCompat.Install(_snippetModule)' in runner_cpp
assert 'Caller mode: direct (RTX40 compatibility experiment)' in runner_cpp
assert 'std::optional<RuntimeCallerMode> callerModeOverride = std::nullopt' in runner_h
assert 'RunChild(exe, runtime, RuntimeCallerMode::Direct)' in selftest
assert 'RunChild(exe, runtime, RuntimeCallerMode::LegacyHook)' in selftest
assert '--child' in selftest and 'CreateProcessW' in selftest, 'runtime tests must be crash-isolated'
assert 'runner.Process(input, nullptr)' in selftest, 'self-test must perform real Feature18 Evaluate'
assert 'crow-runtime-selftest' in cmake
assert 'RuntimeCompat.cpp' in cmake
assert 'profile=rtx40-community' in importer
assert 'caller_mode=$CallerMode' in importer
assert 'does not bundle, download, patch, or redistribute' in (root/'docs/CHANGELOG_V0.6.2_RTX40_COMPAT.md').read_text(encoding='utf-8')
print('PASS: V0.6.2 RTX40 compatibility isolation contract')
