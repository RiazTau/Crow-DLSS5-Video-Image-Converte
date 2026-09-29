#include "TemporalMotionConsensus.h"
#include "ParallelRows.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>

namespace video {
namespace {

struct ConsensusConfig {
    float historyConfidence = 0.64f;
    float depthGate = 0.085f;
    float residualBase = 1.35f;
    float residualScale = 0.10f;
    float spreadScale = 1.20f;
    float maxHistorySpread = 2.75f;
    float maxBlend = 0.62f;
    float highConfidenceProtection = 0.82f;
    float highConfidenceBlendScale = 0.38f;
    float phaseSlipMultiplier = 1.90f;
    float minimumVisibility = 0.42f;
    float maximumUncertainty = 0.72f;
    uint32_t minimumHistorySamples = 1;
};

ConsensusConfig ConfigFor(NvofReliabilityMode mode) {
    ConsensusConfig c;
    if (mode == NvofReliabilityMode::Strong) {
        c.historyConfidence = 0.58f;
        c.depthGate = 0.055f;
        c.residualBase = 0.85f;
        c.residualScale = 0.075f;
        c.spreadScale = 1.00f;
        c.maxHistorySpread = 2.10f;
        c.maxBlend = 0.90f;
        c.highConfidenceProtection = 0.90f;
        c.highConfidenceBlendScale = 0.62f;
        c.phaseSlipMultiplier = 1.55f;
        c.minimumVisibility = 0.34f;
        c.maximumUncertainty = 0.80f;
        c.minimumHistorySamples = 2;
    }
    return c;
}

float MedianSmall(std::array<float, 4> values, size_t count) {
    if (!count) return 0.0f;
    std::sort(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(count));
    if (count & 1u) return values[count / 2u];
    return 0.5f * (values[count / 2u - 1u] + values[count / 2u]);
}

float BilinearScalar(const std::vector<float>& v, uint32_t w, uint32_t h, float gx, float gy, bool& valid) {
    valid = false;
    if (!w || !h || v.size() != static_cast<size_t>(w) * h ||
        gx < 0.0f || gy < 0.0f || gx > static_cast<float>(w - 1u) || gy > static_cast<float>(h - 1u)) return 0.0f;
    const uint32_t x0 = static_cast<uint32_t>(std::floor(gx));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(gy));
    const uint32_t x1 = std::min(w - 1u, x0 + 1u);
    const uint32_t y1 = std::min(h - 1u, y0 + 1u);
    const float tx = gx - static_cast<float>(x0);
    const float ty = gy - static_cast<float>(y0);
    const float a = v[static_cast<size_t>(y0) * w + x0];
    const float b = v[static_cast<size_t>(y0) * w + x1];
    const float c = v[static_cast<size_t>(y1) * w + x0];
    const float d = v[static_cast<size_t>(y1) * w + x1];
    valid = true;
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

bool BilinearVector(const std::vector<float>& xy, uint32_t w, uint32_t h, float gx, float gy, float& x, float& y) {
    if (!w || !h || xy.size() != static_cast<size_t>(w) * h * 2u ||
        gx < 0.0f || gy < 0.0f || gx > static_cast<float>(w - 1u) || gy > static_cast<float>(h - 1u)) return false;
    const uint32_t x0 = static_cast<uint32_t>(std::floor(gx));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(gy));
    const uint32_t x1 = std::min(w - 1u, x0 + 1u);
    const uint32_t y1 = std::min(h - 1u, y0 + 1u);
    const float tx = gx - static_cast<float>(x0);
    const float ty = gy - static_cast<float>(y0);
    auto sample = [&](uint32_t sx, uint32_t sy, uint32_t c) {
        return xy[(static_cast<size_t>(sy) * w + sx) * 2u + c];
    };
    const float ax = sample(x0, y0, 0), ay = sample(x0, y0, 1);
    const float bx = sample(x1, y0, 0), by = sample(x1, y0, 1);
    const float cx = sample(x0, y1, 0), cy = sample(x0, y1, 1);
    const float dx = sample(x1, y1, 0), dy = sample(x1, y1, 1);
    const float topX = ax + (bx - ax) * tx;
    const float topY = ay + (by - ay) * tx;
    const float bottomX = cx + (dx - cx) * tx;
    const float bottomY = cy + (dy - cy) * tx;
    x = topX + (bottomX - topX) * ty;
    y = topY + (bottomY - topY) * ty;
    return std::isfinite(x) && std::isfinite(y);
}

float GridCoord(float pixel, uint32_t step) {
    // History samples live at ((gridIndex + 0.5) * step - 0.5) in source-pixel space.
    return (pixel + 0.5f) / static_cast<float>(step) - 0.5f;
}

float FullScalarNearest(const std::vector<float>& v, uint32_t w, uint32_t h, float px, float py, float fallback) {
    if (v.size() != static_cast<size_t>(w) * h || !w || !h) return fallback;
    const uint32_t x = std::min(w - 1u, static_cast<uint32_t>(std::max(0.0f, std::round(px))));
    const uint32_t y = std::min(h - 1u, static_cast<uint32_t>(std::max(0.0f, std::round(py))));
    return v[static_cast<size_t>(y) * w + x];
}

void FullVectorNearest(const std::vector<float>& xy, uint32_t w, uint32_t h, float px, float py, float& x, float& y) {
    if (xy.size() != static_cast<size_t>(w) * h * 2u || !w || !h) { x = y = 0.0f; return; }
    const uint32_t ix = std::min(w - 1u, static_cast<uint32_t>(std::max(0.0f, std::round(px))));
    const uint32_t iy = std::min(h - 1u, static_cast<uint32_t>(std::max(0.0f, std::round(py))));
    const size_t i = static_cast<size_t>(iy) * w + ix;
    x = xy[i * 2u + 0u];
    y = xy[i * 2u + 1u];
}

bool DepthCompatible(float a, float b, float gate) {
    if (!std::isfinite(a) || !std::isfinite(b)) return false;
    const float tolerance = std::max(gate, 0.06f * std::max(std::abs(a), std::abs(b)));
    return std::abs(a - b) <= tolerance;
}

} // namespace

