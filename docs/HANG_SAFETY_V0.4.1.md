# Hang-safety notes

The original V0.4.0 still had indefinite-wait risks despite moving conversion to a background thread. V0.4.1 removes the most important UI and subprocess deadlock paths.

## Bounded operations
- ffprobe: 15 seconds per probe command.
- FFmpeg decoder no-output stall: 30 seconds.
- Auto Depth initial model/helper ready: 120 seconds.
- Auto Depth frame reply: 30 seconds.
- Decoder final process exit: 15 seconds.
- Encoder final process exit: 60 seconds.

## Cancellation
The Win32 GUI calls CancelSynchronousIo on the conversion/probe worker on Cancel/Close, allowing pending synchronous named/anonymous-pipe ReadFile/WriteFile calls to return instead of holding the UI shutdown path indefinitely.

## Remaining non-I/O risk
A GPU-driver-level hang inside a D3D12/DLSSNR call cannot be safely force-cancelled from user-mode code. This is a different class of failure from the old video-load/FFmpeg blocking issue.

> **V0.6.6-alpha2 NVOF note:** the main conversion worker no longer uses thread-wide `CancelSynchronousIo`. Once the worker can execute NVOF/D3D12 driver calls, broad thread I/O cancellation is unsafe for GPU-session teardown. V0.6.6-alpha2 uses cooperative cancellation for the conversion worker. This note supersedes the V0.4.1 behavior for current builds.
