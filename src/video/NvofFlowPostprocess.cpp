#include "NvofFlowPostprocess.h"
#include "ParallelRows.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace video {
namespace {

float Bilinear(const std::vector<float>& v, uint32_t w, uint32_t h, float x, float y) {
    if (!w || !h || v.size() != static_cast<size_t>(w) * h) return 0.0f;
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1u));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(w - 1u, x0 + 1u);
    const uint32_t y1 = std::min(h - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float a = v[static_cast<size_t>(y0) * w + x0];
    const float b = v[static_cast<size_t>(y0) * w + x1];
    const float c = v[static_cast<size_t>(y1) * w + x0];
    const float d = v[static_cast<size_t>(y1) * w + x1];
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

std::vector<float> BuildLandingCoverage(const std::vector<float>& vx,
                                        const std::vector<float>& vy,
                                        uint32_t gridW,
                                        uint32_t gridH,
                                        uint32_t sourceW,
                                        uint32_t sourceH) {
    const size_t n = static_cast<size_t>(gridW) * gridH;
    std::vector<float> coverage(n, 0.0f);
    if (!gridW || !gridH || !sourceW || !sourceH || vx.size() != n || vy.size() != n) return coverage;

    const float pixelsPerGridX = static_cast<float>(sourceW) / static_cast<float>(gridW);
    const float pixelsPerGridY = static_cast<float>(sourceH) / static_cast<float>(gridH);
    const float gridPerPixelX = 1.0f / pixelsPerGridX;
    const float gridPerPixelY = 1.0f / pixelsPerGridY;
    for (uint32_t gy = 0; gy < gridH; ++gy) {
        for (uint32_t gx = 0; gx < gridW; ++gx) {
            const size_t i = static_cast<size_t>(gy) * gridW + gx;
            const float sx = (static_cast<float>(gx) + 0.5f) * pixelsPerGridX - 0.5f;
            const float sy = (static_cast<float>(gy) + 0.5f) * pixelsPerGridY - 0.5f;
            const float dx = sx + vx[i];
            const float dy = sy + vy[i];
            if (!std::isfinite(dx) || !std::isfinite(dy) ||
                dx < 0.0f || dy < 0.0f || dx > static_cast<float>(sourceW - 1u) || dy > static_cast<float>(sourceH - 1u)) continue;

            const float tx = (dx + 0.5f) * gridPerPixelX - 0.5f;
            const float ty = (dy + 0.5f) * gridPerPixelY - 0.5f;
            const int x0 = static_cast<int>(std::floor(tx));
            const int y0 = static_cast<int>(std::floor(ty));
            const float fxw = tx - static_cast<float>(x0);
            const float fyw = ty - static_cast<float>(y0);
            for (int oy = 0; oy <= 1; ++oy) {
                const int yy = y0 + oy;
                if (yy < 0 || yy >= static_cast<int>(gridH)) continue;
                const float wy = oy ? fyw : 1.0f - fyw;
                for (int ox = 0; ox <= 1; ++ox) {
                    const int xx = x0 + ox;
                    if (xx < 0 || xx >= static_cast<int>(gridW)) continue;
                    const float wx = ox ? fxw : 1.0f - fxw;
                    coverage[static_cast<size_t>(yy) * gridW + static_cast<size_t>(xx)] += wx * wy;
                }
            }
        }
    }
    return coverage;
}

float LumaAt(const Rgba8Image& image, float x, float y, bool& valid) {
    valid = false;
    if (!image.width || !image.height || image.pixels.size() != static_cast<size_t>(image.width) * image.height * 4u ||
        x < 0.0f || y < 0.0f || x > static_cast<float>(image.width - 1u) || y > static_cast<float>(image.height - 1u)) {
        return 0.0f;
    }
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(image.width - 1u, x0 + 1u);
    const uint32_t y1 = std::min(image.height - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    auto sample = [&](uint32_t sx, uint32_t sy) {
        const auto* p = &image.pixels[(static_cast<size_t>(sy) * image.width + sx) * 4u];
        return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
    };
    const float a = sample(x0, y0);
    const float b = sample(x1, y0);
    const float c = sample(x0, y1);
    const float d = sample(x1, y1);
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    valid = true;
    return top + (bottom - top) * ty;
}

float CurrentLuma(const Rgba8Image& image, uint32_t x, uint32_t y) {
    const auto* p = &image.pixels[(static_cast<size_t>(y) * image.width + x) * 4u];
    return (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
}

float Median(std::vector<float>& values, float fallback = 0.0f) {
    if (values.empty()) return fallback;
    const size_t mid = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid), values.end());
    float v = values[mid];
    if ((values.size() & 1u) == 0u && mid) {
        const auto lo = std::max_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(mid));
        v = 0.5f * (v + *lo);
    }
    return v;
}

struct ReliableConfig {
    float rejectConfidence = 0.46f;
    float globalConfidence = 0.70f;
    float depthGate = 0.060f;
    float replacementBlend = 0.85f;
    float minNeighborVisibility = 0.52f;
    float maxNeighborUncertainty = 0.64f;
    uint32_t rings = 2;
};

