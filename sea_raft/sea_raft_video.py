import argparse
import json
import os
import struct
import sys
from argparse import Namespace
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F


def read_exact(stream, n):
    data = bytearray()
    while len(data) < n:
        chunk = stream.read(n - len(data))
        if not chunk:
            raise EOFError
        data.extend(chunk)
    return bytes(data)


def write_exact(stream, data):
    stream.write(data)
    stream.flush()


def load_args(path):
    with open(path, 'r', encoding='utf-8') as f:
        return Namespace(**json.load(f))


def rgb_tensor_from_rgba(buf, w, h, device):
    arr = np.frombuffer(buf, dtype=np.uint8).reshape(h, w, 4)[..., :3].copy()
    t = torch.from_numpy(arr).permute(2, 0, 1).unsqueeze(0).to(device=device, dtype=torch.float32, non_blocking=False)
    return t


def scene_score(a_rgba, b_rgba, w, h):
    if a_rgba is None:
        return 1.0
    # Cheap deterministic sampled RGB L1 score in [0,1].
    a = np.frombuffer(a_rgba, dtype=np.uint8).reshape(h, w, 4)
    b = np.frombuffer(b_rgba, dtype=np.uint8).reshape(h, w, 4)
    step = max(1, int((w * h / 65536.0) ** 0.5))
    aa = a[::step, ::step, :3].astype(np.int16)
    bb = b[::step, ::step, :3].astype(np.int16)
    return float(np.mean(np.abs(aa - bb)) / 255.0)


