from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
d3d_h = (ROOT / "src" / "D3D12Context.h").read_text(encoding="utf-8")
d3d_cpp = (ROOT / "src" / "D3D12Context.cpp").read_text(encoding="utf-8")
runner = (ROOT / "src" / "DlssNrRunner.cpp").read_text(encoding="utf-8")
runner_h = (ROOT / "src" / "DlssNrRunner.h").read_text(encoding="utf-8")
denoiser = (ROOT / "src" / "video" / "TemporalDenoiser.cpp").read_text(encoding="utf-8")
flow = (ROOT / "src" / "video" / "TemporalFlow.cpp").read_text(encoding="utf-8")
dis = (ROOT / "src" / "video" / "DisFlowVideo.cpp").read_text(encoding="utf-8")
parallel = (ROOT / "src" / "video" / "ParallelRows.h").read_text(encoding="utf-8")
converter = (ROOT / "src" / "video" / "VideoConverter.cpp").read_text(encoding="utf-8")
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

assert any(v in cmake for v in ["VERSION 0.6.3", "VERSION 0.6.4", "VERSION 0.6.5", "VERSION 0.6.5.1", "VERSION 0.6.5.2", "VERSION 0.6.5.3", "VERSION 0.6.6"])
assert "DLSS5_PERF_THREADS" in parallel
assert "std::jthread" in parallel
assert "ParallelForRows" in denoiser
assert "ParallelForRows" in flow
assert "ParallelForRows" in dis

# Persistent staging + one-submit hot path.
for token in [
    "CreateUploadTransferBuffer", "CreateReadbackTransferBuffer",
    "WriteUploadTransferBuffer", "RecordUploadTexture2D",
    "RecordReadbackTexture2D", "ReadTransferBuffer",
]:
    assert token in d3d_h and token in d3d_cpp

assert "ProcessInternalBatched" in runner_h and "ProcessInternalLegacy" in runner_h
assert "DLSS5_DISABLE_D3D12_BATCH" in runner
assert "D3D12 frame batching" in runner
batched = runner.split("Rgba8Image DlssNrRunner::ProcessInternalBatched", 1)[1].split(
    "Rgba8Image DlssNrRunner::ProcessInternalLegacy", 1
)[0]
assert batched.count("_d3d.ExecuteAndWait();") == 1, "batched hot path should submit/wait exactly once per frame"
assert "_d3d.UploadTexture2D(" not in batched
assert "_d3d.ReadbackTexture2D(" not in batched
assert "EvaluateRecorded" in batched

# The legacy V0.6.2 synchronization path remains available for rollback/A-B testing.
legacy = runner.split("Rgba8Image DlssNrRunner::ProcessInternalLegacy", 1)[1].split(
    "Rgba8Image DlssNrRunner::Process(", 1
)[0]
assert "_d3d.UploadTexture2D(" in legacy
assert "_d3d.ReadbackTexture2D(" in legacy

# Performance instrumentation must report the stages that can actually bottleneck video conversion.
assert "performance-last.csv" in converter
assert "performance-summary-last.txt" in converter
assert "std::swap(previousInput.pixels, input.pixels)" in converter
assert "std::swap(previousStableOutput.pixels, output.pixels)" in converter
assert "previousStableDepth = std::move(depth)" in converter
assert "previousInput = input;" not in converter
assert "previousStableOutput = output;" not in converter
for stage in ["decode_ms", "flow_ms", "denoise_ms", "depth_ms", "dlssnr_ms", "encode_ms", "total_ms"]:
    assert stage in converter

print("PASS: V0.6.3 performance uplift contract")