ReliableConfig ConfigFor(NvofReliabilityMode mode) {
    ReliableConfig c;
    if (mode == NvofReliabilityMode::Strong) {
        c.rejectConfidence = 0.60f;
        c.globalConfidence = 0.76f;
        c.depthGate = 0.040f;
        c.replacementBlend = 1.0f;
        c.minNeighborVisibility = 0.42f;
        c.maxNeighborUncertainty = 0.72f;
        c.rings = 3;
    }
    return c;
}

struct AffineMotionModel {
    bool valid = false;
    // Coordinates are normalized around image center: nx=(x-cx)/scale, ny=(y-cy)/scale.
    float vx0 = 0.0f, vxx = 0.0f, vxy = 0.0f;
    float vy0 = 0.0f, vyx = 0.0f, vyy = 0.0f;
    float cx = 0.0f, cy = 0.0f, scale = 1.0f;

    std::pair<float, float> Evaluate(float x, float y) const noexcept {
        const float nx = (x - cx) / scale;
        const float ny = (y - cy) / scale;
        return {vx0 + vxx * nx + vxy * ny,
                vy0 + vyx * nx + vyy * ny};
    }
};

bool Solve3x3(float a[3][3], float b[3], float out[3]) {
    float m[3][4] = {
        {a[0][0], a[0][1], a[0][2], b[0]},
        {a[1][0], a[1][1], a[1][2], b[1]},
        {a[2][0], a[2][1], a[2][2], b[2]}
    };
    for (int col = 0; col < 3; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 3; ++r)
            if (std::abs(m[r][col]) > std::abs(m[pivot][col])) pivot = r;
        if (std::abs(m[pivot][col]) < 1e-7f) return false;
        if (pivot != col) for (int c = col; c < 4; ++c) std::swap(m[pivot][c], m[col][c]);
        const float inv = 1.0f / m[col][col];
        for (int c = col; c < 4; ++c) m[col][c] *= inv;
        for (int r = 0; r < 3; ++r) {
            if (r == col) continue;
            const float f = m[r][col];
            for (int c = col; c < 4; ++c) m[r][c] -= f * m[col][c];
        }
    }
    for (int i = 0; i < 3; ++i) out[i] = m[i][3];
    return true;
}

struct AffineSample {
    float nx = 0.0f, ny = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float baseWeight = 1.0f;
};

AffineMotionModel RobustAffineGlobalMotion(const TemporalFlowResult& flow,
                                           uint32_t width,
                                           uint32_t height,
                                           float confidenceThreshold) {
    AffineMotionModel model{};
    const size_t n = static_cast<size_t>(width) * height;
    if (!width || !height || flow.motionXY.size() != n * 2u || flow.confidence.size() != n) return model;
    const bool haveVisibility = flow.historyVisibility.size() == n;
    const bool haveUncertainty = flow.motionUncertainty.size() == n;
    model.cx = 0.5f * static_cast<float>(width - 1u);
    model.cy = 0.5f * static_cast<float>(height - 1u);
    model.scale = std::max(1.0f, 0.5f * static_cast<float>(std::max(width, height)));

    const uint32_t stride = std::max(6u, std::min(width, height) / 120u);
    std::vector<AffineSample> samples;
    samples.reserve((width / stride + 1u) * (height / stride + 1u));
    for (uint32_t y = stride / 2u; y < height; y += stride) {
        for (uint32_t x = stride / 2u; x < width; x += stride) {
            const size_t i = static_cast<size_t>(y) * width + x;
            const float conf = std::clamp(flow.confidence[i], 0.0f, 1.0f);
            if (conf < confidenceThreshold) continue;
            const float vis = haveVisibility ? std::clamp(flow.historyVisibility[i], 0.0f, 1.0f) : 1.0f;
            const float unc = haveUncertainty ? std::clamp(flow.motionUncertainty[i], 0.0f, 1.0f) : (1.0f - conf);
            if (vis < 0.45f || unc > 0.65f) continue;
            const float vx = flow.motionXY[i * 2u + 0u];
            const float vy = flow.motionXY[i * 2u + 1u];
            if (!std::isfinite(vx) || !std::isfinite(vy) || std::hypot(vx, vy) > 2048.0f) continue;
            AffineSample sample{};
            sample.nx = (static_cast<float>(x) - model.cx) / model.scale;
            sample.ny = (static_cast<float>(y) - model.cy) / model.scale;
            sample.vx = vx;
            sample.vy = vy;
            sample.baseWeight = std::max(1e-3f, conf * vis * (1.0f - 0.65f * unc));
            samples.push_back(sample);
        }
    }
    if (samples.size() < 24u) return model;

    std::vector<float> robustWeight(samples.size(), 1.0f);
    float coeffX[3]{}, coeffY[3]{};
    for (int iteration = 0; iteration < 3; ++iteration) {
        float normal[3][3]{};
        float rhsX[3]{}, rhsY[3]{};
        for (size_t i = 0; i < samples.size(); ++i) {
            const auto& s = samples[i];
            const float w = s.baseWeight * robustWeight[i];
            const float q[3] = {1.0f, s.nx, s.ny};
            for (int r = 0; r < 3; ++r) {
                rhsX[r] += w * q[r] * s.vx;
                rhsY[r] += w * q[r] * s.vy;
                for (int c = 0; c < 3; ++c) normal[r][c] += w * q[r] * q[c];
            }
        }
        float nx[3][3], ny[3][3];
        std::copy(&normal[0][0], &normal[0][0] + 9, &nx[0][0]);
        std::copy(&normal[0][0], &normal[0][0] + 9, &ny[0][0]);
        if (!Solve3x3(nx, rhsX, coeffX) || !Solve3x3(ny, rhsY, coeffY)) return AffineMotionModel{};

        std::vector<float> residuals;
        residuals.reserve(samples.size());
        for (const auto& s : samples) {
            const float px = coeffX[0] + coeffX[1] * s.nx + coeffX[2] * s.ny;
            const float py = coeffY[0] + coeffY[1] * s.nx + coeffY[2] * s.ny;
            residuals.push_back(std::hypot(s.vx - px, s.vy - py));
        }
        std::vector<float> tmp = residuals;
        const float medianResidual = Median(tmp, 0.0f);
        std::vector<float> deviations;
        deviations.reserve(residuals.size());
        for (float r : residuals) deviations.push_back(std::abs(r - medianResidual));
        tmp = deviations;
        const float mad = Median(tmp, 0.0f);
        const float sigma = std::max(0.35f, 1.4826f * mad);
        const float huber = std::max(0.75f, medianResidual + 1.75f * sigma);
        for (size_t i = 0; i < residuals.size(); ++i) {
            const float r = residuals[i];
            robustWeight[i] = r <= huber ? 1.0f : huber / std::max(r, 1e-5f);
        }
    }

    model.valid = true;
    model.vx0 = coeffX[0]; model.vxx = coeffX[1]; model.vxy = coeffX[2];
    model.vy0 = coeffY[0]; model.vyx = coeffY[1]; model.vyy = coeffY[2];
    return model;
}

