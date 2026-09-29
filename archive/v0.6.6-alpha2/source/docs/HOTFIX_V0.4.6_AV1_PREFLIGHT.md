# V0.4.6 AV1 decoder preflight hotfix

- Fixes the V0.4.5 assumption that Gyan Essentials contains libdav1d.
- AV1 inputs now run a one-frame RGBA preflight before D3D12/DLSSNR initialization.
- Candidate order: libdav1d -> av1_cuvid -> av1_qsv -> libaom-av1 -> native av1 -> FFmpeg automatic.
- A decoder is selected only if it actually decodes the first source frame to RGBA.
- On NVIDIA systems using Gyan Essentials, av1_cuvid can be selected automatically when supported by the GPU.
- If every decoder fails, conversion stops before DLSSNR with a compact AV1 compatibility/damage error.
