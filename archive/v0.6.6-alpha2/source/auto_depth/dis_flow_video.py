import argparse
import struct
import sys

import cv2
import numpy as np


# V0.6.5.3 stability goals:
# - keep DIS lightweight and dependency-compatible with the existing Auto Depth runtime;
# - prefer smooth/stable flow on ordinary video frames;
# - fall back to DIS spatial propagation only when it measurably improves a difficult pair;
# - collapse true/near duplicate frames to an exact zero-motion field rather than passing
#   sparse sub-pixel DIS noise into the temporal/DLSSNR path;
# - use forward/backward + photometric confidence to condition only isolated low-confidence
#   vectors. High-confidence motion boundaries are deliberately preserved.


def read_exact(stream, n):
    chunks = []
    left = n
    while left:
        b = stream.read(left)
        if not b:
            raise EOFError("unexpected EOF")
        chunks.append(b)
        left -= len(b)
    return b"".join(chunks)


def scene_score(previous, current):
    h0 = cv2.calcHist([previous], [0], None, [32], [0, 256]).reshape(-1)
    h1 = cv2.calcHist([current], [0], None, [32], [0, 256]).reshape(-1)
    n = float(previous.size)
    hist = 0.5 * float(np.abs(h0 - h1).sum() / max(n, 1.0))
    mad = float(np.mean(cv2.absdiff(previous, current))) / 255.0
    return float(np.clip(hist * 0.65 + mad * 0.35, 0.0, 1.0))


def make_dis(use_spatial_propagation):
    # PRESET_MEDIUM remains a good speed/quality basis. OpenCV documents that disabling
    # spatial propagation makes the field smoother, while enabling it may recover major
    # coarse-to-fine errors. V0.6.5.3 therefore uses the smooth variant first and invokes
    # the propagation variant only as a measured rescue candidate.
    dis = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM)
    dis.setUseMeanNormalization(True)
    dis.setUseSpatialPropagation(bool(use_spatial_propagation))
    return dis


def confidence_from_flow(previous, current, flow, reverse_flow):
    h, w = current.shape
    xx, yy = np.meshgrid(np.arange(w, dtype=np.float32), np.arange(h, dtype=np.float32))
    # flow is current -> previous, exactly the convention wanted by the video pipeline.
    map_x = xx + flow[..., 0]
    map_y = yy + flow[..., 1]
    warped_previous = cv2.remap(previous, map_x, map_y, cv2.INTER_LINEAR,
                                borderMode=cv2.BORDER_CONSTANT, borderValue=0)
    residual = cv2.absdiff(current, warped_previous).astype(np.float32)
    photometric = np.exp(-residual / 24.0).astype(np.float32)

    # Forward/backward consistency. A valid current->previous vector plus the
    # previous->current vector sampled at the reprojected point should sum to ~zero.
    back_x = cv2.remap(reverse_flow[..., 0], map_x, map_y, cv2.INTER_LINEAR,
                       borderMode=cv2.BORDER_CONSTANT, borderValue=0)
    back_y = cv2.remap(reverse_flow[..., 1], map_x, map_y, cv2.INTER_LINEAR,
                       borderMode=cv2.BORDER_CONSTANT, borderValue=0)
    fb_error = np.sqrt((flow[..., 0] + back_x) ** 2 + (flow[..., 1] + back_y) ** 2)
    consistency = np.exp(-fb_error / 1.75).astype(np.float32)

    valid = (map_x >= 0.0) & (map_y >= 0.0) & (map_x <= (w - 1)) & (map_y <= (h - 1))
    conf = photometric * consistency * valid.astype(np.float32)
    conf = cv2.GaussianBlur(conf, (3, 3), 0.0)
    conf *= valid.astype(np.float32)
    return np.clip(conf, 0.0, 1.0).astype(np.float32)


def flow_stats(previous, current, flow, conf):
    mag = np.sqrt(flow[..., 0] ** 2 + flow[..., 1] ** 2)
    diff = cv2.absdiff(previous, current)
    # Exact quantiles over the reduced analysis frame are cheap enough and materially more
    # robust than maxima for detecting collapse/noise frames.
    p50 = float(np.quantile(mag, 0.50))
    p90 = float(np.quantile(mag, 0.90))
    p95 = float(np.quantile(mag, 0.95))
    mean_conf = float(np.mean(conf))
    bad_ratio = float(np.mean(conf < 0.25))
    mad = float(np.mean(diff)) / 255.0
    changed_ratio = float(np.mean(diff > 8))
    return {
        "p50": p50,
        "p90": p90,
        "p95": p95,
        "mean_conf": mean_conf,
        "bad_ratio": bad_ratio,
        "mad": mad,
        "changed_ratio": changed_ratio,
    }