bool DepthCompatible(const std::vector<float>* depth, size_t center, size_t neighbor, float gate) {
    if (!depth) return true;
    const float a = (*depth)[center];
    const float b = (*depth)[neighbor];
    if (!std::isfinite(a) || !std::isfinite(b)) return false;
    // External/Auto depth is normalized before it reaches this stage. Use an absolute gate
    // with a tiny relative allowance so smooth depth ramps are traversable while silhouettes
    // remain protected from motion propagation across foreground/background boundaries.
    const float tolerance = std::max(gate, 0.05f * std::max(std::abs(a), std::abs(b)));
    return std::abs(a - b) <= tolerance;
}

void RepairRejectedMotion(TemporalFlowResult& flow,
                          const std::vector<float>* depth,
                          uint32_t width,
                          uint32_t height,
                          NvofReliabilityMode mode,
                          uint32_t neighborhoodStep) {
    if (mode == NvofReliabilityMode::Off || flow.sceneCut) return;
    const size_t n = static_cast<size_t>(width) * height;
    if (!width || !height || flow.motionXY.size() != n * 2u || flow.confidence.size() != n) return;
    if (depth && depth->size() != n) depth = nullptr;

    const ReliableConfig cfg = ConfigFor(mode);
    neighborhoodStep = std::clamp(neighborhoodStep, 1u, 8u);
    const bool haveVisibility = flow.historyVisibility.size() == n;
    const bool haveDisocclusion = flow.disocclusionProbability.size() == n;
    const bool haveUncertainty = flow.motionUncertainty.size() == n;

    const AffineMotionModel globalModel = RobustAffineGlobalMotion(flow, width, height, cfg.globalConfidence);
    const auto [globalCenterX, globalCenterY] = globalModel.valid ?
        globalModel.Evaluate(0.5f * static_cast<float>(width - 1u), 0.5f * static_cast<float>(height - 1u)) :
        std::pair<float, float>{0.0f, 0.0f};
    flow.robustGlobalMotionX = globalCenterX;
    flow.robustGlobalMotionY = globalCenterY;

    const std::vector<float> original = flow.motionXY;
    std::atomic<uint64_t> repaired{0};
    std::atomic<uint64_t> localModeRepaired{0};
    std::atomic<uint64_t> affineFallbackRepaired{0};

    perf::ParallelForRows(height, 32u, [&](uint32_t y0, uint32_t y1, unsigned) {
        std::array<float, 24> vx{};
        std::array<float, 24> vy{};
        std::array<float, 24> weights{};
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t i = static_cast<size_t>(y) * width + x;
                const float conf = std::clamp(flow.confidence[i], 0.0f, 1.0f);
                const float targetVisibility = haveVisibility ? std::clamp(flow.historyVisibility[i], 0.0f, 1.0f) : 1.0f;
                const float targetUncertainty = haveUncertainty ? std::clamp(flow.motionUncertainty[i], 0.0f, 1.0f) : (1.0f - conf);
                // Alpha6 no longer equates NVOFA's own confidence with truth. Periodic phase
                // locks can be high-confidence while independent spatial/FB/temporal evidence
                // says the vector is ambiguous. Effective trust opens those vectors to active
                // reconstruction, while genuinely high-confidence + low-uncertainty vectors
                // remain protected.
                const float effectiveTrust = conf * (0.55f + 0.45f * targetVisibility) *
                                             (1.0f - 0.58f * targetUncertainty);
                const bool independentlyProtected = conf >= (mode == NvofReliabilityMode::Strong ? 0.94f : 0.90f) &&
                                                    targetUncertainty <= 0.24f && targetVisibility >= 0.72f;
                if (independentlyProtected || effectiveTrust >= cfg.rejectConfidence) continue;

                size_t count = 0;
                for (uint32_t ring = 1; ring <= cfg.rings && count < vx.size(); ++ring) {
                    const int d = static_cast<int>(ring * neighborhoodStep);
                    static constexpr int dirs[8][2] = {
                        {-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}
                    };
                    for (const auto& dir : dirs) {
                        const int nx = static_cast<int>(x) + dir[0] * d;
                        const int ny = static_cast<int>(y) + dir[1] * d;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(width) || ny >= static_cast<int>(height)) continue;
                        const size_t ni = static_cast<size_t>(ny) * width + static_cast<size_t>(nx);
                        const float nc = flow.confidence[ni];
                        const float nv = haveVisibility ? std::clamp(flow.historyVisibility[ni], 0.0f, 1.0f) : 1.0f;
                        const float nu = haveUncertainty ? std::clamp(flow.motionUncertainty[ni], 0.0f, 1.0f) : (1.0f - nc);
                        if (nc < cfg.rejectConfidence || nv < cfg.minNeighborVisibility ||
                            nu > cfg.maxNeighborUncertainty || !DepthCompatible(depth, i, ni, cfg.depthGate)) continue;
                        vx[count] = original[ni * 2u + 0u];
                        vy[count] = original[ni * 2u + 1u];
                        weights[count] = nc * nv * (1.0f - 0.65f * nu) / static_cast<float>(ring);
                        ++count;
                        if (count == vx.size()) break;
                    }
                }

                const float targetDisocclusion = haveDisocclusion ? std::clamp(flow.disocclusionProbability[i], 0.0f, 1.0f) : 0.0f;
                float replacementX = 0.0f;
                float replacementY = 0.0f;
                bool haveReplacement = false;
                bool usedLocalMode = false;
                bool usedAffineFallback = false;
                float localCoherence = 0.0f;

                // Alpha6 uses a weighted vector medoid first, then averages only the dominant
                // motion mode around that medoid. Unlike a plain mean this cannot invent a
                // synthetic halfway vector when foreground and background meet at a silhouette.
                if (count >= 2u) {
                    float totalWeight = 0.0f;
                    for (size_t k = 0; k < count; ++k) totalWeight += weights[k];
                    size_t medoid = 0;
                    float bestScore = std::numeric_limits<float>::infinity();
                    for (size_t k = 0; k < count; ++k) {
                        float score = 0.0f;
                        for (size_t j = 0; j < count; ++j)
                            score += weights[j] * std::hypot(vx[k] - vx[j], vy[k] - vy[j]);
                        if (score < bestScore) { bestScore = score; medoid = k; }
                    }
                    const float medoidMag = std::hypot(vx[medoid], vy[medoid]);
                    const float clusterRadius = (mode == NvofReliabilityMode::Strong ? 1.10f : 1.35f) +
                                                (mode == NvofReliabilityMode::Strong ? 0.10f : 0.14f) * medoidMag;
                    float sx = 0.0f, sy = 0.0f, sw = 0.0f;
                    size_t clusterCount = 0;
                    for (size_t k = 0; k < count; ++k) {
                        if (std::hypot(vx[k] - vx[medoid], vy[k] - vy[medoid]) > clusterRadius) continue;
                        sx += vx[k] * weights[k];
                        sy += vy[k] * weights[k];
                        sw += weights[k];
                        ++clusterCount;
                    }
                    localCoherence = totalWeight > 1e-6f ? sw / totalWeight : 0.0f;
                    const float minShare = mode == NvofReliabilityMode::Strong ? 0.46f : 0.56f;
                    if (clusterCount >= 2u && sw > 1e-6f && localCoherence >= minShare) {
                        replacementX = sx / sw;
                        replacementY = sy / sw;
                        haveReplacement = true;
                        usedLocalMode = true;
                    }
                }

                // Only if a coherent local motion mode is unavailable do we fall back to a
                // robust affine camera model. This handles pan + rotation + zoom substantially
                // better than alpha5's single global translation while remaining secondary to
                // real local object motion. Newly revealed surfaces still forbid global fallback.
                if (!haveReplacement && globalModel.valid && targetDisocclusion < 0.45f) {
                    const auto predicted = globalModel.Evaluate(static_cast<float>(x), static_cast<float>(y));
                    replacementX = predicted.first;
                    replacementY = predicted.second;
                    haveReplacement = true;
                    usedAffineFallback = true;
                }
                if (!haveReplacement) continue;

                const float rawX = original[i * 2u + 0u];
                const float rawY = original[i * 2u + 1u];
                const float severity = std::clamp((cfg.rejectConfidence - effectiveTrust) / std::max(cfg.rejectConfidence, 1e-5f), 0.0f, 1.0f);
                const float blend = cfg.replacementBlend * (0.35f + 0.65f * severity);
                flow.motionXY[i * 2u + 0u] = rawX + (replacementX - rawX) * blend;
                flow.motionXY[i * 2u + 1u] = rawY + (replacementY - rawY) * blend;
                if (haveUncertainty && count >= 2u) {
                    // Local consensus improves the motion *candidate* but cannot manufacture
                    // historical visibility for a disoccluded pixel. Keep topology separate.
                    const float coherenceGain = std::clamp(localCoherence, 0.0f, 1.0f);
                    flow.motionUncertainty[i] = std::max(targetDisocclusion * 0.75f,
                        std::min(flow.motionUncertainty[i], 0.76f - 0.28f * coherenceGain));
                }
                repaired.fetch_add(1u, std::memory_order_relaxed);
                if (usedLocalMode) localModeRepaired.fetch_add(1u, std::memory_order_relaxed);
                if (usedAffineFallback) affineFallbackRepaired.fetch_add(1u, std::memory_order_relaxed);
            }
        }
    });

    flow.repairedFraction = n ? static_cast<float>(repaired.load(std::memory_order_relaxed)) / static_cast<float>(n) : 0.0f;
    flow.localModeRepairedFraction = n ? static_cast<float>(localModeRepaired.load(std::memory_order_relaxed)) / static_cast<float>(n) : 0.0f;
    flow.affineFallbackFraction = n ? static_cast<float>(affineFallbackRepaired.load(std::memory_order_relaxed)) / static_cast<float>(n) : 0.0f;
}

} // namespace

