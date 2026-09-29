# V0.3 Change Log

## UI
- Replaced draggable split A/B comparison with simultaneous complete Original and DLSS5 panes.
- Added dedicated Depth Guidance Preview pane.
- Removed Compare mode selector.
- Added Depth Mode selector (Zero / Auto DAV2 / Manual).
- Added Setup Auto Depth and Generate / Refresh Auto Depth controls.
- Restored descriptive NR labels: Default/Natural/Cinematic Style and named Preset entries.

## Auto Depth
- Added `AutoDepthRunner` bridge.
- Added Depth Anything V2 FP16 ONNX inference helper.
- Added isolated ONNX Runtime DirectML Python environment bootstrap.
- Auto model download remains hash-verified through `ModelBootstrap`.
- Auto depth is normalized relative inverse depth and is passed directly to Feature 18.
- Active depth guidance is converted to grayscale for exact preview of the supplied depth field.

## Existing V0.2 functionality retained
- PNG/JPEG/TIFF/EXR source import.
- Blender Multilayer EXR layer selection.
- EXR exposure/tone-map/sRGB controls.
- Manual external depth map and exact Blender `*.Depth.Z` EXR channel import.
- Direct DLSSNR Feature-18 runtime path and NR parameter controls.
- CLI diagnostics/output workflow.
