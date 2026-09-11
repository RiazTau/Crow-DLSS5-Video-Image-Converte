#include "TemporalFlow.h"
#include "ParallelRows.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

struct GrayImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

float Luma(const uint8_t* p) {
    return 0.2126f * static_cast<float>(p[0]) +
           0.7152f * static_cast<float>(p[1]) +
           0.0722f * static_cast<float>(p[2]);
}

GrayImage DownscaleGray(const Rgba8Image& src, uint32_t targetWidth) {
    GrayImage out;
    if (!src.width || !src.height || src.pixels.size() != static_cast<size_t>(src.width) * src.height * 4u) {
        return out;
    }
    targetWidth = std::clamp(targetWidth, 64u, src.width);
    out.width = targetWidth;
    out.height = std::max(1u, static_cast<uint32_t>(std::lround(
        static_cast<double>(src.height) * static_cast<double>(out.width) / static_cast<double>(src.width))));
    out.pixels.resize(static_cast<size_t>(out.width) * out.height);

    video::perf::ParallelForRows(out.height, 24u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned) {
        for (uint32_t y = rowBegin; y < rowEnd; ++y) {
        const uint32_t sy = std::min(src.height - 1u,
            static_cast<uint32_t>((static_cast<uint64_t>(y) * src.height) / out.height));
        for (uint32_t x = 0; x < out.width; ++x) {
            const uint32_t sx = std::min(src.width - 1u,
                static_cast<uint32_t>((static_cast<uint64_t>(x) * src.width) / out.width));
            const auto* p = &src.pixels[(static_cast<size_t>(sy) * src.width + sx) * 4u];
            out.pixels[static_cast<size_t>(y) * out.width + x] =
                static_cast<uint8_t>(std::clamp(std::lround(Luma(p)), 0l, 255l));
        }
    }
    });
    return out;
}

float SceneScore(const GrayImage& a, const GrayImage& b) {
    if (a.width != b.width || a.height != b.height || a.pixels.empty()) return 1.0f;
    constexpr int bins = 32;
    float ha[bins]{};
    float hb[bins]{};
    double mad = 0.0;
    const size_t n = a.pixels.size();
    for (size_t i = 0; i < n; ++i) {
        const uint8_t va = a.pixels[i];
        const uint8_t vb = b.pixels[i];
        ha[std::min(bins - 1, static_cast<int>(va) * bins / 256)] += 1.0f;
        hb[std::min(bins - 1, static_cast<int>(vb) * bins / 256)] += 1.0f;
        mad += std::abs(static_cast<int>(va) - static_cast<int>(vb));
    }
    float hist = 0.0f;
    const float invN = n ? 1.0f / static_cast<float>(n) : 0.0f;
    for (int i = 0; i < bins; ++i) hist += std::abs(ha[i] - hb[i]) * invN;
    hist *= 0.5f; // histogram L1 is [0,2]
    const float madNorm = n ? static_cast<float>(mad / static_cast<double>(n) / 255.0) : 1.0f;
    return std::clamp(hist * 0.65f + madNorm * 0.35f, 0.0f, 1.0f);
}

float BlockSad(const GrayImage& previous, const GrayImage& current,
               int cx, int cy, int px, int py, int halfBlock) {
    double sum = 0.0;
    int count = 0;
    // Sample every second pixel. This is intentionally cheap: V0.5.0 is a
    // calibration/prototype backend before the NVOFA backend lands.
    for (int oy = -halfBlock; oy <= halfBlock; oy += 2) {
        const int y0 = cy + oy;
        const int y1 = py + oy;
        if (y0 < 0 || y1 < 0 || y0 >= static_cast<int>(current.height) || y1 >= static_cast<int>(previous.height)) continue;
        for (int ox = -halfBlock; ox <= halfBlock; ox += 2) {
            const int x0 = cx + ox;
            const int x1 = px + ox;
            if (x0 < 0 || x1 < 0 || x0 >= static_cast<int>(current.width) || x1 >= static_cast<int>(previous.width)) continue;
            const int a = current.pixels[static_cast<size_t>(y0) * current.width + static_cast<size_t>(x0)];
            const int b = previous.pixels[static_cast<size_t>(y1) * previous.width + static_cast<size_t>(x1)];
            sum += std::abs(a - b);
            ++count;
        }
    }
    return count ? static_cast<float>(sum / (static_cast<double>(count) * 255.0)) : 1.0f;
}

