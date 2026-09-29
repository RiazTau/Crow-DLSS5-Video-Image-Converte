# V0.2 Change Log

Baseline: V0.1.1 B2 Direct DLSSNR Feature-18 path, reported runtime-working on the target Windows/RTX machine.

## Added

- Native Win32 GUI executable: `Crow-DLSS5-Video-Image-Converter-Image.exe`
- WIC manual source import: PNG/JPEG/BMP/TIFF
- TinyEXR source import and EXR layer selection
- EXR Exposure EV, Clamp/Reinhard/ACES Fitted tone-map selection, optional sRGB encoding
- Manual external depth import
- Exact arbitrary scanline single-part EXR depth-channel selection, including Blender-style `*.Depth.Z`
- Depth normalization, inverse-depth convention, scale/offset and automatic bilinear resize
- External depth upload as the actual DLSSNR `R32_FLOAT` depth resource
- Configurable `DLSSNR.DepthInverted`
- ReShade/RenoDX-style direct Feature-18 NR controls: Enable, Preset, Style, Intensity, Local Tone, Local Structure, Skin Structure, Auto Mask, UI Correction
- Iteration selector
- Persistent GUI runtime DLL import into `dist/runtime/`
- Before/after preview with draggable split divider and Original/DLSS5/Split modes
- PNG output and SDR-encoded half-float EXR output
- Expanded CLI equivalents
- TinyEXR automatic source dependency bootstrap in the build script

## Deliberate boundaries

- DLSSNR processing remains the validated same-resolution SDR RGBA8 contract.
- EXR input is display-mapped to SDR before Feature 18; native FP16/HDR Feature-18 processing is not claimed yet.
- EXR output stores the SDR result as float/half values; it is not restored scene-linear Cycles radiance.
- Motion remains zero for static images.
- Exact arbitrary depth-channel import currently targets scanline single-part EXR. Tiled/deep/multipart depth is deferred.
- RenoDX-only HDR/codec bridge settings are not exposed as fake no-op controls; only settings that map to the current direct NR path are active.