float DecodeNvofFixed11_5(int16_t value) noexcept {
    return static_cast<float>(value) * (1.0f / 32.0f);
}

TemporalFlowResult PostprocessNvofFlow(const NvofPostprocessInput& in) {
    if (!in.sourceWidth || !in.sourceHeight || !in.gridWidth || !in.gridHeight || !in.forward) {
        throw std::runtime_error("NVOF postprocess received incomplete dimensions/input");
    }
    const size_t gridN = static_cast<size_t>(in.gridWidth) * in.gridHeight;
    if (in.forward->size() != gridN) throw std::runtime_error("NVOF forward grid size mismatch");
    if (in.backward && in.backward->size() != gridN) throw std::runtime_error("NVOF backward grid size mismatch");
    if (in.forwardCost && in.forwardCost->size() != gridN) throw std::runtime_error("NVOF forward cost grid size mismatch");
    if (in.backwardCost && in.backwardCost->size() != gridN) throw std::runtime_error("NVOF backward cost grid size mismatch");

    TemporalFlowResult result;
    result.sceneCutScore = in.sceneCutScore;
    result.sceneCut = in.sceneCut;
    const size_t fullN = static_cast<size_t>(in.sourceWidth) * in.sourceHeight;
    result.motionXY.assign(fullN * 2u, 0.0f);
    result.rawMotionXY.assign(fullN * 2u, 0.0f);
    result.confidence.assign(fullN, 0.0f);
    result.historyVisibility.assign(fullN, 0.0f);
    result.occlusionProbability.assign(fullN, 0.0f);
    result.disocclusionProbability.assign(fullN, 0.0f);
    result.motionUncertainty.assign(fullN, 1.0f);
    if (in.sceneCut) {
        result.meanHistoryVisibility = 0.0f;
        result.disoccludedFraction = 1.0f;
        result.highUncertaintyFraction = 1.0f;
        return result;
    }

    std::vector<float> fx(gridN), fy(gridN), bx, by, costF, costB;
    if (in.backward) { bx.resize(gridN); by.resize(gridN); }
    if (in.forwardCost) costF.resize(gridN);
    if (in.backwardCost) costB.resize(gridN);
    for (size_t i = 0; i < gridN; ++i) {
        fx[i] = DecodeNvofFixed11_5((*in.forward)[i].x);
        fy[i] = DecodeNvofFixed11_5((*in.forward)[i].y);
        if (in.backward) {
            bx[i] = DecodeNvofFixed11_5((*in.backward)[i].x);
            by[i] = DecodeNvofFixed11_5((*in.backward)[i].y);
        }
        if (in.forwardCost) costF[i] = static_cast<float>((*in.forwardCost)[i]) / 255.0f;
        if (in.backwardCost) costB[i] = static_cast<float>((*in.backwardCost)[i]) / 255.0f;
    }

    // Alpha5 visibility topology: splat both directions into the opposite frame on the
    // native NVOF grid. Backward landing coverage identifies current pixels that have no
    // plausible previous-frame source (disocclusion). Forward over-coverage identifies
    // many-to-one correspondence competition near occlusion boundaries. Keeping this on
    // the NVOF grid makes the signal cheap and naturally low-pass instead of noisy per-pixel.
    const std::vector<float> forwardLanding = BuildLandingCoverage(fx, fy, in.gridWidth, in.gridHeight,
                                                                    in.sourceWidth, in.sourceHeight);
    const std::vector<float> backwardLanding = bx.empty() ? std::vector<float>{} :
        BuildLandingCoverage(bx, by, in.gridWidth, in.gridHeight, in.sourceWidth, in.sourceHeight);

    // NVOFA flow values are expressed in input-image pixel units even when the output is
    // a coarser vector grid. Upsampling therefore interpolates vectors but does NOT multiply
    // their magnitude by the grid spacing.
    const float gridPerPixelX = static_cast<float>(in.gridWidth) / static_cast<float>(in.sourceWidth);
    const float gridPerPixelY = static_cast<float>(in.gridHeight) / static_cast<float>(in.sourceHeight);
    const bool photo = in.previousFrame && in.currentFrame &&
        in.previousFrame->width == in.sourceWidth && in.previousFrame->height == in.sourceHeight &&
        in.currentFrame->width == in.sourceWidth && in.currentFrame->height == in.sourceHeight;

    perf::ParallelForRows(in.sourceHeight, 32u, [&](uint32_t y0, uint32_t y1, unsigned) {
        for (uint32_t y = y0; y < y1; ++y) {
            const float gy = (static_cast<float>(y) + 0.5f) * gridPerPixelY - 0.5f;
            for (uint32_t x = 0; x < in.sourceWidth; ++x) {
                const float gx = (static_cast<float>(x) + 0.5f) * gridPerPixelX - 0.5f;
                const size_t i = static_cast<size_t>(y) * in.sourceWidth + x;
                const float mvx = Bilinear(fx, in.gridWidth, in.gridHeight, gx, gy);
                const float mvy = Bilinear(fy, in.gridWidth, in.gridHeight, gx, gy);
                result.motionXY[i * 2u] = mvx;
                result.motionXY[i * 2u + 1u] = mvy;

                float confCostF = 0.80f;
                if (!costF.empty()) confCostF = std::clamp(1.0f - Bilinear(costF, in.gridWidth, in.gridHeight, gx, gy), 0.0f, 1.0f);

                float confFb = 0.80f;
                float confCostB = 0.80f;
                float fbErr = 0.0f;
                const float px = static_cast<float>(x) + mvx;
                const float py = static_cast<float>(y) + mvy;
                const bool projectedInBounds = px >= 0.0f && py >= 0.0f &&
                                               px <= static_cast<float>(in.sourceWidth - 1u) &&
                                               py <= static_cast<float>(in.sourceHeight - 1u);
                float previousGridX = projectedInBounds ? (px + 0.5f) * gridPerPixelX - 0.5f : gx;
                float previousGridY = projectedInBounds ? (py + 0.5f) * gridPerPixelY - 0.5f : gy;
                bool historyInBounds = projectedInBounds;
                if (!bx.empty()) {
                    // F maps current q -> previous p. B is previous -> current, so a correct
                    // pair satisfies F(q) + B(p) ~= 0 where p=q+F(q).
                    if (historyInBounds) {
                        const float bmvx = Bilinear(bx, in.gridWidth, in.gridHeight, previousGridX, previousGridY);
                        const float bmvy = Bilinear(by, in.gridWidth, in.gridHeight, previousGridX, previousGridY);
                        fbErr = std::hypot(mvx + bmvx, mvy + bmvy);
                        confFb = std::exp(-fbErr / 1.35f);
                        if (!costB.empty()) confCostB = std::clamp(1.0f - Bilinear(costB, in.gridWidth, in.gridHeight, previousGridX, previousGridY), 0.0f, 1.0f);
                    } else {
                        confFb = 0.0f;
                        confCostB = 0.0f;
                        fbErr = 1000.0f;
                    }
                }

                float confPhoto = 0.80f;
                if (photo) {
                    bool valid = false;
                    const float prevLuma = LumaAt(*in.previousFrame, static_cast<float>(x) + mvx, static_cast<float>(y) + mvy, valid);
                    if (valid) {
                        const float residual = std::abs(CurrentLuma(*in.currentFrame, x, y) - prevLuma);
                        confPhoto = std::exp(-residual / 0.075f);
                    } else {
                        confPhoto = 0.0f;
                        historyInBounds = false;
                    }
                }

                // Soft topology is deliberately separate from appearance confidence. A current
                // pixel may have a low photometric score because lighting changed while still
                // having a valid historical source; conversely, a disoccluded pixel must reject
                // NR history even if a locally similar texture produces a plausible cost.
                float disocclusion = historyInBounds ? 0.0f : 1.0f;
                float occlusionAmbiguity = 0.0f;
                if (!backwardLanding.empty()) {
                    const float reverseCoverage = std::max(0.0f, Bilinear(backwardLanding, in.gridWidth, in.gridHeight, gx, gy));
                    disocclusion = std::max(disocclusion, std::clamp((0.80f - reverseCoverage) / 0.80f, 0.0f, 1.0f));
                }
                if (historyInBounds && !forwardLanding.empty()) {
                    const float forwardDensity = std::max(0.0f, Bilinear(forwardLanding, in.gridWidth, in.gridHeight, previousGridX, previousGridY));
                    occlusionAmbiguity = std::clamp((forwardDensity - 1.20f) / 1.40f, 0.0f, 1.0f);
                }
                const float fbVisibility = bx.empty() ? (historyInBounds ? 0.85f : 0.0f) : std::exp(-fbErr / 2.25f);
                const float historyVisibility = std::clamp(fbVisibility * (1.0f - disocclusion) *
                                                           (1.0f - 0.55f * occlusionAmbiguity), 0.0f, 1.0f);

                // Cost catches local NVOFA uncertainty, FB catches wrong periodic-phase locks,
                // and photometric residual catches reprojections that disagree with the actual
                // source imagery. Backward cost is sampled at the reprojected previous point.
                const float costPair = std::sqrt(std::max(0.0f, confCostF * confCostB));
                const float confidence = std::clamp(0.25f * costPair + 0.50f * confFb + 0.25f * confPhoto, 0.0f, 1.0f);
                result.confidence[i] = confidence;
                result.historyVisibility[i] = historyVisibility;
                result.disocclusionProbability[i] = disocclusion;
                result.occlusionProbability[i] = occlusionAmbiguity;
                const float evidence = std::clamp(0.30f * costPair + 0.30f * confFb +
                                                  0.15f * confPhoto + 0.25f * historyVisibility, 0.0f, 1.0f);
                result.motionUncertainty[i] = std::clamp(std::max({1.0f - evidence,
                                                                  0.78f * disocclusion,
                                                                  0.48f * occlusionAmbiguity}), 0.0f, 1.0f);
            }
        }
    });

    // Compact diagnostics are derived after the parallel pixel pass to avoid per-pixel
    // atomic contention on 4K/8K material.
    double visibilitySum = 0.0;
    uint64_t disoccluded = 0, occlusionAmbiguous = 0, uncertain = 0;
    for (size_t i = 0; i < fullN; ++i) {
        visibilitySum += result.historyVisibility[i];
        if (result.disocclusionProbability[i] >= 0.50f) ++disoccluded;
        if (result.occlusionProbability[i] >= 0.50f) ++occlusionAmbiguous;
        if (result.motionUncertainty[i] >= 0.65f) ++uncertain;
    }
    result.meanHistoryVisibility = fullN ? static_cast<float>(visibilitySum / static_cast<double>(fullN)) : 0.0f;
    result.disoccludedFraction = fullN ? static_cast<float>(disoccluded) / static_cast<float>(fullN) : 0.0f;
    result.occlusionAmbiguousFraction = fullN ? static_cast<float>(occlusionAmbiguous) / static_cast<float>(fullN) : 0.0f;
    result.highUncertaintyFraction = fullN ? static_cast<float>(uncertain) / static_cast<float>(fullN) : 0.0f;

    // Preserve the original NVOF field before the aggressive alpha2 repair. Alpha5 uses
    // the same confidence + visibility analysis to derive separate NR-safe and FG-oriented motion fields.
    result.rawMotionXY = result.motionXY;
    RepairRejectedMotion(result, nullptr, in.sourceWidth, in.sourceHeight,
                         in.reliabilityMode, std::max(1u, in.flowGridSize));
    return result;
}