float BilinearScalar(const std::vector<float>& v, uint32_t w, uint32_t h, float x, float y, float fallback = 0.0f) {
    if (!w || !h || v.size() != static_cast<size_t>(w) * h) return fallback;
    if (x < 0.0f || y < 0.0f || x > static_cast<float>(w - 1u) || y > static_cast<float>(h - 1u)) return fallback;
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
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

void BilinearRgba(const Rgba8Image& image, float x, float y, float out[4]) {
    if (!image.width || !image.height || image.pixels.empty() || x < 0.0f || y < 0.0f ||
        x > static_cast<float>(image.width - 1u) || y > static_cast<float>(image.height - 1u)) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(image.width - 1u, x0 + 1u);
    const uint32_t y1 = std::min(image.height - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    for (int c = 0; c < 4; ++c) {
        const float a = image.pixels[(static_cast<size_t>(y0) * image.width + x0) * 4u + c];
        const float b = image.pixels[(static_cast<size_t>(y0) * image.width + x1) * 4u + c];
        const float cc = image.pixels[(static_cast<size_t>(y1) * image.width + x0) * 4u + c];
        const float d = image.pixels[(static_cast<size_t>(y1) * image.width + x1) * 4u + c];
        const float top = a + (b - a) * tx;
        const float bottom = cc + (d - cc) * tx;
        out[c] = top + (bottom - top) * ty;
    }
}

} // namespace

namespace video {

TemporalFlowEstimator::TemporalFlowEstimator(TemporalFlowSettings settings) : _settings(settings) {
    _settings.analysisWidth = std::max(64u, _settings.analysisWidth);
    _settings.blockSize = std::max(4u, _settings.blockSize);
    _settings.searchRadius = std::max(1u, _settings.searchRadius);
    _settings.sceneCutThreshold = std::clamp(_settings.sceneCutThreshold, 0.05f, 0.95f);
    _settings.depthHistoryWeight = std::clamp(_settings.depthHistoryWeight, 0.0f, 0.8f);
    _settings.outputHistoryWeight = std::clamp(_settings.outputHistoryWeight, 0.0f, 0.8f);
}

TemporalFlowResult TemporalFlowEstimator::Estimate(const Rgba8Image& previous, const Rgba8Image& current) const {
    if (previous.width != current.width || previous.height != current.height || !current.width || !current.height) {
        throw std::runtime_error("Temporal flow requires same-sized consecutive frames");
    }

    TemporalFlowResult result;
    const size_t fullPixels = static_cast<size_t>(current.width) * current.height;
    result.motionXY.assign(fullPixels * 2u, 0.0f);
    result.confidence.assign(fullPixels, 0.0f);

    const auto prevSmall = DownscaleGray(previous, _settings.analysisWidth);
    const auto currSmall = DownscaleGray(current, _settings.analysisWidth);
    result.sceneCutScore = SceneScore(prevSmall, currSmall);
    result.sceneCut = result.sceneCutScore >= _settings.sceneCutThreshold;
    if (result.sceneCut || prevSmall.pixels.empty()) return result;

    const uint32_t block = _settings.blockSize;
    const uint32_t gridW = std::max(1u, (currSmall.width + block - 1u) / block);
    const uint32_t gridH = std::max(1u, (currSmall.height + block - 1u) / block);
    std::vector<float> gridX(static_cast<size_t>(gridW) * gridH, 0.0f);
    std::vector<float> gridY(static_cast<size_t>(gridW) * gridH, 0.0f);
    std::vector<float> gridC(static_cast<size_t>(gridW) * gridH, 0.0f);
    const int halfBlock = static_cast<int>(block / 2u);
    const int search = static_cast<int>(_settings.searchRadius);

    for (uint32_t gy = 0; gy < gridH; ++gy) {
        for (uint32_t gx = 0; gx < gridW; ++gx) {
            const int cx = std::min(static_cast<int>(currSmall.width) - 1,
                                    static_cast<int>(gx * block + block / 2u));
            const int cy = std::min(static_cast<int>(currSmall.height) - 1,
                                    static_cast<int>(gy * block + block / 2u));
            float best = std::numeric_limits<float>::max();
            float second = std::numeric_limits<float>::max();
            float bestRaw = std::numeric_limits<float>::max();
            float secondRaw = std::numeric_limits<float>::max();
            int bestDx = 0, bestDy = 0;
            for (int dy = -search; dy <= search; ++dy) {
                for (int dx = -search; dx <= search; ++dx) {
                    const int px = cx + dx;
                    const int py = cy + dy;
                    if (px < 0 || py < 0 || px >= static_cast<int>(prevSmall.width) || py >= static_cast<int>(prevSmall.height)) continue;
                    const float rawSad = BlockSad(prevSmall, currSmall, cx, cy, px, py, halfBlock);
                    // A tiny displacement prior resolves textureless-block ties without
                    // overpowering real image evidence. Dense methods such as DIS obtain
                    // a similar effect from spatial regularization.
                    const float displacement = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                    const float sad = rawSad + 0.0015f * displacement;
                    if (rawSad < bestRaw) { secondRaw = bestRaw; bestRaw = rawSad; }
                    else if (rawSad < secondRaw) { secondRaw = rawSad; }
                    if (sad < best) {
                        second = best;
                        best = sad;
                        bestDx = dx;
                        bestDy = dy;
                    } else if (sad < second) {
                        second = sad;
                    }
                }
            }
            const size_t gi = static_cast<size_t>(gy) * gridW + gx;
            gridX[gi] = static_cast<float>(bestDx);
            gridY[gi] = static_cast<float>(bestDy);
            const float uniqueness = (std::isfinite(secondRaw) && secondRaw > 1e-5f)
                ? std::clamp((secondRaw - bestRaw) / secondRaw, 0.0f, 1.0f) : 0.0f;
            const float match = std::clamp(1.0f - bestRaw * 2.5f, 0.0f, 1.0f);
            gridC[gi] = std::sqrt(uniqueness * match);
        }
    }

    // Lightweight spatial regularization: low-confidence blocks inherit motion
    // from confident neighbours. This fills textureless surfaces where block matching
    // has the same aperture ambiguity as other optical-flow methods.
    for (int pass = 0; pass < 3; ++pass) {
        auto nextX = gridX;
        auto nextY = gridY;
        auto nextC = gridC;
        for (uint32_t gy = 0; gy < gridH; ++gy) {
            for (uint32_t gx = 0; gx < gridW; ++gx) {
                const size_t gi = static_cast<size_t>(gy) * gridW + gx;
                if (gridC[gi] >= 0.18f) continue;
                float sumX = 0.0f, sumY = 0.0f, sumW = 0.0f, maxC = 0.0f;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        if (!ox && !oy) continue;
                        const int nx = static_cast<int>(gx) + ox;
                        const int ny = static_cast<int>(gy) + oy;
                        if (nx < 0 || ny < 0 || nx >= static_cast<int>(gridW) || ny >= static_cast<int>(gridH)) continue;
                        const size_t ni = static_cast<size_t>(ny) * gridW + static_cast<size_t>(nx);
                        if (gridC[ni] < 0.18f) continue;
                        const float w = gridC[ni];
                        sumX += gridX[ni] * w;
                        sumY += gridY[ni] * w;
                        sumW += w;
                        maxC = std::max(maxC, gridC[ni]);
                    }
                }
                if (sumW > 0.0f) {
                    nextX[gi] = sumX / sumW;
                    nextY[gi] = sumY / sumW;
                    nextC[gi] = maxC * 0.55f;
                }
            }
        }
        gridX.swap(nextX); gridY.swap(nextY); gridC.swap(nextC);
    }

    const float fullPerSmallX = static_cast<float>(current.width) / static_cast<float>(currSmall.width);
    const float fullPerSmallY = static_cast<float>(current.height) / static_cast<float>(currSmall.height);
    perf::ParallelForRows(current.height, 32u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned) {
        for (uint32_t y = rowBegin; y < rowEnd; ++y) {
        const float sy = (static_cast<float>(y) + 0.5f) / fullPerSmallY - 0.5f;
        const float gyF = std::clamp((sy - static_cast<float>(block) * 0.5f) / static_cast<float>(block),
                                     0.0f, static_cast<float>(gridH - 1u));
        const uint32_t gy0 = static_cast<uint32_t>(std::floor(gyF));
        const uint32_t gy1 = std::min(gridH - 1u, gy0 + 1u);
        const float ty = gyF - static_cast<float>(gy0);
        for (uint32_t x = 0; x < current.width; ++x) {
            const float sx = (static_cast<float>(x) + 0.5f) / fullPerSmallX - 0.5f;
            const float gxF = std::clamp((sx - static_cast<float>(block) * 0.5f) / static_cast<float>(block),
                                         0.0f, static_cast<float>(gridW - 1u));
            const uint32_t gx0 = static_cast<uint32_t>(std::floor(gxF));
            const uint32_t gx1 = std::min(gridW - 1u, gx0 + 1u);
            const float tx = gxF - static_cast<float>(gx0);
            auto sampleGrid = [&](const std::vector<float>& g) {
                const float a = g[static_cast<size_t>(gy0) * gridW + gx0];
                const float b = g[static_cast<size_t>(gy0) * gridW + gx1];
                const float c = g[static_cast<size_t>(gy1) * gridW + gx0];
                const float d = g[static_cast<size_t>(gy1) * gridW + gx1];
                const float top = a + (b - a) * tx;
                const float bottom = c + (d - c) * tx;
                return top + (bottom - top) * ty;
            };
            const size_t i = static_cast<size_t>(y) * current.width + x;
            result.motionXY[i * 2u + 0u] = sampleGrid(gridX) * fullPerSmallX;
            result.motionXY[i * 2u + 1u] = sampleGrid(gridY) * fullPerSmallY;
            result.confidence[i] = std::clamp(sampleGrid(gridC), 0.0f, 1.0f);
        }
    }
    });
    return result;
}

