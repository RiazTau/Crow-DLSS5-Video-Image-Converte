# V0.4.1 Hang-Safety Hotfix

- Video metadata probing moved off the Win32 UI thread.
- ffprobe capture now uses non-blocking pipe polling and a hard timeout instead of ReadFile + INFINITE wait.
- Decoder pipe uses stall detection and checks cancellation while waiting for frame bytes.
- Conversion Cancel calls CancelSynchronousIo on the worker so blocked synchronous pipe reads/writes can be interrupted.
- Auto Depth video helper startup has a 120 s ready timeout; frame replies have a 30 s stall timeout and detect helper exit.
- FFmpeg decoder/encoder finalization waits are bounded (15 s / 60 s) instead of infinite.
- Cancellation-triggered I/O aborts are reported as Cancelled rather than Failed.
- Closing the GUI also cancels probe/conversion I/O before joining worker threads.

The DLSSNR Feature 18 processing path, parameter mapping, video output codecs and realtime preview behavior are otherwise unchanged from V0.4.0.
