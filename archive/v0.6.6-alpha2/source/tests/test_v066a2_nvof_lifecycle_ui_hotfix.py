from pathlib import Path

root = Path(__file__).resolve().parents[1]
gui = (root/'src/video/VideoGuiApp.cpp').read_text(encoding='utf-8-sig')
bridge = (root/'src/video/NvofD3D12Bridge.cpp').read_text(encoding='utf-8-sig')
session_h = (root/'src/video/NvofFlowSession.h').read_text(encoding='utf-8-sig')
session_cpp = (root/'src/video/NvofFlowSession.cpp').read_text(encoding='utf-8-sig')
converter = (root/'src/video/VideoConverter.cpp').read_text(encoding='utf-8-sig')
settings = (root/'src/video/VideoConverter.h').read_text(encoding='utf-8-sig')
config = (root/'src/video/NvofConfig.h').read_text(encoding='utf-8-sig')

# Cancellation must not issue thread-wide CancelSynchronousIo against a worker that can be
# inside nvofapi64.dll / D3D12 driver calls.
assert 'CancelSynchronousIo' not in gui
assert 'Cancelling safely... current GPU/FFmpeg operation will finish before teardown.' in gui

# Teardown follows NVIDIA D3D12 order: wait/drain -> unregister -> free client resources -> destroy.
shutdown = bridge.split('void Shutdown() noexcept', 1)[1].split('[[noreturn]]', 1)[0]
assert 'WaitFor(fenceValue)' in shutdown and 'd3d.Flush()' in shutdown
assert shutdown.index('Unregister(flowHandle)') < shutdown.index('flowTexture.Reset()')
assert shutdown.index('flowTexture.Reset()') < shutdown.index('api.nvOFDestroy(handle)')
assert 'shuttingDown' in bridge
assert 'nvofFlow.reset();' in converter

# User-facing NVOF controls and persistence.
for token in [
    'IDC_NVOF_QUALITY', 'IDC_NVOF_GRID', 'IDC_NVOF_TEMPORAL_HINTS', 'IDC_NVOF_OUTPUT_COST',
    'L"nvof_quality"', 'L"nvof_grid"', 'L"nvof_temporal_hints"', 'L"nvof_output_cost"',
    'L"Quality / Slow"', 'L"Balanced / Medium"', 'L"Performance / Fast"',
    'L"4x4 - Validated Stable"', 'L"2x2 - Finer"', 'L"1x1 - Finest"'
]:
    assert token in gui, token

for token in ['NvofSettings nvof;', '#include "NvofConfig.h"']:
    assert token in settings, token
for token in ['outputGridSize = 4', 'temporalHints = true', 'outputCost = true']:
    assert token in config, token
assert '_settings.temporalHints' in session_cpp
assert 'settings.outputGridSize' in bridge
assert 'outputCostEnabled' in bridge
print('PASS: V0.6.6-alpha2 NVOF lifecycle / cancellation / tuning UI hotfix contract')
