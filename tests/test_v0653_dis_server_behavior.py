import struct
import subprocess
import sys
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / 'auto_depth' / 'dis_flow_video.py'


def read_exact(stream, n):
    out = bytearray()
    while len(out) < n:
        b = stream.read(n - len(out))
        if not b:
            raise RuntimeError('worker EOF')
        out += b
    return bytes(out)


def send(proc, image):
    h, w = image.shape
    proc.stdin.write(b'FRM1' + struct.pack('<II', w, h) + image.tobytes())
    proc.stdin.flush()
    assert read_exact(proc.stdout, 4) == b'FLW1'
    rw, rh, score, cut = struct.unpack('<II f I', read_exact(proc.stdout, 16))
    n = rw * rh
    x = np.frombuffer(read_exact(proc.stdout, n * 4), '<f4').reshape(rh, rw)
    y = np.frombuffer(read_exact(proc.stdout, n * 4), '<f4').reshape(rh, rw)
    c = np.frombuffer(read_exact(proc.stdout, n * 4), '<f4').reshape(rh, rw)
    return score, cut, x, y, c


def shift(im, dx):
    h, w = im.shape
    m = np.float32([[1, 0, dx], [0, 1, 0]])
    return cv2.warpAffine(im, m, (w, h), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT)


def main():
    rng = np.random.default_rng(111)
    h, w = 180, 320
    base = np.clip(rng.normal(125, 35, (h, w)), 0, 255).astype(np.uint8)
    base = cv2.GaussianBlur(base, (0, 0), 1.0)
    cv2.rectangle(base, (55, 45), (135, 140), 220, -1)

    a = np.clip(base.astype(np.float32) + rng.normal(0, 1.0, base.shape), 0, 255).astype(np.uint8)
    b = np.clip(base.astype(np.float32) + rng.normal(0, 1.0, base.shape), 0, 255).astype(np.uint8)
    cimg = shift(b, 4.0)

    proc = subprocess.Popen([sys.executable, str(WORKER), '--server', '--scene-cut', '0.32'],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        assert read_exact(proc.stdout, 4) == b'RDY1'
        _, cut0, *_ = send(proc, a)
        assert cut0 == 1
        _, cut1, fx1, fy1, conf1 = send(proc, b)
        assert cut1 == 0
        assert float(np.max(np.abs(fx1))) == 0.0
        assert float(np.max(np.abs(fy1))) == 0.0
        assert float(conf1.mean()) > 0.80

        _, cut2, fx2, fy2, conf2 = send(proc, cimg)
        assert cut2 == 0
        mean_x = float(np.mean(fx2[30:-30, 40:-40]))
        assert -4.8 < mean_x < -3.2, mean_x
        assert float(conf2.mean()) > 0.60
        print(f'PASS: near-duplicate zero field + resumed motion x={mean_x:.3f} conf={conf2.mean():.3f}')
    finally:
        if proc.stdin:
            proc.stdin.close()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == '__main__':
    main()