def candidate_score(stats):
    # Confidence is the primary objective. The bad-pixel penalty keeps a candidate with a
    # small but catastrophic failure region from winning on a marginally higher mean.
    return stats["mean_conf"] - 0.12 * stats["bad_ratio"]


def needs_spatial_rescue(stats):
    # Stable DIS (spatial propagation off) is intentionally the first candidate. Request
    # the classic spatial-propagation DIS only when the first solution is genuinely weak.
    # This catches large displacement / coarse-to-fine failures without paying 2x DIS cost
    # on normal frames.
    return (
        stats["mean_conf"] < 0.78
        or stats["bad_ratio"] > 0.18
        or (stats["p95"] > 24.0 and stats["mean_conf"] < 0.90)
    )


def is_near_duplicate(stats):
    # A true/near duplicate pair has very little image evidence for motion. DIS can still
    # return sparse sub-pixel vectors around noise/edges; in HSV preview this appears as a
    # black field with random coloured speckles and can be harmful to aggressive temporal
    # reconstruction. Be conservative: require both low image change and a low median flow.
    return (
        stats["mad"] < 0.010
        and stats["changed_ratio"] < 0.020
        and stats["p50"] < 0.12
        and stats["p95"] < 1.50
    )


def condition_isolated_outliers(flow, conf):
    # Component-wise 3x3 medians are used only as a local proposal. We do NOT blur the
    # entire field; only low-confidence vectors that strongly disagree with their local
    # neighbourhood are corrected. This preserves high-confidence motion boundaries.
    fx = flow[..., 0].astype(np.float32, copy=False)
    fy = flow[..., 1].astype(np.float32, copy=False)
    med_x = cv2.medianBlur(fx, 3)
    med_y = cv2.medianBlur(fy, 3)
    local_mag = np.sqrt(med_x * med_x + med_y * med_y)
    deviation = np.sqrt((fx - med_x) ** 2 + (fy - med_y) ** 2)
    threshold = 0.75 + 0.25 * local_mag
    low = conf < 0.35
    outlier = low & (deviation > threshold)
    if not np.any(outlier):
        return flow

    blend = np.clip((0.35 - conf) / 0.35, 0.0, 1.0) * outlier.astype(np.float32)
    fixed = flow.copy()
    fixed[..., 0] = fx * (1.0 - blend) + med_x * blend
    fixed[..., 1] = fy * (1.0 - blend) + med_y * blend
    return fixed.astype(np.float32, copy=False)


def zero_motion_confidence(previous, current):
    # For a duplicate/near-duplicate pair, zero motion is the correct temporal model.
    # Confidence still follows pixel similarity so compression noise does not receive a
    # perfect 1.0 blindly.
    residual = cv2.absdiff(previous, current).astype(np.float32)
    conf = np.exp(-residual / 18.0).astype(np.float32)
    conf = cv2.GaussianBlur(conf, (3, 3), 0.0)
    return np.clip(conf, 0.0, 1.0).astype(np.float32)