std::vector<float> TemporalFlowEstimator::StabilizeDepth(const std::vector<float>& currentDepth,
                                                          const std::vector<float>& previousStableDepth,
                                                          uint32_t width,
                                                          uint32_t height,
                                                          const TemporalFlowResult& flow) const {
    const size_t n = static_cast<size_t>(width) * height;
    if (currentDepth.size() != n || previousStableDepth.size() != n || flow.motionXY.size() != n * 2u || flow.confidence.size() != n || flow.sceneCut) {
        return currentDepth;
    }
    std::vector<float> out = currentDepth;
    perf::ParallelForRows(height, 32u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned) {
        for (uint32_t y = rowBegin; y < rowEnd; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * width + x;
            if (flow.confidence[i] < 0.25f) continue;
            const float px = static_cast<float>(x) + flow.motionXY[i * 2u + 0u];
            const float py = static_cast<float>(y) + flow.motionXY[i * 2u + 1u];
            const float prev = BilinearScalar(previousStableDepth, width, height, px, py, currentDepth[i]);
            if (std::abs(currentDepth[i] - prev) > _settings.depthRejectThreshold) continue;
            const float w = _settings.depthHistoryWeight * flow.confidence[i];
            out[i] = currentDepth[i] * (1.0f - w) + prev * w;
        }
    }
    });
    return out;
}

