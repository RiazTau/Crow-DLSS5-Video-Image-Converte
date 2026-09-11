#include "TemporalDenoiser.h"
#include "ParallelRows.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

struct YCoCg {
    float y = 0.0f;
    float co = 0.0f;
    float cg = 0.0f;
};

YCoCg ToYCoCg(float r, float g, float b) {
    // Reversible-style YCoCg transform, normalized to roughly [-0.5, 1].
    return {
        0.25f * r + 0.50f * g + 0.25f * b,
        0.50f * r - 0.50f * b,
       -0.25f * r + 0.50f * g - 0.25f * b,
    };
}

void FromYCoCg(const YCoCg& c, float& r, float& g, float& b) {
    r = c.y + c.co - c.cg;
    g = c.y + c.cg;
    b = c.y - c.co - c.cg;
}

YCoCg PixelYCoCg(const Rgba8Image& image, uint32_t x, uint32_t y) {
    x = std::min(image.width - 1u, x);
    y = std::min(image.height - 1u, y);
    const auto* p = &image.pixels[(static_cast<size_t>(y) * image.width + x) * 4u];
    constexpr float inv255 = 1.0f / 255.0f;
    return ToYCoCg(p[0] * inv255, p[1] * inv255, p[2] * inv255);
}

void BilinearRgba01(const Rgba8Image& image, float x, float y, float out[4]) {
    if (!image.width || !image.height || image.pixels.empty() ||
        x < 0.0f || y < 0.0f || x > static_cast<float>(image.width - 1u) ||
        y > static_cast<float>(image.height - 1u)) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(image.width - 1u, x0 + 1u);
    const uint32_t y1 = std::min(image.height - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    constexpr float inv255 = 1.0f / 255.0f;
    for (int c = 0; c < 4; ++c) {
        const float a = image.pixels[(static_cast<size_t>(y0) * image.width + x0) * 4u + c] * inv255;
        const float b = image.pixels[(static_cast<size_t>(y0) * image.width + x1) * 4u + c] * inv255;
        const float cc = image.pixels[(static_cast<size_t>(y1) * image.width + x0) * 4u + c] * inv255;
        const float d = image.pixels[(static_cast<size_t>(y1) * image.width + x1) * 4u + c] * inv255;
        const float top = a + (b - a) * tx;
        const float bottom = cc + (d - cc) * tx;
        out[c] = top + (bottom - top) * ty;
    }
}

uint8_t SampleAgeNearest(const std::vector<uint8_t>& ages, uint32_t w, uint32_t h, float x, float y) {
    if (ages.size() != static_cast<size_t>(w) * h || x < 0.0f || y < 0.0f ||
        x > static_cast<float>(w - 1u) || y > static_cast<float>(h - 1u)) return 0;
    const uint32_t ix = std::min(w - 1u, static_cast<uint32_t>(std::lround(x)));
    const uint32_t iy = std::min(h - 1u, static_cast<uint32_t>(std::lround(y)));
    return ages[static_cast<size_t>(iy) * w + ix];
}

float SmoothStep01(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

} // namespace

namespace video {

TemporalDenoiser::TemporalDenoiser(TemporalDenoiseSettings settings) : _settings(settings) {
    _settings.strength = std::clamp(_settings.strength, 0.0f, 1.0f);
    _settings.historyWeight = std::clamp(_settings.historyWeight, 0.0f, 0.98f);
    _settings.spatialStrength = std::clamp(_settings.spatialStrength, 0.0f, 1.0f);
    _settings.detailProtection = std::clamp(_settings.detailProtection, 0.0f, 1.0f);
    _settings.maxHistory = std::clamp(_settings.maxHistory, 2u, 64u);
    _settings.lumaReject = std::clamp(_settings.lumaReject, 0.02f, 0.50f);
    _settings.chromaReject = std::clamp(_settings.chromaReject, 0.02f, 0.50f);
    _settings.clampExpansion = std::clamp(_settings.clampExpansion, 0.0f, 0.15f);
}

void TemporalDenoiser::Reset() {
    _history = {};
    _historyAge.clear();
}

Rgba8Image TemporalDenoiser::Process(const Rgba8Image& current,
                                     const TemporalFlowResult* flow,
                                     bool resetHistory,
                                     TemporalDenoiseStats* stats) {
    if (!current.width || !current.height ||
        current.pixels.size() != static_cast<size_t>(current.width) * current.height * 4u) {
        throw std::runtime_error("Temporal denoiser received an invalid RGBA frame");
    }
    if (stats) *stats = {};
    if (_settings.mode == DenoiseMode::Off || _settings.strength <= 0.0f) {
        Reset();
        return current;
    }

    const size_t n = static_cast<size_t>(current.width) * current.height;
    const bool historyValid = !resetHistory &&
        _history.width == current.width && _history.height == current.height &&
        _history.pixels.size() == current.pixels.size() && _historyAge.size() == n;
    const bool flowValid = historyValid && flow && !flow->sceneCut &&
        flow->motionXY.size() == n * 2u && flow->confidence.size() == n;

    Rgba8Image out = current;
    std::vector<uint8_t> newAge(n, 1u);

    struct LocalStats {
        double historyWeightSum = 0.0;
        double spatialWeightSum = 0.0;
        double ageSum = 0.0;
        uint64_t rejected = 0;
    };
    const unsigned workerCount = perf::WorkerCountForRows(current.height, 40u);
    std::vector<LocalStats> workerStats(workerCount);

    const float spatialBase = (_settings.mode == DenoiseMode::FullHq) ? _settings.spatialStrength : 0.0f;

    perf::ParallelForRows(current.height, 40u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned workerIndex) {
        LocalStats& localStats = workerStats[workerIndex];
        for (uint32_t y = rowBegin; y < rowEnd; ++y) {
            for (uint32_t x = 0; x < current.width; ++x) {
                const size_t i = static_cast<size_t>(y) * current.width + x;
                const auto cur = PixelYCoCg(current, x, y);

                // One 3x3 neighborhood powers both history clipping and the spatial fallback.
                YCoCg localMin{ 10.0f, 10.0f, 10.0f };
                YCoCg localMax{-10.0f,-10.0f,-10.0f };
                float spatialR = 0.0f, spatialG = 0.0f, spatialB = 0.0f, spatialW = 0.0f;
                float localLumaSum = 0.0f, localLumaSq = 0.0f;
                int localCount = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    const uint32_t sy = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(y) + oy, 0, static_cast<int>(current.height) - 1));
                    for (int ox = -1; ox <= 1; ++ox) {
                        const uint32_t sx = static_cast<uint32_t>(std::clamp<int>(static_cast<int>(x) + ox, 0, static_cast<int>(current.width) - 1));
                        const auto nc = PixelYCoCg(current, sx, sy);
                        localMin.y  = std::min(localMin.y, nc.y);   localMax.y  = std::max(localMax.y, nc.y);
                        localMin.co = std::min(localMin.co, nc.co); localMax.co = std::max(localMax.co, nc.co);
                        localMin.cg = std::min(localMin.cg, nc.cg); localMax.cg = std::max(localMax.cg, nc.cg);
                        localLumaSum += nc.y;
                        localLumaSq += nc.y * nc.y;
                        ++localCount;

                        if (spatialBase > 0.0f) {
                            const auto* p = &current.pixels[(static_cast<size_t>(sy) * current.width + sx) * 4u];
                            const float spatialKernel = (ox == 0 && oy == 0) ? 1.0f : ((ox == 0 || oy == 0) ? 0.72f : 0.52f);
                            const float lumaDistance = std::abs(nc.y - cur.y);
                            const float rangeSigma = 0.018f + 0.085f * _settings.strength;
                            const float rangeWeight = std::exp(-lumaDistance / std::max(0.004f, rangeSigma));
                            const float w = spatialKernel * rangeWeight;
                            spatialR += static_cast<float>(p[0]) * w;
                            spatialG += static_cast<float>(p[1]) * w;
                            spatialB += static_cast<float>(p[2]) * w;
                            spatialW += w;
                        }
                    }
                }

                const float mean = localLumaSum / static_cast<float>(std::max(1, localCount));
                const float variance = std::max(0.0f, localLumaSq / static_cast<float>(std::max(1, localCount)) - mean * mean);
                const float sigma = std::sqrt(variance);
                const float localRange = localMax.y - localMin.y;
                // Strong edges/details should not be spatially blurred. Flat noisy surfaces get more fallback filtering.
                const float edge = SmoothStep01((localRange - 0.025f) / 0.16f);
                const float flatness = 1.0f - edge * _settings.detailProtection;

                auto* dst = &out.pixels[i * 4u];
                const auto* src = &current.pixels[i * 4u];
                float baseR = static_cast<float>(src[0]) / 255.0f;
                float baseG = static_cast<float>(src[1]) / 255.0f;
                float baseB = static_cast<float>(src[2]) / 255.0f;
                float appliedSpatial = 0.0f;

                // Full HQ uses a conservative edge-aware spatial fallback, especially where history cannot be trusted.
                if (spatialBase > 0.0f && spatialW > 1e-5f) {
                    const float sr = (spatialR / spatialW) / 255.0f;
                    const float sg = (spatialG / spatialW) / 255.0f;
                    const float sb = (spatialB / spatialW) / 255.0f;
                    appliedSpatial = spatialBase * _settings.strength * flatness;
                    baseR += (sr - baseR) * appliedSpatial;
                    baseG += (sg - baseG) * appliedSpatial;
                    baseB += (sb - baseB) * appliedSpatial;
                }

                float histWeight = 0.0f;
                uint8_t prevAge = 0;
                if (flowValid) {
                    const float confidence = std::clamp(flow->confidence[i], 0.0f, 1.0f);
                    const float px = static_cast<float>(x) + flow->motionXY[i * 2u + 0u];
                    const float py = static_cast<float>(y) + flow->motionXY[i * 2u + 1u];
                    float histRgba[4]{};
                    BilinearRgba01(_history, px, py, histRgba);
                    prevAge = SampleAgeNearest(_historyAge, current.width, current.height, px, py);
                    if (confidence >= 0.08f && prevAge > 0) {
                        auto hist = ToYCoCg(histRgba[0], histRgba[1], histRgba[2]);

                        // Variance clip / neighborhood AABB. The expansion grows slightly with measured local noise.
                        const float expandY = _settings.clampExpansion + sigma * 0.70f;
                        const float expandC = _settings.clampExpansion * 1.35f + sigma * 0.45f;
                        hist.y  = std::clamp(hist.y,  localMin.y  - expandY, localMax.y  + expandY);
                        hist.co = std::clamp(hist.co, localMin.co - expandC, localMax.co + expandC);
                        hist.cg = std::clamp(hist.cg, localMin.cg - expandC, localMax.cg + expandC);

                        const float lumaDiff = std::abs(hist.y - cur.y);
                        const float chromaDiff = std::max(std::abs(hist.co - cur.co), std::abs(hist.cg - cur.cg));
                        const float lumaAccept = std::exp(-std::pow(lumaDiff / _settings.lumaReject, 2.0f));
                        const float chromaAccept = std::exp(-std::pow(chromaDiff / _settings.chromaReject, 2.0f));
                        const float motion = std::hypot(flow->motionXY[i * 2u + 0u], flow->motionXY[i * 2u + 1u]);
                        // Optical flow is already compensating motion; only gently reduce accumulation at very large velocities.
                        const float motionAccept = 0.72f + 0.28f * std::exp(-motion / 36.0f);
                        const float ageFactor = std::clamp(static_cast<float>(prevAge) / 6.0f, 0.20f, 1.0f);
                        const float detailFactor = 1.0f - 0.36f * edge * _settings.detailProtection;
                        histWeight = _settings.historyWeight * _settings.strength * confidence *
                                     lumaAccept * chromaAccept * motionAccept * ageFactor * detailFactor;
                        histWeight = std::clamp(histWeight, 0.0f, 0.965f);

                        if (histWeight > 0.01f) {
                            float hr = 0.0f, hg = 0.0f, hb = 0.0f;
                            FromYCoCg(hist, hr, hg, hb);
                            baseR += (std::clamp(hr, 0.0f, 1.0f) - baseR) * histWeight;
                            baseG += (std::clamp(hg, 0.0f, 1.0f) - baseG) * histWeight;
                            baseB += (std::clamp(hb, 0.0f, 1.0f) - baseB) * histWeight;
                            newAge[i] = static_cast<uint8_t>(std::min<uint32_t>(_settings.maxHistory, static_cast<uint32_t>(prevAge) + 1u));
                        } else {
                            ++localStats.rejected;
                        }
                    } else {
                        ++localStats.rejected;
                    }
                }

                // Where temporal accumulation is strong, back off the spatial pass so microdetail does not become plasticky.
                if (appliedSpatial > 0.0f && histWeight > 0.0f) {
                    const float correction = appliedSpatial * histWeight * 0.55f;
                    const float cr = static_cast<float>(src[0]) / 255.0f;
                    const float cg = static_cast<float>(src[1]) / 255.0f;
                    const float cb = static_cast<float>(src[2]) / 255.0f;
                    baseR += (cr - baseR) * correction;
                    baseG += (cg - baseG) * correction;
                    baseB += (cb - baseB) * correction;
                }

                dst[0] = static_cast<uint8_t>(std::clamp(std::lround(baseR * 255.0f), 0l, 255l));
                dst[1] = static_cast<uint8_t>(std::clamp(std::lround(baseG * 255.0f), 0l, 255l));
                dst[2] = static_cast<uint8_t>(std::clamp(std::lround(baseB * 255.0f), 0l, 255l));
                dst[3] = src[3];

                localStats.historyWeightSum += histWeight;
                localStats.spatialWeightSum += appliedSpatial * (1.0f - histWeight);
                localStats.ageSum += newAge[i];
            }
        }
    });

    double historyWeightSum = 0.0;
    double spatialWeightSum = 0.0;
    double ageSum = 0.0;
    uint64_t rejected = 0;
    for (const auto& local : workerStats) {
        historyWeightSum += local.historyWeightSum;
        spatialWeightSum += local.spatialWeightSum;
        ageSum += local.ageSum;
        rejected += local.rejected;
    }

    _history = out;
    _historyAge = std::move(newAge);

    if (stats && n) {
        const double invN = 1.0 / static_cast<double>(n);
        stats->averageHistoryWeight = static_cast<float>(historyWeightSum * invN);
        stats->averageSpatialWeight = static_cast<float>(spatialWeightSum * invN);
        stats->rejectedFraction = static_cast<float>(static_cast<double>(rejected) * invN);
        stats->averageHistoryAge = static_cast<float>(ageSum * invN);
    }
    return out;
}

} // namespace video
