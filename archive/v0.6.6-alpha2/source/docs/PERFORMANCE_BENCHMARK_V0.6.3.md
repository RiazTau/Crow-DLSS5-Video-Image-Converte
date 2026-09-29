# V0.6.3 development microbenchmark

This is a development-host CPU microbenchmark, **not** an RTX40/RTX50 Windows performance claim.

Synthetic 1280x720 Full HQ TemporalDenoiser, GCC `-O3 -march=native`, three measured passes after warm-up:

| Worker setting | Average denoise time |
| --- | ---: |
| `DLSS5_PERF_THREADS=1` | 181.4 ms |
| `DLSS5_PERF_THREADS=8` | 51.6 ms |
| `DLSS5_PERF_THREADS=16` | 44.8 ms |

The 16-worker result is about **4.0x faster** for this isolated CPU stage. Real Windows video throughput will depend on resolution, CPU topology, Auto Depth, optical flow, DLSSNR runtime, encoder and memory bandwidth.

The D3D12 optimization cannot be benchmarked in the Linux development container. Source-level regression checks verify that the new batched hot path records one `ExecuteAndWait()` per frame, while the V0.6.2 legacy path remains available for A/B testing.