def uncertainty_u8(info, args, sensitivity=1.0):
    raw_b = info[:, 2:]
    weight = info[:, :2].softmax(dim=1)
    log_b = torch.zeros_like(raw_b)
    vmax = float(getattr(args, 'var_max', 10.0))
    vmin = float(getattr(args, 'var_min', 0.0))
    log_b[:, 0] = torch.clamp(raw_b[:, 0], min=0.0, max=vmax)
    log_b[:, 1] = torch.clamp(raw_b[:, 1], min=vmin, max=0.0)
    expected = (log_b * weight).sum(dim=1, keepdim=True)
    # SEA-RAFT's info tensor is a learned mixture-of-Laplace scale representation.
    # Convert it monotonically to [0,1] rather than pretending it is a calibrated probability.
    denom = max(1.0, vmax * 0.45)
    sensitivity = max(0.25, min(2.5, float(sensitivity)))
    uncertainty = 1.0 - torch.exp(-(torch.clamp(expected, min=0.0) / denom) * sensitivity)
    return torch.clamp(uncertainty * 255.0, 0.0, 255.0).to(torch.uint8)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', action='store_true')
    p.add_argument('--repo', required=True)
    p.add_argument('--cfg', required=True)
    p.add_argument('--model', default=None)
    p.add_argument('--url', default=None)
    p.add_argument('--device', default='cuda')
    p.add_argument('--scene-cut', type=float, default=0.28)
    p.add_argument('--scale', type=int, choices=[-2, -1, 0], default=None)
    p.add_argument('--iters', type=int, default=None)
    p.add_argument('--uncertainty-sensitivity', type=float, default=1.0)
    p.add_argument('--check-only', action='store_true', help='Load the selected SEA-RAFT model and exit without starting the frame pipe.')
    opt = p.parse_args()

    repo = Path(opt.repo).resolve()
    if not (repo / 'core' / 'raft.py').exists():
        raise RuntimeError(f'SEA-RAFT checkout is incomplete: {repo}')
    sys.path.insert(0, str(repo))
    sys.path.insert(0, str(repo / 'core'))
    from raft import RAFT
    from utils.utils import load_ckpt

    # The official model constructor seeds its backbone from torchvision ImageNet weights
    # before loading the SEA-RAFT checkpoint. For inference that network download is redundant
    # and breaks offline checkpoint use, so suppress it; the SEA-RAFT checkpoint supplies the
    # actual trained parameters immediately afterwards.
    import torchvision.models as tvm
    _resnet18 = tvm.resnet18
    _resnet34 = tvm.resnet34
    def _resnet18_no_download(*a, **kw):
        kw['weights'] = None
        return _resnet18(*a, **kw)
    def _resnet34_no_download(*a, **kw):
        kw['weights'] = None
        return _resnet34(*a, **kw)
    tvm.resnet18 = _resnet18_no_download
    tvm.resnet34 = _resnet34_no_download

    args = load_args(opt.cfg)
    if opt.scale is not None:
        args.scale = int(opt.scale)
    if opt.iters is not None:
        args.iters = max(1, min(12, int(opt.iters)))
    device = torch.device(opt.device)
    if device.type == 'cuda' and not torch.cuda.is_available():
        raise RuntimeError('CUDA is unavailable. SEA-RAFT branch requires an NVIDIA CUDA PyTorch runtime.')
    torch.backends.cudnn.benchmark = True

    if opt.model:
        model = RAFT(args)
        load_ckpt(model, opt.model)
    elif opt.url:
        local_hub = Path(opt.url)
        if local_hub.is_dir() and (local_hub / 'model.safetensors').exists():
            # Match the official SEA-RAFT Hugging Face loading path. PyTorchModelHubMixin
            # intentionally defaults to strict=False, as the published SEA-RAFT Hub
            # checkpoints can omit compatibility-only backbone normalization entries
            # that are present in newer upstream source revisions. The previous Crow
            # direct safetensors load used strict=True and therefore rejected an
            # otherwise official-compatible checkpoint with missing downsample BN keys.
            model = RAFT.from_pretrained(str(local_hub), args=args, local_files_only=True, strict=False)
        else:
            model = RAFT.from_pretrained(opt.url, args=args, strict=False)
    else:
        raise RuntimeError('Either --model or --url must be specified for SEA-RAFT.')
    model = model.to(device).eval()
    if opt.check_only:
        print(f'SEA-RAFT model load OK: cfg={Path(opt.cfg).name} source={opt.model or opt.url} device={device}', file=sys.stderr)
        return

    inp = sys.stdin.buffer
    out = sys.stdout.buffer
    write_exact(out, b'SRD1')

    previous_rgba = None
    previous_tensor = None
    while True:
        try:
            magic = read_exact(inp, 4)
        except EOFError:
            break
        if magic != b'FRM1':
            raise RuntimeError(f'Invalid SEA-RAFT frame magic: {magic!r}')
        w, h = struct.unpack('<II', read_exact(inp, 8))
        rgba = read_exact(inp, w * h * 4)
        score = scene_score(previous_rgba, rgba, w, h)
        cut = previous_rgba is None or score >= opt.scene_cut
        current = rgb_tensor_from_rgba(rgba, w, h, device)

        if cut:
            write_exact(out, b'SRF1' + struct.pack('<IIIIfI', w, h, 0, 0, score, 1))
            previous_rgba = rgba
            previous_tensor = current
            continue

        # Crow motion convention is current -> previous. SEA-RAFT estimates image1 -> image2,
        # therefore current is image1 and the stored previous frame is image2.
        scale_exp = int(getattr(args, 'scale', -1))
        scale = float(2 ** scale_exp)
        if scale != 1.0:
            cur_model = F.interpolate(current, scale_factor=scale, mode='bilinear', align_corners=False)
            prev_model = F.interpolate(previous_tensor, scale_factor=scale, mode='bilinear', align_corners=False)
        else:
            cur_model, prev_model = current, previous_tensor

        with torch.inference_mode():
            output = model(cur_model, prev_model, iters=int(getattr(args, 'iters', 4)), test_mode=True)
            flow = output['flow'][-1]
            info = output['info'][-1]
            # Flow units are model-input pixels. Convert the vector magnitude to source-image pixels,
            # but keep the lower spatial lattice for low-overhead IPC; C++ bilinearly expands it.
            flow = flow / scale
            u8 = uncertainty_u8(info, args, opt.uncertainty_sensitivity)

        grid_h, grid_w = int(flow.shape[-2]), int(flow.shape[-1])
        flow_np = flow[0].permute(1, 2, 0).contiguous().float().cpu().numpy()
        u8_np = u8[0, 0].contiguous().cpu().numpy()
        write_exact(out, b'SRF1' + struct.pack('<IIIIfI', w, h, grid_w, grid_h, score, 0))
        write_exact(out, flow_np.tobytes(order='C'))
        write_exact(out, u8_np.tobytes(order='C'))

        previous_rgba = rgba
        previous_tensor = current


if __name__ == '__main__':
    main()
