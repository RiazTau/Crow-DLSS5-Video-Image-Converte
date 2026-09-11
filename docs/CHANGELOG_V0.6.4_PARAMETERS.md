# V0.6.4 Parameter Persistence / Anti-Warp Defaults

Base: V0.6.3 Performance Uplift Test.

## Default-profile change

The video GUI and `VideoSettings` factory defaults now use the balanced anti-warp profile: Flow Width 480, Scene Cut 0.28, DN Strength 0.72, DN History 0.76, DN Spatial 0.22, Detail Protect 0.86, Stable Depth ON, Stable Output OFF, MV scale 1.0/1.0.

## Persistent parameter profile

`Save Parameters` writes the current processing controls to `dist/video/video-parameters.ini`. The GUI loads this file automatically on startup. Input/output paths and runtime DLL selection are intentionally excluded.

## Per-parameter reset

Every persisted control has an adjacent Reset button backed by one centralized factory-default table. Reset affects one parameter only; there is no bulk reset. Reset does not silently overwrite the saved profile—press Save Parameters to persist it.

## Compatibility/performance boundaries

The V0.6.3 CPU parallelism, D3D12 batching, legacy synchronization fallback, performance profiler, V0.6.2 runtime compatibility profile, AV1 preflight, Full HQ denoise, DIS optical flow and Auto Depth paths are preserved.