TemporalMotionConsensus::TemporalMotionConsensus(uint32_t width,
                                                 uint32_t height,
                                                 NvofReliabilityMode mode,
                                                 uint32_t nvofGridSize)
    : _width(width), _height(height), _mode(mode) {
    // The history is intentionally compact. Auto samples at NVOF-grid scale; Strong uses
    // a finer 2-pixel lattice so the extra 5-frame context does not cost hundreds of MB at 4K.
    const uint32_t grid = std::clamp(nvofGridSize, 1u, 4u);
    _step = mode == NvofReliabilityMode::Strong ? std::max(2u, grid / 2u) : std::max(2u, grid);
    // Keep 8K-class history bounded. At >12 MP Strong falls back to a 4-pixel
    // history lattice; the final detailed field still comes from full-resolution NVOF.
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (pixels > 12000000ull) _step = std::max(_step, 4u);
    _maxHistory = mode == NvofReliabilityMode::Strong ? 3u : (mode == NvofReliabilityMode::Auto ? 1u : 0u);
}

void TemporalMotionConsensus::Reset() {
    _history.clear();
}

TemporalMotionConsensus::HistoryGrid TemporalMotionConsensus::MakeHistoryGrid(
    const TemporalFlowResult& flow,
    const std::vector<float>* depth) const {
    HistoryGrid g;
    g.step = _step;
    g.width = (_width + _step - 1u) / _step;
    g.height = (_height + _step - 1u) / _step;
    const size_t n = static_cast<size_t>(g.width) * g.height;
    g.motionXY.resize(n * 2u, 0.0f);
    g.confidence.resize(n, 0.0f);
    g.visibility.resize(n, 1.0f);
    g.uncertainty.resize(n, 1.0f);
    if (depth && depth->size() == static_cast<size_t>(_width) * _height) g.depth.resize(n, 0.0f);

    perf::ParallelForRows(g.height, 32u, [&](uint32_t y0, uint32_t y1, unsigned) {
        for (uint32_t gy = y0; gy < y1; ++gy) {
            const float py = std::min(static_cast<float>(_height - 1u), (static_cast<float>(gy) + 0.5f) * _step - 0.5f);
            for (uint32_t gx = 0; gx < g.width; ++gx) {
                const float px = std::min(static_cast<float>(_width - 1u), (static_cast<float>(gx) + 0.5f) * _step - 0.5f);
                const size_t gi = static_cast<size_t>(gy) * g.width + gx;
                float vx = 0.0f, vy = 0.0f;
                FullVectorNearest(flow.motionXY, _width, _height, px, py, vx, vy);
                g.motionXY[gi * 2u + 0u] = vx;
                g.motionXY[gi * 2u + 1u] = vy;
                g.confidence[gi] = FullScalarNearest(flow.confidence, _width, _height, px, py, 0.0f);
                g.visibility[gi] = FullScalarNearest(flow.historyVisibility, _width, _height, px, py, 1.0f);
                g.uncertainty[gi] = FullScalarNearest(flow.motionUncertainty, _width, _height, px, py,
                                                      1.0f - g.confidence[gi]);
                if (!g.depth.empty()) g.depth[gi] = FullScalarNearest(*depth, _width, _height, px, py, std::numeric_limits<float>::quiet_NaN());
            }
        }
    });
    return g;
}