TemporalFlowResult BuildNrSafeReliableMotion(const TemporalFlowResult& flow,
                                             const std::vector<float>* depth,
                                             uint32_t width,
                                             uint32_t height,
                                             NvofReliabilityMode mode,
                                             uint32_t neighborhoodStep) {
    TemporalFlowResult out = flow;
    if (mode == NvofReliabilityMode::Off || flow.sceneCut) return out;
    const size_t n = static_cast<size_t>(width) * height;
    if (!width || !height || flow.motionXY.size() != n * 2u || flow.confidence.size() != n) return out;
    if (depth && depth->size() != n) depth = nullptr;

    const std::vector<float>& raw = flow.rawMotionXY.size() == n * 2u ? flow.rawMotionXY : flow.motionXY;

    // Candidate motion remains the stronger FG-style repair, because it is useful as a second
    // opinion. NR never accepts it wholesale: the blend is bounded and the confidence field
    // is reduced when the raw/candidate disagreement indicates unsafe history reprojection.
    TemporalFlowResult candidate = flow;
    // Alpha6 re-runs active reconstruction after spatial consensus even without depth.
    // Depth, when available, adds a silhouette gate; it is no longer a prerequisite for
    // consuming cross-scale uncertainty or dominant-motion / affine candidates.
    RepairRejectedMotion(candidate, depth, width, height, mode, neighborhoodStep);

    out.motionXY = raw;
    const bool haveVisibility = flow.historyVisibility.size() == n;
    const bool haveDisocclusion = flow.disocclusionProbability.size() == n;
    const bool haveOcclusion = flow.occlusionProbability.size() == n;
    const bool haveUncertainty = flow.motionUncertainty.size() == n;
    const float protectConfidence = mode == NvofReliabilityMode::Strong ? 0.82f : 0.76f;
    const float correctBelow = mode == NvofReliabilityMode::Strong ? 0.64f : 0.54f;
    const float hardReject = mode == NvofReliabilityMode::Strong ? 0.34f : 0.28f;
    const float maxBlend = mode == NvofReliabilityMode::Strong ? 0.60f : 0.46f;
    std::atomic<uint64_t> corrected{0};
    std::atomic<uint64_t> rejected{0};

    perf::ParallelForRows(height, 32u, [&](uint32_t y0, uint32_t y1, unsigned) {
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                const size_t i = static_cast<size_t>(y) * width + x;
                const float conf = std::clamp(flow.confidence[i], 0.0f, 1.0f);
                const float visibility = haveVisibility ? std::clamp(flow.historyVisibility[i], 0.0f, 1.0f) : 1.0f;
                const float disocclusion = haveDisocclusion ? std::clamp(flow.disocclusionProbability[i], 0.0f, 1.0f) : 0.0f;
                const float occlusion = haveOcclusion ? std::clamp(flow.occlusionProbability[i], 0.0f, 1.0f) : 0.0f;
                const float uncertainty = haveUncertainty ? std::clamp(flow.motionUncertainty[i], 0.0f, 1.0f) : (1.0f - conf);
                const float rawX = raw[i * 2u + 0u];
                const float rawY = raw[i * 2u + 1u];
                const float candX = candidate.motionXY[i * 2u + 0u];
                const float candY = candidate.motionXY[i * 2u + 1u];
                if (!std::isfinite(rawX) || !std::isfinite(rawY) || !std::isfinite(candX) || !std::isfinite(candY)) {
                    out.confidence[i] = 0.0f;
                    rejected.fetch_add(1u, std::memory_order_relaxed);
                    continue;
                }

                const float delta = std::hypot(candX - rawX, candY - rawY);
                const float rawMag = std::hypot(rawX, rawY);
                // Small disagreements are safe; large disagreements on low-confidence pixels are
                // exactly where NR should reduce history trust instead of following FG's full fix.
                const float safeDelta = 1.25f + 0.18f * rawMag;
                const float severeDelta = 3.0f + 0.30f * rawMag;

                float blend = 0.0f;
                const float arbitrationConfidence = conf * (0.45f + 0.55f * visibility) * (1.0f - 0.40f * uncertainty);
                if (arbitrationConfidence < protectConfidence && delta > 0.05f) {
                    const float severity = std::clamp((correctBelow - arbitrationConfidence) / std::max(correctBelow, 1e-5f), 0.0f, 1.0f);
                    const float agreement = std::clamp(1.0f - std::max(0.0f, delta - safeDelta) / std::max(1.0f, severeDelta), 0.0f, 1.0f);
                    blend = maxBlend * (0.22f + 0.78f * severity) * (0.45f + 0.55f * agreement);
                    if (arbitrationConfidence > correctBelow) blend *= 0.35f;
                    // NR never spends correction budget to create confidence in a surface that
                    // has no visible previous-frame source. It may still keep a bounded vector
                    // for NGX, but history trust below is driven independently by visibility.
                    blend *= 0.55f + 0.45f * (1.0f - disocclusion);
                }

                if (blend > 0.02f) {
                    out.motionXY[i * 2u + 0u] = rawX + (candX - rawX) * blend;
                    out.motionXY[i * 2u + 1u] = rawY + (candY - rawY) * blend;
                    corrected.fetch_add(1u, std::memory_order_relaxed);
                }

                float historyConfidence = conf * visibility * (1.0f - 0.65f * uncertainty);
                historyConfidence *= 1.0f - 0.35f * occlusion;
                if (delta > safeDelta) {
                    const float disagreement = std::clamp((delta - safeDelta) / std::max(1.0f, severeDelta - safeDelta), 0.0f, 1.0f);
                    historyConfidence *= 1.0f - 0.70f * disagreement;
                }
                if (conf < hardReject) {
                    historyConfidence = std::min(historyConfidence, mode == NvofReliabilityMode::Strong ? 0.10f : 0.08f);
                }
                if (disocclusion >= 0.80f || visibility <= 0.10f) historyConfidence = 0.0f;
                else if (disocclusion >= 0.50f) historyConfidence = std::min(historyConfidence, 0.04f);
                // Reprojection outside the image is never valid history.
                const float px = static_cast<float>(x) + out.motionXY[i * 2u + 0u];
                const float py = static_cast<float>(y) + out.motionXY[i * 2u + 1u];
                if (px < 0.0f || py < 0.0f || px > static_cast<float>(width - 1u) || py > static_cast<float>(height - 1u))
                    historyConfidence = 0.0f;

                out.confidence[i] = std::clamp(historyConfidence, 0.0f, 1.0f);
                if (out.confidence[i] < 0.08f) rejected.fetch_add(1u, std::memory_order_relaxed);
            }
        }
    });

    out.nrSafeCorrectedFraction = n ? static_cast<float>(corrected.load(std::memory_order_relaxed)) / static_cast<float>(n) : 0.0f;
    out.nrHistoryRejectedFraction = n ? static_cast<float>(rejected.load(std::memory_order_relaxed)) / static_cast<float>(n) : 0.0f;
    return out;
}

void RefineReliableMotionWithDepth(TemporalFlowResult& flow,
                                   const std::vector<float>* depth,
                                   uint32_t width,
                                   uint32_t height,
                                   NvofReliabilityMode mode,
                                   uint32_t neighborhoodStep) {
    if (depth && depth->size() != static_cast<size_t>(width) * height) depth = nullptr;
    // Kept under the historical function name for API compatibility. In alpha6 this is
    // the post-spatial active-reconstruction pass; depth is an optional boundary gate.
    RepairRejectedMotion(flow, depth, width, height, mode, neighborhoodStep);
}

} // namespace video
