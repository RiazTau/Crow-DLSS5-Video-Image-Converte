"""Protocol smoke test for the V0.5 DIS helper. Run from the source root."""
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "auto_depth" / "dis_flow_video.py"


def read_exact(stream, n):
    out = bytearray()
    while len(out) < n:
        chunk = stream.read(n - len(out))
        if not chunk:
            raise RuntimeError("worker EOF")
        out += chunk
    return bytes(out)


def send_frame(proc, image):
    h, w = image.shape
    proc.stdin.write(b"FRM1" + struct.pack("<II", w, h) + image.tobytes())
    proc.stdin.flush()
    assert read_exact(proc.stdout, 4) == b"FLW1"
    width, height, score, cut = struct.unpack("<II f I", read_exact(proc.stdout, 16))
    n = width * height
    fx = np.frombuffer(read_exact(proc.stdout, n * 4), dtype="<f4").reshape(height, width)
    fy = np.frombuffer(read_exact(proc.stdout, n * 4), dtype="<f4").reshape(height, width)
    conf = np.frombuffer(read_exact(proc.stdout, n * 4), dtype="<f4").reshape(height, width)
    return score, cut, fx, fy, conf


def main():
    w, h = 320, 180
    previous = np.full((h, w), 20, np.uint8)
    current = previous.copy()
    previous[60:120, 80:140] = 220
    current[60:120, 84:144] = 220  # object moves +4 px in current frame

    proc = subprocess.Popen(
        [sys.executable, str(WORKER), "--server", "--scene-cut", "0.32"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    try:
        assert read_exact(proc.stdout, 4) == b"RDY1"
        _, cut0, *_ = send_frame(proc, previous)
        assert cut0 == 1
        score, cut, fx, fy, conf = send_frame(proc, current)
        assert cut == 0, score
        region = fx[70:110, 90:130]
        mean_x = float(region.mean())
        mean_y = float(fy[70:110, 90:130].mean())
        mean_conf = float(conf[70:110, 90:130].mean())
        # current -> previous should be roughly -4 px in X.
        assert -5.5 < mean_x < -2.5, mean_x
        assert abs(mean_y) < 1.0, mean_y
        assert mean_conf > 0.4, mean_conf
        print(f"PASS: score={score:.4f} flow=({mean_x:.3f},{mean_y:.3f}) confidence={mean_conf:.3f}")
    finally:
        if proc.stdin:
            proc.stdin.close()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        err = proc.stderr.read().decode("utf-8", "replace") if proc.stderr else ""
        if proc.returncode not in (0, None):
            print(err, file=sys.stderr)


if __name__ == "__main__":
    main()