Rgba8Image TemporalFlowEstimator::StabilizeOutput(const Rgba8Image& currentOutput,
                                                   const Rgba8Image& previousStableOutput,
                                                   const TemporalFlowResult& flow) const {
    if (currentOutput.width != previousStableOutput.width || currentOutput.height != previousStableOutput.height ||
        currentOutput.pixels.size() != previousStableOutput.pixels.size() || flow.sceneCut) {
        return currentOutput;
    }
    const size_t n = static_cast<size_t>(currentOutput.width) * currentOutput.height;
    if (flow.motionXY.size() != n * 2u || flow.confidence.size() != n) return currentOutput;

    Rgba8Image out = currentOutput;
    perf::ParallelForRows(currentOutput.height, 32u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned) {
        for (uint32_t y = rowBegin; y < rowEnd; ++y) {
        for (uint32_t x = 0; x < currentOutput.width; ++x) {
            const size_t i = static_cast<size_t>(y) * currentOutput.width + x;
            const float confidence = flow.confidence[i];
            if (confidence < 0.35f) continue;
            float prev[4]{};
            const float px = static_cast<float>(x) + flow.motionXY[i * 2u + 0u];
            const float py = static_cast<float>(y) + flow.motionXY[i * 2u + 1u];
            BilinearRgba(previousStableOutput, px, py, prev);
            const auto* cur = &currentOutput.pixels[i * 4u];
            const float dr = std::abs(static_cast<float>(cur[0]) - prev[0]) / 255.0f;
            const float dg = std::abs(static_cast<float>(cur[1]) - prev[1]) / 255.0f;
            const float db = std::abs(static_cast<float>(cur[2]) - prev[2]) / 255.0f;
            if (std::max({dr, dg, db}) > _settings.outputRejectThreshold) continue;
            const float w = _settings.outputHistoryWeight * confidence;
            auto* dst = &out.pixels[i * 4u];
            for (int c = 0; c < 3; ++c) {
                dst[c] = static_cast<uint8_t>(std::clamp(std::lround(static_cast<float>(cur[c]) * (1.0f - w) + prev[c] * w), 0l, 255l));
            }
            dst[3] = cur[3];
        }
    }
    });
    return out;
}

} // namespace video
