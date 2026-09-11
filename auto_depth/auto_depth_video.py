import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image
import onnxruntime as ort

MEAN = np.asarray([0.485, 0.456, 0.406], dtype=np.float32)
STD = np.asarray([0.229, 0.224, 0.225], dtype=np.float32)


def read_exact(stream, n):
    chunks = []
    remaining = n
    while remaining:
        b = stream.read(remaining)
        if not b:
            raise EOFError("unexpected EOF")
        chunks.append(b)
        remaining -= len(b)
    return b"".join(chunks)


def round_multiple(v, m=14):
    return max(m, int(round(v / m) * m))


def normalize_depth(depth):
    d = np.asarray(depth, dtype=np.float32).squeeze()
    finite = np.isfinite(d)
    if not np.any(finite):
        return np.zeros_like(d, dtype=np.float32)
    lo = float(np.min(d[finite])); hi = float(np.max(d[finite]))
    if hi - lo < 1e-12:
        return np.zeros_like(d, dtype=np.float32)
    d = np.where(finite, d, lo)
    return np.clip((d - lo) / (hi - lo), 0.0, 1.0).astype(np.float32)


class Dav2:
    def __init__(self, model, size):
        self.size = size
        so = ort.SessionOptions()
        so.enable_mem_pattern = False
        so.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        available = ort.get_available_providers()
        providers = []
        if "DmlExecutionProvider" in available:
            providers.append("DmlExecutionProvider")
        providers.append("CPUExecutionProvider")
        self.session = ort.InferenceSession(str(Path(model)), sess_options=so, providers=providers)
        self.input = self.session.get_inputs()[0]

    def run(self, rgba, width, height):
        source = Image.fromarray(rgba.reshape(height, width, 4), mode="RGBA").convert("RGB")
        scale = max(self.size / float(width), self.size / float(height))
        nw = round_multiple(width * scale); nh = round_multiple(height * scale)
        resized = source.resize((nw, nh), Image.Resampling.BICUBIC)
        arr = np.asarray(resized, dtype=np.float32) / 255.0
        arr = (arr - MEAN) / STD
        arr = np.transpose(arr, (2, 0, 1))[None, ...]
        arr = arr.astype(np.float16 if "float16" in self.input.type else np.float32)
        result = self.session.run(None, {self.input.name: arr})[0]
        depth = normalize_depth(result)
        depth_img = Image.fromarray(depth, mode="F").resize((width, height), Image.Resampling.BICUBIC)
        return normalize_depth(np.asarray(depth_img, dtype=np.float32))


def server(args):
    model = Dav2(args.model, args.size)
    inp = sys.stdin.buffer; out = sys.stdout.buffer
    out.write(b"RDY1"); out.flush()
    while True:
        magic = inp.read(4)
        if not magic or magic == b"QUIT":
            return 0
        if magic != b"FRM1":
            raise RuntimeError(f"bad frame magic: {magic!r}")
        width, height = struct.unpack("<II", read_exact(inp, 8))
        raw = read_exact(inp, width * height * 4)
        rgba = np.frombuffer(raw, dtype=np.uint8)
        depth = model.run(rgba, width, height)
        out.write(b"DEP1")
        out.write(struct.pack("<II", width, height))
        out.write(np.asarray(depth, dtype="<f4").tobytes(order="C"))
        out.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--size", type=int, default=518)
    ap.add_argument("--server", action="store_true")
    args = ap.parse_args()
    if not args.server:
        ap.error("auto_depth_video.py is a server helper; use --server")
    return server(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"Auto Depth video helper error: {exc}", file=sys.stderr, flush=True)
        raise