def server(args):
    # Primary = stability-first DIS. Rescue = OpenCV's default spatial propagation mode.
    dis_stable = make_dis(False)
    dis_stable_back = make_dis(False)
    dis_rescue = make_dis(True)
    dis_rescue_back = make_dis(True)

    previous = None
    frame_index = 0
    inp = sys.stdin.buffer
    out = sys.stdout.buffer
    out.write(b"RDY1")
    out.flush()

    while True:
        magic = inp.read(4)
        if not magic or magic == b"QUIT":
            return 0
        if magic != b"FRM1":
            raise RuntimeError(f"bad frame magic: {magic!r}")
        width, height = struct.unpack("<II", read_exact(inp, 8))
        raw = read_exact(inp, width * height)
        current = np.frombuffer(raw, dtype=np.uint8).reshape(height, width).copy()
        frame_index += 1

        score = 0.0
        cut = 1 if previous is None else 0
        flow = np.zeros((height, width, 2), dtype=np.float32)
        conf = np.zeros((height, width), dtype=np.float32)

        if previous is not None:
            score = scene_score(previous, current)
            cut = 1 if score >= args.scene_cut else 0
            if not cut:
                # OpenCV calc(I0, I1) returns I0 -> I1. Current first therefore yields
                # current -> previous motion, matching the internal DLSS temporal convention.
                flow = dis_stable.calc(current, previous, None).astype(np.float32, copy=False)
                reverse_flow = dis_stable_back.calc(previous, current, None).astype(np.float32, copy=False)
                conf = confidence_from_flow(previous, current, flow, reverse_flow)
                stats = flow_stats(previous, current, flow, conf)
                mode = "stable"

                # Large/ambiguous motion is where OpenCV documents spatial propagation as
                # useful for recovering coarse-to-fine failures. Compute it only on demand
                # and select it only when confidence metrics improve.
                if needs_spatial_rescue(stats):
                    rescue_flow = dis_rescue.calc(current, previous, None).astype(np.float32, copy=False)
                    rescue_reverse = dis_rescue_back.calc(previous, current, None).astype(np.float32, copy=False)
                    rescue_conf = confidence_from_flow(previous, current, rescue_flow, rescue_reverse)
                    rescue_stats = flow_stats(previous, current, rescue_flow, rescue_conf)
                    if candidate_score(rescue_stats) > candidate_score(stats) + 0.005:
                        flow, reverse_flow, conf, stats = rescue_flow, rescue_reverse, rescue_conf, rescue_stats
                        mode = "rescue"

                # Exact / near duplicate frames are a special case: sparse sub-pixel DIS
                # noise has no useful motion meaning. Force the mathematically correct zero
                # field instead of feeding coloured speckles to DLSSNR.
                if is_near_duplicate(stats):
                    flow = np.zeros_like(flow, dtype=np.float32)
                    conf = zero_motion_confidence(previous, current)
                    mode = "zero-near-duplicate"
                    print(
                        f"frame={frame_index} mode={mode} mad={stats['mad']:.5f} "
                        f"changed={stats['changed_ratio']:.4f} p50={stats['p50']:.3f} "
                        f"p95={stats['p95']:.3f} conf={stats['mean_conf']:.3f}",
                        file=sys.stderr,
                        flush=True,
                    )
                else:
                    # Repair only isolated, low-confidence vectors. Apply symmetrically to
                    # both directions and recompute confidence so downstream consumers see
                    # confidence for the exact final field.
                    reverse_conf = confidence_from_flow(current, previous, reverse_flow, flow)
                    conditioned = condition_isolated_outliers(flow, conf)
                    conditioned_reverse = condition_isolated_outliers(reverse_flow, reverse_conf)
                    if conditioned is not flow or conditioned_reverse is not reverse_flow:
                        flow = conditioned
                        reverse_flow = conditioned_reverse
                        conf = confidence_from_flow(previous, current, flow, reverse_flow)

                    if mode == "rescue" or stats["mean_conf"] < 0.55:
                        final_stats = flow_stats(previous, current, flow, conf)
                        print(
                            f"frame={frame_index} mode={mode} mad={final_stats['mad']:.5f} "
                            f"p50={final_stats['p50']:.3f} p95={final_stats['p95']:.3f} "
                            f"conf={final_stats['mean_conf']:.3f} bad={final_stats['bad_ratio']:.3f}",
                            file=sys.stderr,
                            flush=True,
                        )

        out.write(b"FLW1")
        out.write(struct.pack("<II f I", width, height, float(score), int(cut)))
        out.write(np.asarray(flow[..., 0], dtype="<f4").tobytes(order="C"))
        out.write(np.asarray(flow[..., 1], dtype="<f4").tobytes(order="C"))
        out.write(np.asarray(conf, dtype="<f4").tobytes(order="C"))
        out.flush()
        previous = current


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", action="store_true")
    ap.add_argument("--scene-cut", type=float, default=0.32)
    args = ap.parse_args()
    if not args.server:
        ap.error("dis_flow_video.py is a server helper; use --server")
    return server(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"DIS optical-flow helper error: {exc}", file=sys.stderr, flush=True)
        raise
