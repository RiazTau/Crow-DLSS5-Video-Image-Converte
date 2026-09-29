import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image
import onnxruntime as ort

MEAN = np.asarray([0.485, 0.456, 0.406], dtype=np.float32)
STD = np.asarray([0.229, 0.224, 0.225], dtype=np.float32)


def round_multiple(v: float, m: int = 14) -> int:
    return max(m, int(round(v / m) * m))


def resize_for_model(img: Image.Image, target: int):
    w, h = img.size
    scale = max(target / float(w), target / float(h))
    nw = round_multiple(w * scale)
    nh = round_multiple(h * scale)
    return img.resize((nw, nh), Image.Resampling.BICUBIC)


def normalize_depth(depth: np.ndarray) -> np.ndarray:
    d = np.asarray(depth, dtype=np.float32)
    d = np.squeeze(d)
    finite = np.isfinite(d)
    if not np.any(finite):
        return np.zeros_like(d, dtype=np.float32)
    lo = float(np.min(d[finite]))
    hi = float(np.max(d[finite]))
    if hi - lo < 1e-12:
        return np.zeros_like(d, dtype=np.float32)
    d = np.where(finite, d, lo)
    return np.clip((d - lo) / (hi - lo), 0.0, 1.0).astype(np.float32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--model', required=True)
    ap.add_argument('--input', required=True)
    ap.add_argument('--output', required=True)
    ap.add_argument('--size', type=int, default=518)
    args = ap.parse_args()

    source = Image.open(args.input).convert('RGB')
    ow, oh = source.size
    resized = resize_for_model(source, args.size)
    arr = np.asarray(resized, dtype=np.float32) / 255.0
    arr = (arr - MEAN) / STD
    arr = np.transpose(arr, (2, 0, 1))[None, ...]

    so = ort.SessionOptions()
    so.enable_mem_pattern = False
    so.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    available = ort.get_available_providers()
    providers = []
    if 'DmlExecutionProvider' in available:
        providers.append('DmlExecutionProvider')
    providers.append('CPUExecutionProvider')
    session = ort.InferenceSession(str(Path(args.model)), sess_options=so, providers=providers)
    inp = session.get_inputs()[0]
    if 'float16' in inp.type:
        arr = arr.astype(np.float16)
    else:
        arr = arr.astype(np.float32)
    result = session.run(None, {inp.name: arr})[0]
    depth = normalize_depth(result)

    depth_img = Image.fromarray(depth, mode='F').resize((ow, oh), Image.Resampling.BICUBIC)
    depth = normalize_depth(np.asarray(depth_img, dtype=np.float32))

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open('wb') as f:
        f.write(b'DAV2')
        f.write(struct.pack('<II', ow, oh))
        f.write(np.asarray(depth, dtype='<f4').tobytes(order='C'))
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f'Auto Depth error: {exc}', file=sys.stderr)
        raise
