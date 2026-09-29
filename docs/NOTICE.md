# Third-party notices and dependency boundaries

Crow source is distributed under GPLv3; see the repository `LICENSE`. The project
is derived conceptually and in part from the GPLv3 Magpie experimental DLSSNR
work. Third-party projects, SDKs, runtimes, models and downloaded binaries remain
under their own licenses and are not relicensed by Crow.

This file is a practical dependency summary, not a replacement for the license,
notice or model-card files shipped by each upstream project. Upstream terms and
the terms accompanying the exact downloaded version always control.

## NVIDIA components

- **NVIDIA NGX / DLSS SDK and DLSS runtimes** — NVIDIA license terms apply.
  `nvngx_dlssnr.dll` is supplied by the user and is not included in this source
  package. DLSS-G runtime files and the downloaded DLSS SDK likewise remain
  external to the tracked source.
- **[NVIDIA Optical Flow SDK](https://developer.nvidia.com/opticalflow/download)** — the SDK download is governed by NVIDIA's Software
  Developer Kits, Samples and Tools License Agreement presented by NVIDIA at
  download time. SDK packages, headers and samples are not redistributed in this
  source archive. The runtime implementation is supplied by the installed NVIDIA
  display driver.
- **[NVIDIA NVAPI SDK](https://github.com/NVIDIA/nvapi)** — the public `NVIDIA/nvapi` repository components used by
  the build are provided under the MIT License. The repository is acquired at
  build/setup time and is not vendored here; NVIDIA driver components retain
  their applicable NVIDIA terms.

## Neural motion, Python and model dependencies

- **[SEA-RAFT](https://github.com/princeton-vl/SEA-RAFT)** — the official `princeton-vl/SEA-RAFT` source is BSD-3-Clause.
  Crow contains an integration adapter and setup worker, but downloads the
  upstream checkout at setup time instead of vendoring it.
- **SEA-RAFT Spring-S and Spring-M checkpoints** — the currently configured
  Hugging Face repositories
  [`MemorySlices/Tartan-C-T-TSKH-spring540x960-S`](https://huggingface.co/MemorySlices/Tartan-C-T-TSKH-spring540x960-S) and
  [`MemorySlices/Tartan-C-T-TSKH-spring540x960-M`](https://huggingface.co/MemorySlices/Tartan-C-T-TSKH-spring540x960-M) are marked BSD-3-Clause in their
  model cards. Users and redistributors must re-check the model card for the
  exact downloaded revision.
- **[PyTorch](https://github.com/pytorch/pytorch/blob/main/LICENSE) and [torchvision](https://github.com/pytorch/vision/blob/main/LICENSE)** — installed into the isolated SEA-RAFT environment.
  Their main projects use BSD-style/BSD-3-Clause terms and their binary packages
  can contain separately licensed third-party components. Preserve the license
  and notice files supplied with the exact wheels when redistributing them.
- **[Hugging Face Hub client](https://github.com/huggingface/huggingface_hub)** — Apache-2.0. Hugging Face is a distribution
  service; every downloaded model remains governed by the license declared in
  that model repository's model card, not by the client library's license.
- **Depth Anything V2 Small ONNX model** — the pinned
  [`onnx-community/depth-anything-v2-small`](https://huggingface.co/onnx-community/depth-anything-v2-small) model repository is marked
  Apache-2.0. Crow downloads the FP16 ONNX weight on demand and verifies its
  SHA-256. Other Depth Anything V2 model sizes may use different terms and are
  not covered by this statement.
- **ONNX Runtime DirectML** — MIT License. It is installed into the isolated Auto
  Depth environment on user request.
- **[OpenCV / opencv-python-headless 4.10+](https://github.com/opencv/opencv/blob/4.x/LICENSE)** — Apache-2.0. Crow uses it for Auto
  Depth support and DIS optical-flow processing.
- **NumPy and Pillow** — installed into isolated Python environments and remain
  subject to their respective upstream licenses and bundled notices.

## Media and source-code dependencies

- **[FFmpeg / ffprobe](https://ffmpeg.org/legal.html)** — FFmpeg is primarily LGPL-2.1-or-later. A particular
  binary becomes GPL-covered when built with GPL components, and builds using
  nonfree components can have additional redistribution restrictions. Crow does
  not vendor FFmpeg in this source archive. Before distributing a Portable build,
  inspect the exact FFmpeg binary/configuration and include its corresponding
  license, source offer and notices.
- **TinyEXR v1** — BSD-3-Clause plus its upstream third-party notices. The build
  obtains the upstream `release` branch rather than embedding it here.
- **miniz** — upstream terms as shipped by TinyEXR.

V0.3 EXR source support maps float EXR data into the previously validated SDR
RGBA8 Feature-18 contract. Auto Depth is a separate image-guidance inference
stage and does not change that color contract.
