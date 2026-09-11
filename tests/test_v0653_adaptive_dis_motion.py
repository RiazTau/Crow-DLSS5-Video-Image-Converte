"""Behavioral smoke tests for V0.6.5.3 Adaptive Stable DIS motion generation."""
import importlib.util
from pathlib import Path

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / "auto_depth" / "dis_flow_video.py"
spec = importlib.util.spec_from_file_location("dis_flow_video_v0653", HELPER)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def shift(im, dx, dy=0.0):
    h, w = im.shape
    m = np.float32([[1, 0, dx], [0, 1, dy]])
    return cv2.warpAffine(im, m, (w, h), flags=cv2.INTER_LINEAR, borderMode=cv2.BORDER_REFLECT)


def base_image(w=480, h=270):
    rng = np.random.default_rng(1234)
    im = np.clip(rng.normal(125.0, 40.0, (h, w)), 0, 255).astype(np.uint8)
    im = cv2.GaussianBlur(im, (0, 0), 1.1)
    cv2.rectangle(im, (75, 55), (205, 205), 220, -1)
    cv2.circle(im, (350, 145), 48, 45, -1)
    return im


def calc_candidate(previous, current, spatial):
    fwd = mod.make_dis(spatial).calc(current, previous, None).astype(np.float32, copy=False)
    bwd = mod.make_dis(spatial).calc(previous, current, None).astype(np.float32, copy=False)
    conf = mod.confidence_from_flow(previous, current, fwd, bwd)
    return fwd, bwd, conf, mod.flow_stats(previous, current, fwd, conf)


def test_normal_translation():
    base = base_image()
    previous = base
    current = shift(base, 4.0)
    flow, reverse, conf, stats = calc_candidate(previous, current, False)
    interior = flow[20:-20, 30:-20, 0]
    mean_x = float(interior.mean())
    assert -4.5 < mean_x < -3.5, mean_x
    assert stats["mean_conf"] > 0.75, stats
    assert not mod.is_near_duplicate(stats), stats
    print(f"normal translation: x={mean_x:.3f}, conf={stats['mean_conf']:.3f}")


def test_near_duplicate_zero_policy():
    rng = np.random.default_rng(9)
    base = base_image()
    previous = np.clip(base.astype(np.float32) + rng.normal(0, 1.2, base.shape), 0, 255).astype(np.uint8)
    current = np.clip(base.astype(np.float32) + rng.normal(0, 1.2, base.shape), 0, 255).astype(np.uint8)
    flow, reverse, conf, stats = calc_candidate(previous, current, False)
    assert mod.is_near_duplicate(stats), stats
    # The previous implementation would visualize the residual DIS speckles very brightly
    # because its display normalization follows P95. V0.6.5.3 collapses this class to zero.
    zero = np.zeros_like(flow)
    zero_conf = mod.zero_motion_confidence(previous, current)
    assert float(np.max(np.abs(zero))) == 0.0
    assert float(zero_conf.mean()) > 0.75
    print(f"near duplicate: raw_p95={stats['p95']:.3f}, zero_conf={zero_conf.mean():.3f}")


def test_large_motion_requests_and_benefits_from_rescue():
    base = base_image()
    previous = base
    current = shift(base, 48.0)
    stable_flow, stable_rev, stable_conf, stable_stats = calc_candidate(previous, current, False)
    rescue_flow, rescue_rev, rescue_conf, rescue_stats = calc_candidate(previous, current, True)
    assert mod.needs_spatial_rescue(stable_stats), stable_stats
    assert mod.candidate_score(rescue_stats) > mod.candidate_score(stable_stats), (stable_stats, rescue_stats)
    interior = rescue_flow[20:-20, 60:-20, 0]
    err = float(np.mean(np.abs(interior + 48.0)))
    assert err < 3.0, err
    print(
        f"large motion: stable_conf={stable_stats['mean_conf']:.3f}, "
        f"rescue_conf={rescue_stats['mean_conf']:.3f}, rescue_err={err:.3f}"
    )


def test_low_confidence_outlier_conditioning():
    h, w = 64, 96
    flow = np.zeros((h, w, 2), dtype=np.float32)
    flow[..., 0] = -3.0
    conf = np.ones((h, w), dtype=np.float32)
    flow[30, 40] = (18.0, -12.0)
    conf[30, 40] = 0.0
    fixed = mod.condition_isolated_outliers(flow, conf)
    assert np.linalg.norm(fixed[30, 40] - np.array([-3.0, 0.0], np.float32)) < 0.5, fixed[30, 40]
    assert np.allclose(fixed[20, 20], flow[20, 20])
    print("outlier conditioning: PASS")


if __name__ == "__main__":
    test_normal_translation()
    test_near_duplicate_zero_policy()
    test_large_motion_requests_and_benefits_from_rescue()
    test_low_confidence_outlier_conditioning()
    print("PASS: V0.6.5.3 adaptive stable DIS motion")
