# Notices

This project is derived conceptually and in part from the GPLv3 Magpie experimental DLSSNR work. The project remains under GPLv3; see LICENSE.

External dependencies retain their own licenses:

- NVIDIA NGX / DLSS SDK and `nvngx_dlssnr.dll`: NVIDIA proprietary licensing applies. The DLSSNR runtime DLL is user supplied and is never downloaded by this project.
- TinyEXR v1: 3-clause BSD / upstream third-party notices. The build script obtains the upstream `release` branch rather than embedding it in this source archive.
- miniz: upstream terms as shipped by TinyEXR.
- Depth Anything V2 Small model: Apache-2.0 model repository; the program downloads the pinned FP16 ONNX weight on demand and verifies its SHA-256.
- ONNX Runtime DirectML: Microsoft MIT license. Installed into the isolated Auto Depth virtual environment on user request.
- NumPy and Pillow: installed into the isolated Auto Depth environment and remain subject to their respective upstream licenses.

V0.3 EXR source support still maps float EXR data into the previously validated SDR RGBA8 Feature-18 contract. Auto Depth is a separate image-guidance inference stage and does not change that color contract.