void TemporalMotionConsensus::PushHistory(HistoryGrid grid) {
    if (!_maxHistory) return;
    _history.push_front(std::move(grid));
    while (_history.size() > _maxHistory) _history.pop_back();
}

TemporalFlowResult TemporalMotionConsensus::Stabilize(const TemporalFlowResult& current,
                                                       const std::vector<float>* depth,
                                                       bool reset) {
    TemporalFlowResult out = current;
    out.temporalConsensusCorrectedFraction = 0.0f;
    out.temporalConsensusMeanResidual = 0.0f;
    out.temporalConsensusHistory = static_cast<uint32_t>(_history.size());

    const size_t fullN = static_cast<size_t>(_width) * _height;
    const bool valid = _width && _height && current.motionXY.size() == fullN * 2u && current.confidence.size() == fullN;
    if (_mode == NvofReliabilityMode::Off || !valid) return out;
    if (reset || current.sceneCut) {
        Reset();
        return out;
    }

    const bool haveDepth = depth && depth->size() == fullN;
    if (_history.empty()) {
        PushHistory(MakeHistoryGrid(out, haveDepth ? depth : nullptr));
        return out;
    }

    const ConsensusConfig cfg = ConfigFor(_mode);
    const uint32_t gridW = (_width + _step - 1u) / _step;
    const uint32_t gridH = (_height + _step - 1u) / _step;
    const size_t gridN = static_cast<size_t>(gridW) * gridH;
    std::vector<float> correctionXY(gridN * 2u, 0.0f);
    std::vector<float> correctionWeight(gridN, 0.0f);
    std::atomic<uint64_t> correctedCells{0};
    std::atomic<uint64_t> residualMilliSum{0};

    perf::ParallelForRows(gridH, 16u, [&](uint32_t gy0, uint32_t gy1, unsigned) {
        for (uint32_t gy = gy0; gy < gy1; ++gy) {
            const float py0 = std::min(static_cast<float>(_height - 1u), (static_cast<float>(gy) + 0.5f) * _step - 0.5f);
            for (uint32_t gx = 0; gx < gridW; ++gx) {
                const float px0 = std::min(static_cast<float>(_width - 1u), (static_cast<float>(gx) + 0.5f) * _step - 0.5f);
                const size_t gi = static_cast<size_t>(gy) * gridW + gx;

                float curX = 0.0f, curY = 0.0f;
                FullVectorNearest(current.motionXY, _width, _height, px0, py0, curX, curY);
                const float curConf = FullScalarNearest(current.confidence, _width, _height, px0, py0, 0.0f);
                const float curVisibility = FullScalarNearest(current.historyVisibility, _width, _height, px0, py0, 1.0f);
                const float curUncertainty = FullScalarNearest(current.motionUncertainty, _width, _height, px0, py0, 1.0f-curConf);
                const float curDisocclusion = FullScalarNearest(current.disocclusionProbability, _width, _height, px0, py0, 0.0f);
                if (!std::isfinite(curX) || !std::isfinite(curY) || std::hypot(curX, curY) > 2048.0f) continue;
                // Temporal trajectory evidence is invalid for newly revealed surfaces. Their FG
                // candidate may still come from spatial/depth repair, but alpha6 never drags them
                // through stale temporal history.
                if (curVisibility < 0.18f || curDisocclusion > 0.72f) continue;

                float traceX = px0 + curX;
                float traceY = py0 + curY;
                float referenceDepth = haveDepth ? FullScalarNearest(*depth, _width, _height, px0, py0,
                                                                      std::numeric_limits<float>::quiet_NaN())
                                                     : std::numeric_limits<float>::quiet_NaN();

                std::array<float, 4> hx{};
                std::array<float, 4> hy{};
                std::array<float, 4> hc{};
                std::array<float, 4> hv{};
                std::array<float, 4> hu{};
                size_t count = 0;
                for (const auto& h : _history) {
                    if (count == hx.size()) break;
                    const float hgx = GridCoord(traceX, h.step);
                    const float hgy = GridCoord(traceY, h.step);
                    float vx = 0.0f, vy = 0.0f;
                    if (!BilinearVector(h.motionXY, h.width, h.height, hgx, hgy, vx, vy)) break;
                    bool confValid = false;
                    const float conf = BilinearScalar(h.confidence, h.width, h.height, hgx, hgy, confValid);
                    bool visibilityValid = false, uncertaintyValid = false;
                    const float visibility = BilinearScalar(h.visibility, h.width, h.height, hgx, hgy, visibilityValid);
                    const float uncertainty = BilinearScalar(h.uncertainty, h.width, h.height, hgx, hgy, uncertaintyValid);
                    if (!confValid || !visibilityValid || !uncertaintyValid || conf < cfg.historyConfidence ||
                        visibility < cfg.minimumVisibility || uncertainty > cfg.maximumUncertainty) break;

                    if (haveDepth && !h.depth.empty() && std::isfinite(referenceDepth)) {
                        bool depthValid = false;
                        const float hd = BilinearScalar(h.depth, h.width, h.height, hgx, hgy, depthValid);
                        if (!depthValid || !DepthCompatible(referenceDepth, hd, cfg.depthGate)) break;
                        referenceDepth = hd;
                    }

                    hx[count] = vx;
                    hy[count] = vy;
                    hc[count] = conf;
                    hv[count] = visibility;
                    hu[count] = uncertainty;
                    ++count;
                    traceX += vx;
                    traceY += vy;
                    if (traceX < 0.0f || traceY < 0.0f || traceX > static_cast<float>(_width - 1u) || traceY > static_cast<float>(_height - 1u)) break;
                }
                if (count < cfg.minimumHistorySamples) continue;

                float expectedX = MedianSmall(hx, count);
                float expectedY = MedianSmall(hy, count);
                float historyConfidence = 0.0f;
                float historyReliability = 0.0f;
                float historyUncertainty = 0.0f;
                for (size_t i = 0; i < count; ++i) {
                    historyConfidence += hc[i];
                    historyUncertainty += hu[i];
                    historyReliability += hc[i] * hv[i] * (1.0f - 0.70f * hu[i]);
                }
                historyConfidence /= static_cast<float>(count);
                historyUncertainty /= static_cast<float>(count);
                historyReliability /= static_cast<float>(count);

                std::array<float, 4> distances{};
                for (size_t i = 0; i < count; ++i) distances[i] = std::hypot(hx[i] - expectedX, hy[i] - expectedY);
                float spread = MedianSmall(distances, count);

                // Alpha6 turns trailing consensus into a lightweight predictive trajectory
                // model. The newest historical segment anchors the prediction; acceleration is
                // extrapolated only when successive acceleration estimates agree. Auto blends
                // toward prediction, Strong can fully adopt it. This removes the lag inherent in
                // alpha6's temporal median on genuine acceleration while retaining phase-slip
                // rejection for unstable tracks.
                if (count >= 2u) {
                    const float a1x = hx[0] - hx[1];
                    const float a1y = hy[0] - hy[1];
                    float predictionWeight = _mode == NvofReliabilityMode::Strong ? 0.42f : 0.18f;
                    if (count >= 3u) {
                        const float a2x = hx[1] - hx[2];
                        const float a2y = hy[1] - hy[2];
                        const float accelDelta = std::hypot(a1x - a2x, a1y - a2y);
                        const float accelMag = std::max(std::hypot(a1x, a1y), std::hypot(a2x, a2y));
                        const float accelAgreement = std::clamp(1.0f - accelDelta / std::max(0.65f, 0.70f + 0.24f * accelMag), 0.0f, 1.0f);
                        predictionWeight = (_mode == NvofReliabilityMode::Strong ? 0.55f : 0.28f) +
                                           (_mode == NvofReliabilityMode::Strong ? 0.45f : 0.42f) * accelAgreement;
                    }
                    predictionWeight *= std::clamp(historyReliability, 0.0f, 1.0f);
                    const float predictedX = hx[0] + a1x;
                    const float predictedY = hy[0] + a1y;
                    expectedX += (predictedX - expectedX) * predictionWeight;
                    expectedY += (predictedY - expectedY) * predictionWeight;
                }

                const float expectedMag = std::hypot(expectedX, expectedY);
                const float residual = std::hypot(curX - expectedX, curY - expectedY);
                const float uncertaintyScale = std::clamp(1.10f + 0.55f * historyUncertainty - 0.38f * curUncertainty, 0.72f, 1.55f);
                const float threshold = (cfg.residualBase + cfg.residualScale * expectedMag + cfg.spreadScale * spread) * uncertaintyScale;
                const bool historyStable = spread <= cfg.maxHistorySpread + 0.06f * expectedMag;
                const bool phaseSlipLike = historyStable && residual > threshold * cfg.phaseSlipMultiplier;

                bool shouldCorrect = false;
                if (_mode == NvofReliabilityMode::Strong) {
                    shouldCorrect = historyStable && historyConfidence >= cfg.historyConfidence && residual > threshold;
                } else {
                    shouldCorrect = historyStable && residual > threshold &&
                                    ((curConf < 0.70f && historyConfidence >= 0.68f) ||
                                     (phaseSlipLike && historyConfidence >= 0.80f &&
                                      (curConf < 0.92f || curUncertainty > 0.45f)));
                }
                if (!shouldCorrect) continue;

                const float severity = std::clamp((residual - threshold) / std::max(0.75f, threshold * 1.6f), 0.0f, 1.0f);
                float blend = cfg.maxBlend * (0.30f + 0.70f * severity) * (0.45f + 0.55f * historyReliability);
                blend *= (0.58f + 0.42f * curVisibility) * (0.62f + 0.38f * curUncertainty) *
                         (1.0f - 0.42f * historyUncertainty);

                // A high-current-confidence vector can still be a self-consistent periodic phase
                // lock, but Auto should be cautious. Strong deliberately trusts stable temporal
                // history more and therefore keeps a larger override even in that case.
                if (curConf >= cfg.highConfidenceProtection && curUncertainty < 0.36f)
                    blend *= cfg.highConfidenceBlendScale;

                // If the robust global motion independently supports the temporal candidate and
                // disagrees with current, slightly strengthen the vote. This remains a secondary
                // signal; moving foreground objects are not forced onto camera motion.
                const float globalMag = std::hypot(current.robustGlobalMotionX, current.robustGlobalMotionY);
                if (globalMag > 0.25f) {
                    const float expectedToGlobal = std::hypot(expectedX - current.robustGlobalMotionX,
                                                              expectedY - current.robustGlobalMotionY);
                    const float currentToGlobal = std::hypot(curX - current.robustGlobalMotionX,
                                                             curY - current.robustGlobalMotionY);
                    if (expectedToGlobal + 0.5f < currentToGlobal) blend = std::min(cfg.maxBlend, blend * 1.12f);
                }

                if (blend < 0.04f) continue;
                correctionXY[gi * 2u + 0u] = (expectedX - curX) * blend;
                correctionXY[gi * 2u + 1u] = (expectedY - curY) * blend;
                correctionWeight[gi] = blend;
                correctedCells.fetch_add(1u, std::memory_order_relaxed);
                residualMilliSum.fetch_add(static_cast<uint64_t>(std::min(100000.0f, residual * 1000.0f)), std::memory_order_relaxed);
            }
        }
    });

    // Bilinearly spread only the correction delta back to full resolution. This preserves
    // alpha2's detailed NVOF field and applies temporal consensus as a low-frequency trajectory
    // correction instead of replacing the whole motion field with a coarse history lattice.
    if (correctedCells.load(std::memory_order_relaxed)) {
        perf::ParallelForRows(_height, 32u, [&](uint32_t y0, uint32_t y1, unsigned) {
            for (uint32_t y = y0; y < y1; ++y) {
                const float gyp = GridCoord(static_cast<float>(y), _step);
                for (uint32_t x = 0; x < _width; ++x) {
                    const float gxp = GridCoord(static_cast<float>(x), _step);
                    bool weightValid = false;
                    const float weight = BilinearScalar(correctionWeight, gridW, gridH, gxp, gyp, weightValid);
                    if (!weightValid || weight <= 0.001f) continue;
                    float dx = 0.0f, dy = 0.0f;
                    if (!BilinearVector(correctionXY, gridW, gridH, gxp, gyp, dx, dy)) continue;
                    const size_t i = static_cast<size_t>(y) * _width + x;
                    out.motionXY[i * 2u + 0u] += dx;
                    out.motionXY[i * 2u + 1u] += dy;
                    // Corrected vectors inherit some confidence from stable history, but never
                    // become artificially perfect. This keeps future-frame arbitration cautious.
                    out.confidence[i] = std::max(out.confidence[i], std::min(0.92f, 0.72f + 0.20f * weight));
                    if (out.motionUncertainty.size() == fullN) {
                        out.motionUncertainty[i] = std::max(0.10f, out.motionUncertainty[i] * (1.0f - 0.28f * weight));
                    }
                }
            }
        });
    }

    const uint64_t corrected = correctedCells.load(std::memory_order_relaxed);
    out.temporalConsensusCorrectedFraction = gridN ? static_cast<float>(corrected) / static_cast<float>(gridN) : 0.0f;
    out.temporalConsensusMeanResidual = corrected ?
        static_cast<float>(residualMilliSum.load(std::memory_order_relaxed)) / (1000.0f * static_cast<float>(corrected)) : 0.0f;
    out.temporalConsensusHistory = static_cast<uint32_t>(_history.size());

    PushHistory(MakeHistoryGrid(out, haveDepth ? depth : nullptr));
    return out;
}

} // namespace video
