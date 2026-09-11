#include "MotionPreview.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace video {
namespace {
constexpr float kPi = 3.14159265358979323846f;

void HsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    h -= std::floor(h);
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    const float hh = h * 6.0f;
    const int sector = static_cast<int>(std::floor(hh)) % 6;
    const float f = hh - std::floor(hh);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * f);
    const float t = v * (1.0f - s * (1.0f - f));
    float rf = 0.0f, gf = 0.0f, bf = 0.0f;
    switch (sector) {
    case 0: rf = v; gf = t; bf = p; break;
    case 1: rf = q; gf = v; bf = p; break;
    case 2: rf = p; gf = v; bf = t; break;
    case 3: rf = p; gf = q; bf = v; break;
    case 4: rf = t; gf = p; bf = v; break;
    default: rf = v; gf = p; bf = q; break;
    }
    r = static_cast<uint8_t>(std::lround(std::clamp(rf, 0.0f, 1.0f) * 255.0f));
    g = static_cast<uint8_t>(std::lround(std::clamp(gf, 0.0f, 1.0f) * 255.0f));
    b = static_cast<uint8_t>(std::lround(std::clamp(bf, 0.0f, 1.0f) * 255.0f));
}

float RobustMagnitude(const std::vector<float>& motionXY, float scaleX, float scaleY) {
    const size_t count = motionXY.size() / 2u;
    if (!count) return 0.0f;
    const size_t step = std::max<size_t>(1u, count / 32768u);
    std::vector<float> samples;
    samples.reserve(count / step + 1u);
    for (size_t i = 0; i < count; i += step) {
        const float x = motionXY[i * 2u + 0u] * scaleX;
        const float y = motionXY[i * 2u + 1u] * scaleY;
        if (!std::isfinite(x) || !std::isfinite(y)) continue;
        const float m = std::sqrt(x * x + y * y);
        if (std::isfinite(m)) samples.push_back(m);
    }
    if (samples.empty()) return 0.0f;
    const size_t index = std::min(samples.size() - 1u,
                                  static_cast<size_t>(std::floor((samples.size() - 1u) * 0.95)));
    std::nth_element(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(index), samples.end());
    return samples[index];
}
}

MotionPreviewResult BuildMotionPreview(const std::vector<float>& motionXY,
                                       uint32_t width,
                                       uint32_t height,
                                       float scaleX,
                                       float scaleY,
                                       uint32_t maxPreviewWidth) {
    MotionPreviewResult result;
    if (!width || !height || motionXY.size() < static_cast<size_t>(width) * height * 2u) return result;

    result.robustMagnitudePixels = RobustMagnitude(motionXY, scaleX, scaleY);
    const float displayScale = std::max(0.25f, result.robustMagnitudePixels);

    const uint32_t dstW = std::max<uint32_t>(1u, std::min(width, std::max<uint32_t>(1u, maxPreviewWidth)));
    const uint32_t dstH = std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(
        static_cast<double>(height) * static_cast<double>(dstW) / static_cast<double>(width))));
    result.image.width = dstW;
    result.image.height = dstH;
    result.image.pixels.resize(static_cast<size_t>(dstW) * dstH * 4u);

    for (uint32_t y = 0; y < dstH; ++y) {
        const uint32_t sy = std::min(height - 1u, static_cast<uint32_t>((static_cast<uint64_t>(y) * height) / dstH));
        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t sx = std::min(width - 1u, static_cast<uint32_t>((static_cast<uint64_t>(x) * width) / dstW));
            const size_t src = (static_cast<size_t>(sy) * width + sx) * 2u;
            float vx = motionXY[src + 0u] * scaleX;
            float vy = motionXY[src + 1u] * scaleY;
            if (!std::isfinite(vx) || !std::isfinite(vy)) { vx = 0.0f; vy = 0.0f; }
            const float magnitude = std::sqrt(vx * vx + vy * vy);
            const float normalized = std::clamp(magnitude / displayScale, 0.0f, 1.0f);
            // Brightness is sqrt-compressed so slow but valid motion remains visible while
            // P95 prevents a few extreme vectors from making the whole preview nearly black.
            const float value = std::sqrt(normalized);
            const float hue = (std::atan2(vy, vx) + kPi) / (2.0f * kPi);
            uint8_t r = 0, g = 0, b = 0;
            if (value > 0.005f) HsvToRgb(hue, 1.0f, value, r, g, b);
            const size_t dst = (static_cast<size_t>(y) * dstW + x) * 4u;
            result.image.pixels[dst + 0u] = r;
            result.image.pixels[dst + 1u] = g;
            result.image.pixels[dst + 2u] = b;
            result.image.pixels[dst + 3u] = 255u;
        }
    }
    return result;
}

} // namespace video
