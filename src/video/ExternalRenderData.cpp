#include "ExternalRenderData.h"
#include "ImageImport.h"
#include "ParallelRows.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

bool EndsWithInsensitive(const std::string& value, const std::string& suffix) {
    if (suffix.size() > value.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(value[value.size() - suffix.size() + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

bool ContainsInsensitive(const std::string& value, const std::string& needle) {
    std::string a = value, b = needle;
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return a.find(b) != std::string::npos;
}

float PixelLuma(const uint8_t* p) {
    return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

float BilinearLuma(const Rgba8Image& image, float x, float y, bool& inside) {
    inside = image.width && image.height && !image.pixels.empty() &&
             x >= 0.0f && y >= 0.0f && x <= static_cast<float>(image.width - 1u) &&
             y <= static_cast<float>(image.height - 1u);
    if (!inside) return 0.0f;
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(image.width - 1u, x0 + 1u);
    const uint32_t y1 = std::min(image.height - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float a = PixelLuma(&image.pixels[(static_cast<size_t>(y0) * image.width + x0) * 4u]);
    const float b = PixelLuma(&image.pixels[(static_cast<size_t>(y0) * image.width + x1) * 4u]);
    const float c = PixelLuma(&image.pixels[(static_cast<size_t>(y1) * image.width + x0) * 4u]);
    const float d = PixelLuma(&image.pixels[(static_cast<size_t>(y1) * image.width + x1) * 4u]);
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

float BilinearField(const std::vector<float>& field, uint32_t width, uint32_t height, float x, float y, bool& inside) {
    inside = width && height && field.size() == static_cast<size_t>(width) * height &&
             x >= 0.0f && y >= 0.0f && x <= static_cast<float>(width - 1u) &&
             y <= static_cast<float>(height - 1u);
    if (!inside) return 0.0f;
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(width - 1u, x0 + 1u);
    const uint32_t y1 = std::min(height - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float a = field[static_cast<size_t>(y0) * width + x0];
    const float b = field[static_cast<size_t>(y0) * width + x1];
    const float c = field[static_cast<size_t>(y1) * width + x0];
    const float d = field[static_cast<size_t>(y1) * width + x1];
    const float top = a + (b - a) * tx;
    const float bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

float SceneCutScore(const Rgba8Image& previous, const Rgba8Image& current, uint32_t analysisWidth) {
    if (previous.width != current.width || previous.height != current.height || !current.width || !current.height) return 1.0f;
    analysisWidth = std::clamp(analysisWidth, 64u, current.width);
    const uint32_t analysisHeight = std::max(1u, static_cast<uint32_t>(std::lround(
        static_cast<double>(current.height) * analysisWidth / current.width)));
    constexpr int bins = 32;
    double mad = 0.0;
    uint64_t ha[bins]{};
    uint64_t hb[bins]{};
    const size_t n = static_cast<size_t>(analysisWidth) * analysisHeight;
    for (uint32_t y = 0; y < analysisHeight; ++y) {
        const uint32_t sy = std::min(current.height - 1u,
            static_cast<uint32_t>((static_cast<uint64_t>(y) * current.height) / analysisHeight));
        for (uint32_t x = 0; x < analysisWidth; ++x) {
            const uint32_t sx = std::min(current.width - 1u,
                static_cast<uint32_t>((static_cast<uint64_t>(x) * current.width) / analysisWidth));
            const auto* a = &previous.pixels[(static_cast<size_t>(sy) * current.width + sx) * 4u];
            const auto* b = &current.pixels[(static_cast<size_t>(sy) * current.width + sx) * 4u];
            const int ya = std::clamp(static_cast<int>(std::lround(PixelLuma(a))), 0, 255);
            const int yb = std::clamp(static_cast<int>(std::lround(PixelLuma(b))), 0, 255);
            ++ha[std::min(bins - 1, ya * bins / 256)];
            ++hb[std::min(bins - 1, yb * bins / 256)];
            mad += std::abs(ya - yb);
        }
    }
    if (!n) return 1.0f;
    double hist = 0.0;
    for (int i = 0; i < bins; ++i) hist += std::abs(static_cast<double>(ha[i]) - static_cast<double>(hb[i]));
    hist = 0.5 * hist / static_cast<double>(n);
    const double madNorm = mad / static_cast<double>(n) / 255.0;
    return static_cast<float>(std::clamp(hist * 0.65 + madNorm * 0.35, 0.0, 1.0));
}

void RequireDimensions(const FloatChannelMap& c, uint32_t width, uint32_t height, const char* what) {
    if (c.width != width || c.height != height || c.values.size() != static_cast<size_t>(width) * height) {
        throw std::runtime_error(std::string(what) + " EXR dimensions do not match the source video");
    }
}

} // namespace

namespace video {

ExternalDataProbe ProbeExternalExr(const std::filesystem::path& firstFrame) {
    ExternalDataProbe out;
    out.channels = ListExrChannels(firstFrame);
    out.sequenceDescription = ParseExrSequencePattern(firstFrame).Description();
    return out;
}

std::string AutoSelectDepthChannel(const std::vector<std::string>& channels) {
    for (const auto& c : channels) if (EndsWithInsensitive(c, ".Depth.Z")) return c;
    for (const auto& c : channels) if (EndsWithInsensitive(c, ".Z") && ContainsInsensitive(c, "depth")) return c;
    for (const auto& c : channels) if (ContainsInsensitive(c, "depth")) return c;
    for (const auto& c : channels) if (c == "Z" || EndsWithInsensitive(c, ".Z")) return c;
    // Generic RGBA depth EXRs are common in Unreal/XRFeitoria datasets. Never
    // choose Alpha merely because TinyEXR enumerates A first.
    for (const auto& leaf : {"R", "G", "B"}) {
        for (const auto& c : channels) if (c == leaf || EndsWithInsensitive(c, std::string(".") + leaf)) return c;
    }
    for (const auto& c : channels) if (!(c == "A" || EndsWithInsensitive(c, ".A"))) return c;
    return channels.empty() ? std::string{} : channels.front();
}

std::string AutoSelectMotionXChannel(const std::vector<std::string>& channels) {
    for (const auto& c : channels) if (EndsWithInsensitive(c, ".Vector.X")) return c;
    for (const auto& c : channels) if (ContainsInsensitive(c, "vector") && EndsWithInsensitive(c, ".X")) return c;
    for (const auto& c : channels) if ((ContainsInsensitive(c, "vector") || ContainsInsensitive(c, "flow") || ContainsInsensitive(c, "motion")) && EndsWithInsensitive(c, ".R")) return c;
    for (const auto& c : channels) if (c == "X" || EndsWithInsensitive(c, ".X")) return c;
    for (const auto& c : channels) if (c == "R" || EndsWithInsensitive(c, ".R")) return c;
    return {};
}

std::string AutoSelectMotionYChannel(const std::vector<std::string>& channels) {
    for (const auto& c : channels) if (EndsWithInsensitive(c, ".Vector.Y")) return c;
    for (const auto& c : channels) if (ContainsInsensitive(c, "vector") && EndsWithInsensitive(c, ".Y")) return c;
    for (const auto& c : channels) if ((ContainsInsensitive(c, "vector") || ContainsInsensitive(c, "flow") || ContainsInsensitive(c, "motion")) && EndsWithInsensitive(c, ".G")) return c;
    for (const auto& c : channels) if (c == "Y" || EndsWithInsensitive(c, ".Y")) return c;
    for (const auto& c : channels) if (c == "G" || EndsWithInsensitive(c, ".G")) return c;
    return {};
}

ExternalRenderDataReader::ExternalRenderDataReader(ExternalRenderDataSettings settings,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   uint64_t expectedFrames,
                                                   bool needDepth,
                                                   bool needMotion)
    : _settings(std::move(settings)), _width(width), _height(height), _expectedFrames(expectedFrames),
      _needDepth(needDepth), _needMotion(needMotion) {
    if (_needDepth) _depthPattern = ParseExrSequencePattern(_settings.depth.firstFrame);
    if (_needMotion) _motionPattern = ParseExrSequencePattern(_settings.motion.firstFrame);
}

std::filesystem::path ExternalRenderDataReader::RequireDepthPath(uint64_t oneBasedVideoFrame) const {
    if (!oneBasedVideoFrame) throw std::runtime_error("External depth frame index must be one-based");
    const auto p = _depthPattern.FramePath(oneBasedVideoFrame - 1u);
    if (!std::filesystem::exists(p)) throw std::runtime_error("Missing external depth frame: " + p.string());
    return p;
}

std::filesystem::path ExternalRenderDataReader::RequireMotionPath(uint64_t oneBasedVideoFrame) const {
    if (!oneBasedVideoFrame) throw std::runtime_error("External motion frame index must be one-based");
    const auto p = _motionPattern.FramePath(oneBasedVideoFrame - 1u);
    if (!std::filesystem::exists(p)) throw std::runtime_error("Missing external motion frame: " + p.string());
    return p;
}

void ExternalRenderDataReader::Validate() const {
    if (!_width || !_height) throw std::runtime_error("External render data requires a valid source-video resolution");
    if (_needDepth) {
        if (_settings.depth.channel.empty()) throw std::runtime_error("Select an external depth EXR channel first");
        const auto first = RequireDepthPath(1);
        const auto c = LoadExrFloatChannel(first, _settings.depth.channel);
        RequireDimensions(c, _width, _height, "External depth");
        if (_settings.depth.mapping == ExternalDepthMapping::FixedRange &&
            (!std::isfinite(_settings.depth.nearValue) || !std::isfinite(_settings.depth.farValue) ||
             _settings.depth.farValue <= _settings.depth.nearValue)) {
            throw std::runtime_error("External depth Fixed Range requires Far > Near");
        }
    }
    if (_needMotion) {
        if (_settings.motion.xChannel.empty() || _settings.motion.yChannel.empty()) {
            throw std::runtime_error("Select both external motion X and Y EXR channels first");
        }
        const auto first = RequireMotionPath(1);
        const auto x = LoadExrFloatChannel(first, _settings.motion.xChannel);
        const auto y = LoadExrFloatChannel(first, _settings.motion.yChannel);
        RequireDimensions(x, _width, _height, "External motion X");
        RequireDimensions(y, _width, _height, "External motion Y");
    }
}

std::vector<float> ExternalRenderDataReader::LoadDepth(uint64_t oneBasedVideoFrame) const {
    const auto p = RequireDepthPath(oneBasedVideoFrame);
    auto c = LoadExrFloatChannel(p, _settings.depth.channel);
    RequireDimensions(c, _width, _height, "External depth");
    if (_settings.depth.mapping == ExternalDepthMapping::Raw01) {
        for (float& v : c.values) v = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
        return std::move(c.values);
    }

    const float nearV = _settings.depth.nearValue;
    const float farV = _settings.depth.farValue;
    const float invRange = 1.0f / (farV - nearV);
    for (float& v : c.values) {
        float t = std::isfinite(v) ? (v - nearV) * invRange : 1.0f;
        t = std::clamp(t, 0.0f, 1.0f);
        v = _settings.depth.depthInverted ? (1.0f - t) : t;
    }
    return std::move(c.values);
}

TemporalFlowResult ExternalRenderDataReader::LoadMotion(uint64_t oneBasedVideoFrame,
                                                        const Rgba8Image* previous,
                                                        const Rgba8Image& current,
                                                        uint32_t sceneAnalysisWidth,
                                                        float sceneCutThreshold) const {
    TemporalFlowResult result;
    const size_t pixels = static_cast<size_t>(_width) * _height;
    result.motionXY.assign(pixels * 2u, 0.0f);
    result.confidence.assign(pixels, 0.0f);
    if (!previous) {
        result.sceneCut = true;
        result.sceneCutScore = 1.0f;
        return result;
    }

    result.sceneCutScore = SceneCutScore(*previous, current, sceneAnalysisWidth);
    result.sceneCut = result.sceneCutScore >= std::clamp(sceneCutThreshold, 0.05f, 0.95f);

    std::filesystem::path p;
    if (_settings.motion.direction == ExternalMotionDirection::CurrentToPrevious) {
        p = RequireMotionPath(oneBasedVideoFrame);
    } else {
        // A forward field stored for frame N-1 maps previous -> current. For
        // current video frame N we therefore consume the preceding EXR frame.
        if (oneBasedVideoFrame < 2u) return result;
        p = _motionPattern.FramePath(oneBasedVideoFrame - 2u);
        if (!std::filesystem::exists(p)) throw std::runtime_error("Missing external forward-motion frame: " + p.string());
    }
    auto x = LoadExrFloatChannel(p, _settings.motion.xChannel);
    auto y = LoadExrFloatChannel(p, _settings.motion.yChannel);
    RequireDimensions(x, _width, _height, "External motion X");
    RequireDimensions(y, _width, _height, "External motion Y");
    if (result.sceneCut) return result;

    const float sx = _settings.motion.importScaleX * (_settings.motion.flipX ? -1.0f : 1.0f);
    const float sy = _settings.motion.importScaleY * (_settings.motion.flipY ? -1.0f : 1.0f);
    perf::ParallelForRows(_height, 32u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned) {
        for (uint32_t yy = rowBegin; yy < rowEnd; ++yy) {
            for (uint32_t xx = 0; xx < _width; ++xx) {
                const size_t i = static_cast<size_t>(yy) * _width + xx;
                float mx = 0.0f, my = 0.0f;
                if (_settings.motion.direction == ExternalMotionDirection::CurrentToPrevious) {
                    mx = x.values[i] * sx;
                    my = y.values[i] * sy;
                } else {
                    // Numerically invert previous->current flow without scatter holes.
                    // Solve p + F(p) = q for previous position p using a few fixed-
                    // point iterations, then output p-q (current->previous).
                    float px = static_cast<float>(xx);
                    float py = static_cast<float>(yy);
                    bool valid = true;
                    for (int it = 0; it < 4; ++it) {
                        bool ix = false, iy = false;
                        const float fx = BilinearField(x.values, _width, _height, px, py, ix) * sx;
                        const float fy = BilinearField(y.values, _width, _height, px, py, iy) * sy;
                        if (!ix || !iy || !std::isfinite(fx) || !std::isfinite(fy)) { valid = false; break; }
                        px = static_cast<float>(xx) - fx;
                        py = static_cast<float>(yy) - fy;
                    }
                    if (valid) { mx = px - static_cast<float>(xx); my = py - static_cast<float>(yy); }
                    else { mx = my = std::numeric_limits<float>::quiet_NaN(); }
                }
                if (!std::isfinite(mx) || !std::isfinite(my)) {
                    mx = my = 0.0f;
                    result.confidence[i] = 0.0f;
                } else {
                    bool inside = false;
                    const float prevLuma = BilinearLuma(*previous, static_cast<float>(xx) + mx,
                                                        static_cast<float>(yy) + my, inside);
                    if (inside) {
                        const float currLuma = PixelLuma(&current.pixels[i * 4u]);
                        // Ground-truth render vectors are trusted spatially, while a light
                        // photometric gate rejects disocclusion boundaries and badly mapped channels.
                        const float residual = std::abs(currLuma - prevLuma) / 255.0f;
                        result.confidence[i] = std::clamp(std::exp(-residual * 5.0f), 0.0f, 1.0f);
                    }
                }
                result.motionXY[i * 2u + 0u] = mx;
                result.motionXY[i * 2u + 1u] = my;
            }
        }
    });
    return result;
}

} // namespace video
