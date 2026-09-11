#include "NvofFlowPostprocess.h"
#include "ParallelRows.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

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
    if (in.forwardCost && in.forwardCost->size() != gridN) throw std::runtime_error("NVOF cost grid size mismatch");

    TemporalFlowResult result;
    result.sceneCutScore = in.sceneCutScore;
    result.sceneCut = in.sceneCut;
    const size_t fullN = static_cast<size_t>(in.sourceWidth) * in.sourceHeight;
    result.motionXY.assign(fullN * 2u, 0.0f);
    result.confidence.assign(fullN, 0.0f);
    if (in.sceneCut) return result;

    std::vector<float> fx(gridN), fy(gridN), bx, by, cost;
    if (in.backward) { bx.resize(gridN); by.resize(gridN); }
    if (in.forwardCost) cost.resize(gridN);
    for (size_t i = 0; i < gridN; ++i) {
        fx[i] = DecodeNvofFixed11_5((*in.forward)[i].x);
        fy[i] = DecodeNvofFixed11_5((*in.forward)[i].y);
        if (in.backward) {
            bx[i] = DecodeNvofFixed11_5((*in.backward)[i].x);
            by[i] = DecodeNvofFixed11_5((*in.backward)[i].y);
        }
        if (in.forwardCost) cost[i] = static_cast<float>((*in.forwardCost)[i]) / 255.0f;
    }

    // NVOFA flow values are expressed in input-image pixel units even when the output is
    // a coarser vector grid. Upsampling therefore interpolates vectors but does NOT multiply
    // their magnitude by the grid spacing (unlike our low-resolution DIS helper).
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

                float confCost = 0.80f; // neutral fallback if cost output is disabled
                if (!cost.empty()) confCost = std::clamp(1.0f - Bilinear(cost, in.gridWidth, in.gridHeight, gx, gy), 0.0f, 1.0f);

                float confFb = 0.80f;
                if (!bx.empty()) {
                    // F maps current q -> previous p. B is previous -> current, so a correct
                    // pair satisfies F(q) + B(p) ~= 0 where p=q+F(q).
                    const float px = static_cast<float>(x) + mvx;
                    const float py = static_cast<float>(y) + mvy;
                    if (px >= 0.0f && py >= 0.0f && px <= static_cast<float>(in.sourceWidth - 1u) && py <= static_cast<float>(in.sourceHeight - 1u)) {
                        const float bgx = (px + 0.5f) * gridPerPixelX - 0.5f;
                        const float bgy = (py + 0.5f) * gridPerPixelY - 0.5f;
                        const float bmvx = Bilinear(bx, in.gridWidth, in.gridHeight, bgx, bgy);
                        const float bmvy = Bilinear(by, in.gridWidth, in.gridHeight, bgx, bgy);
                        const float fbErr = std::hypot(mvx + bmvx, mvy + bmvy);
                        confFb = std::exp(-fbErr / 1.5f);
                    } else {
                        confFb = 0.0f;
                    }
                }

                float confPhoto = 0.80f;
                if (photo) {
                    bool valid = false;
                    const float prevLuma = LumaAt(*in.previousFrame, static_cast<float>(x) + mvx, static_cast<float>(y) + mvy, valid);
                    if (valid) {
                        const float residual = std::abs(CurrentLuma(*in.currentFrame, x, y) - prevLuma);
                        confPhoto = std::exp(-residual / 0.08f);
                    } else {
                        confPhoto = 0.0f;
                    }
                }

                // Cost alone is not a universal confidence measure; combine all available
                // evidence and let strong FB/photo disagreement invalidate dubious motion.
                result.confidence[i] = std::clamp(0.25f * confCost + 0.45f * confFb + 0.30f * confPhoto, 0.0f, 1.0f);
            }
        }
    });
    return result;
}

} // namespace video
