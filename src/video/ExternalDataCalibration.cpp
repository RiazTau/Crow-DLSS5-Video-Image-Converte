#include "ExternalDataCalibration.h"
#include "ExternalSequence.h"
#include "ImageImport.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace {

using video::ExternalMotionDirection;
using video::MotionCalibrationFramePair;

std::string LowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool ContainsI(const std::string& value, const std::string& needle) {
    return LowerAscii(value).find(LowerAscii(needle)) != std::string::npos;
}

std::string ChannelLeaf(const std::string& value) {
    const auto p = value.find_last_of('.');
    return p == std::string::npos ? value : value.substr(p + 1);
}

bool LeafEq(const std::string& c, const char* leaf) {
    return LowerAscii(ChannelLeaf(c)) == LowerAscii(leaf);
}

float PixelLuma(const uint8_t* p) {
    return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
}

float BilinearLuma(const Rgba8Image& image, float x, float y, bool& inside) {
    inside = image.width && image.height && image.pixels.size() >= static_cast<size_t>(image.width) * image.height * 4u &&
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

float PercentileSorted(const std::vector<float>& sorted, double q) {
    if (sorted.empty()) return 0.0f;
    q = std::clamp(q, 0.0, 1.0);
    const double pos = q * static_cast<double>(sorted.size() - 1u);
    const size_t i0 = static_cast<size_t>(std::floor(pos));
    const size_t i1 = std::min(sorted.size() - 1u, i0 + 1u);
    const float t = static_cast<float>(pos - static_cast<double>(i0));
    return sorted[i0] + (sorted[i1] - sorted[i0]) * t;
}

struct ChannelStats {
    std::string channel;
    size_t finite = 0;
    size_t total = 0;
    float p001 = 0.0f;
    float p01 = 0.0f;
    float p10 = 0.0f;
    float p50 = 0.0f;
    float p90 = 0.0f;
    float p99 = 0.0f;
    float p999 = 0.0f;
    float robustSpan = 0.0f;
    float finiteRatio = 0.0f;
    float positiveRatio = 0.0f;
    float in01Ratio = 0.0f;
    float score = -1000.0f;
};

std::vector<std::filesystem::path> FindSequenceFrames(const std::filesystem::path& firstFrame) {
    const auto pattern = video::ParseExrSequencePattern(firstFrame);
    std::vector<std::pair<uint64_t, std::filesystem::path>> numbered;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(pattern.directory, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        const auto p = e.path();
        if (LowerAscii(p.extension().string()) != ".exr") continue;
        const std::wstring stem = p.stem().wstring();
        if (stem.size() < pattern.prefix.size() + pattern.padding) continue;
        if (stem.compare(0, pattern.prefix.size(), pattern.prefix) != 0) continue;
        const std::wstring digits = stem.substr(pattern.prefix.size());
        if (digits.size() < pattern.padding ||
            !std::all_of(digits.begin(), digits.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; })) continue;
        try {
            const uint64_t n = std::stoull(digits);
            if (n >= pattern.firstNumber) numbered.emplace_back(n, p);
        } catch (...) {}
    }
    std::sort(numbered.begin(), numbered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::filesystem::path> out;
    out.reserve(numbered.size());
    for (auto& x : numbered) out.push_back(std::move(x.second));
    if (out.empty() && std::filesystem::exists(firstFrame)) out.push_back(firstFrame);
    return out;
}

std::vector<std::filesystem::path> EvenSamplePaths(const std::vector<std::filesystem::path>& all, size_t maxSamples) {
    if (all.size() <= maxSamples || maxSamples <= 1u) return all;
    std::vector<std::filesystem::path> out;
    out.reserve(maxSamples);
    for (size_t i = 0; i < maxSamples; ++i) {
        const size_t index = static_cast<size_t>(std::llround(
            static_cast<double>(i) * static_cast<double>(all.size() - 1u) /
            static_cast<double>(maxSamples - 1u)));
        if (out.empty() || out.back() != all[index]) out.push_back(all[index]);
    }
    return out;
}

std::vector<float> SparseFiniteValues(const FloatChannelMap& c, size_t maxValues) {
    std::vector<float> out;
    if (c.values.empty() || !maxValues) return out;
    const size_t stride = std::max<size_t>(1u, c.values.size() / maxValues);
    out.reserve(std::min(maxValues, c.values.size()));
    for (size_t i = 0; i < c.values.size(); i += stride) {
        const float v = c.values[i];
        if (std::isfinite(v)) out.push_back(v);
    }
    return out;
}

float DepthNamePrior(const std::string& channel) {
    const std::string leaf = LowerAscii(ChannelLeaf(channel));
    float score = 0.0f;
    if (ContainsI(channel, "depth")) score += 5.0f;
    if (leaf == "z") score += 5.0f;
    if (leaf == "r") score += 1.8f;
    else if (leaf == "g") score += 1.4f;
    else if (leaf == "b") score += 1.0f;
    if (leaf == "a" || leaf == "alpha") score -= 8.0f;
    if (ContainsI(channel, "vector") || ContainsI(channel, "flow") || ContainsI(channel, "motion")) score -= 4.0f;
    return score;
}

ChannelStats AnalyzeDepthChannel(const std::string& channel,
                                 const std::vector<std::filesystem::path>& frames,
                                 size_t maxValuesPerFrame) {
    ChannelStats s;
    s.channel = channel;
    std::vector<float> values;
    size_t totalSamples = 0, positive = 0, in01 = 0;
    uint32_t expectedWidth = 0, expectedHeight = 0;
    for (const auto& p : frames) {
        const auto map = LoadExrFloatChannel(p, channel);
        if (!expectedWidth) { expectedWidth = map.width; expectedHeight = map.height; }
        else if (map.width != expectedWidth || map.height != expectedHeight) {
            throw std::runtime_error("Depth EXR sequence changes resolution during auto-calibration");
        }
        const auto sparse = SparseFiniteValues(map, maxValuesPerFrame);
        totalSamples += std::min(maxValuesPerFrame, map.values.size());
        for (float v : sparse) {
            values.push_back(v);
            if (v >= 0.0f) ++positive;
            if (v >= -1e-5f && v <= 1.00001f) ++in01;
        }
    }
    s.finite = values.size();
    s.total = std::max(totalSamples, values.size());
    if (values.empty()) return s;
    std::sort(values.begin(), values.end());
    s.p001 = PercentileSorted(values, 0.001);
    s.p01 = PercentileSorted(values, 0.01);
    s.p10 = PercentileSorted(values, 0.10);
    s.p50 = PercentileSorted(values, 0.50);
    s.p90 = PercentileSorted(values, 0.90);
    s.p99 = PercentileSorted(values, 0.99);
    s.p999 = PercentileSorted(values, 0.999);
    s.robustSpan = s.p99 - s.p01;
    s.finiteRatio = static_cast<float>(values.size()) / static_cast<float>(std::max<size_t>(1u, s.total));
    s.positiveRatio = static_cast<float>(positive) / static_cast<float>(values.size());
    s.in01Ratio = static_cast<float>(in01) / static_cast<float>(values.size());

    const float magnitude = std::max({1.0f, std::abs(s.p01), std::abs(s.p99)});
    const float relativeSpan = s.robustSpan / magnitude;
    s.score = DepthNamePrior(channel);
    s.score += std::clamp(s.finiteRatio, 0.0f, 1.0f) * 2.0f;
    s.score += std::clamp(s.positiveRatio, 0.0f, 1.0f) * 1.5f;
    s.score += std::clamp(relativeSpan * 5.0f, 0.0f, 3.0f);
    if (s.robustSpan <= std::max(1e-7f, magnitude * 1e-6f)) s.score -= 12.0f;
    if (LeafEq(channel, "A") && s.in01Ratio > 0.99f) s.score -= 6.0f;
    return s;
}

std::vector<float> DownsampleChannel(const FloatChannelMap& c, uint32_t dw, uint32_t dh) {
    if (!dw || !dh || !c.width || !c.height) return {};
    std::vector<float> out(static_cast<size_t>(dw) * dh);
    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t sy = std::min(c.height - 1u,
            static_cast<uint32_t>((static_cast<uint64_t>(y) * c.height) / dh));
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t sx = std::min(c.width - 1u,
                static_cast<uint32_t>((static_cast<uint64_t>(x) * c.width) / dw));
            out[static_cast<size_t>(y) * dw + x] = c.values[static_cast<size_t>(sy) * c.width + sx];
        }
    }
    return out;
}

struct MotionFileGrid {
    uint32_t width = 0;
    uint32_t height = 0;
    std::unordered_map<std::string, std::vector<float>> channels;
};

struct MotionGridCache {
    uint32_t analysisWidth = 0;
    uint32_t analysisHeight = 0;
    std::map<std::filesystem::path, MotionFileGrid> files;

    const std::vector<float>& Get(const std::filesystem::path& p, const std::string& channel) {
        auto& f = files[p];
        auto it = f.channels.find(channel);
        if (it != f.channels.end()) return it->second;
        const auto map = LoadExrFloatChannel(p, channel);
        if (!f.width) { f.width = map.width; f.height = map.height; }
        if (map.width != f.width || map.height != f.height) throw std::runtime_error("Motion EXR channels have inconsistent dimensions");
        auto values = DownsampleChannel(map, analysisWidth, analysisHeight);
        return f.channels.emplace(channel, std::move(values)).first->second;
    }
};

float PairZeroMotionError(const MotionCalibrationFramePair& p, unsigned step = 6u) {
    if (p.previous.width != p.current.width || p.previous.height != p.current.height || !p.current.width) return 1.0f;
    double sum = 0.0;
    size_t count = 0;
    for (uint32_t y = step / 2u; y < p.current.height; y += step) {
        for (uint32_t x = step / 2u; x < p.current.width; x += step) {
            const size_t i = static_cast<size_t>(y) * p.current.width + x;
            const float a = PixelLuma(&p.previous.pixels[i * 4u]);
            const float b = PixelLuma(&p.current.pixels[i * 4u]);
            const float r = std::min(std::abs(a - b) / 255.0f, 0.35f) / 0.35f;
            sum += r;
            ++count;
        }
    }
    return count ? static_cast<float>(sum / static_cast<double>(count)) : 1.0f;
}

struct MotionCandidate {
    std::string x;
    std::string y;
    float sx = 1.0f;
    float sy = 1.0f;
    bool flipX = false;
    bool flipY = false;
    ExternalMotionDirection direction = ExternalMotionDirection::CurrentToPrevious;
    std::string unit;
    float error = 1.0f;
    float cost = 1.0f;
    float meanMotion = 0.0f;
};

std::filesystem::path MotionPathForPair(const video::ExrSequencePattern& pattern,
                                        uint64_t currentOneBasedFrame,
                                        ExternalMotionDirection direction) {
    if (!currentOneBasedFrame) return {};
    if (direction == ExternalMotionDirection::CurrentToPrevious) {
        return pattern.FramePath(currentOneBasedFrame - 1u);
    }
    if (currentOneBasedFrame < 2u) return {};
    return pattern.FramePath(currentOneBasedFrame - 2u);
}

float EvaluateMotionCandidate(MotionCandidate& c,
                              const video::ExrSequencePattern& pattern,
                              uint32_t sourceW,
                              uint32_t sourceH,
                              const std::vector<MotionCalibrationFramePair>& pairs,
                              MotionGridCache& cache) {
    double sum = 0.0;
    double motionSum = 0.0;
    size_t count = 0;
    constexpr unsigned step = 6u;
    for (const auto& pair : pairs) {
        if (!pair.current.width || pair.current.width != pair.previous.width ||
            pair.current.height != pair.previous.height) continue;
        const auto path = MotionPathForPair(pattern, pair.currentOneBasedFrame, c.direction);
        if (path.empty() || !std::filesystem::exists(path)) continue;
        const auto& rawX = cache.Get(path, c.x);
        const auto& rawY = cache.Get(path, c.y);
        const uint32_t aw = pair.current.width, ah = pair.current.height;
        if (rawX.size() != static_cast<size_t>(aw) * ah || rawY.size() != rawX.size()) continue;
        const float ax = static_cast<float>(aw) / static_cast<float>(sourceW);
        const float ay = static_cast<float>(ah) / static_cast<float>(sourceH);
        const float signX = c.flipX ? -1.0f : 1.0f;
        const float signY = c.flipY ? -1.0f : 1.0f;
        for (uint32_t y = step / 2u; y < ah; y += step) {
            for (uint32_t x = step / 2u; x < aw; x += step) {
                const size_t i = static_cast<size_t>(y) * aw + x;
                const float fx = rawX[i] * c.sx * signX;
                const float fy = rawY[i] * c.sy * signY;
                if (!std::isfinite(fx) || !std::isfinite(fy) ||
                    std::abs(fx) > static_cast<float>(sourceW) * 2.0f ||
                    std::abs(fy) > static_cast<float>(sourceH) * 2.0f) {
                    sum += 1.0;
                    ++count;
                    continue;
                }
                const float dx = fx * ax;
                const float dy = fy * ay;
                bool inside = false;
                float a = 0.0f, b = 0.0f;
                if (c.direction == ExternalMotionDirection::CurrentToPrevious) {
                    a = PixelLuma(&pair.current.pixels[i * 4u]);
                    b = BilinearLuma(pair.previous, static_cast<float>(x) + dx, static_cast<float>(y) + dy, inside);
                } else {
                    a = PixelLuma(&pair.previous.pixels[i * 4u]);
                    b = BilinearLuma(pair.current, static_cast<float>(x) + dx, static_cast<float>(y) + dy, inside);
                }
                const float residual = inside ? std::min(std::abs(a - b) / 255.0f, 0.35f) / 0.35f : 1.0f;
                sum += residual;
                motionSum += std::sqrt(dx * dx + dy * dy);
                ++count;
            }
        }
    }
    c.meanMotion = count ? static_cast<float>(motionSum / static_cast<double>(count)) : 0.0f;
    if (!count) return std::numeric_limits<float>::infinity();
    float e = static_cast<float>(sum / static_cast<double>(count));
    // Prevent a constant/zero channel from tying the zero-motion baseline on a
    // sequence that clearly contains motion.
    if (c.meanMotion < 0.025f) e += 0.025f;
    return e;
}

int MotionChannelPrior(const std::string& c, bool wantX) {
    int p = 0;
    if (ContainsI(c, "vector") || ContainsI(c, "flow") || ContainsI(c, "motion")) p += 20;
    const auto leaf = LowerAscii(ChannelLeaf(c));
    if (wantX && (leaf == "x" || leaf == "r")) p += 10;
    if (!wantX && (leaf == "y" || leaf == "g")) p += 10;
    if (leaf == "a" || leaf == "alpha") p -= 8;
    return p;
}

float MotionConventionPenalty(const MotionCandidate& c) {
    auto axisPenalty = [](const std::string& channel, bool wantX) {
        const auto leaf = LowerAscii(ChannelLeaf(channel));
        const bool semantic = ContainsI(channel, "vector") || ContainsI(channel, "flow") || ContainsI(channel, "motion");
        float p = 0.0f;
        if (wantX) {
            if (leaf == "x" || leaf == "r") p = 0.0f;
            else if (leaf == "y" || leaf == "g") p = 0.020f;
            else if (leaf == "z" || leaf == "b") p = 0.035f;
            else if (leaf == "w" || leaf == "a" || leaf == "alpha") p = 0.070f;
            else p = 0.025f;
        } else {
            if (leaf == "y" || leaf == "g") p = 0.0f;
            else if (leaf == "x" || leaf == "r") p = 0.020f;
            else if (leaf == "z" || leaf == "b") p = 0.035f;
            else if (leaf == "w" || leaf == "a" || leaf == "alpha") p = 0.070f;
            else p = 0.025f;
        }
        if (semantic) p *= 0.35f;
        return p;
    };
    float p = axisPenalty(c.x, true) + axisPenalty(c.y, false);
    if (c.unit != "pixels") p += 0.002f;
    // Prefer the native current->previous interpretation when both directions
    // reproject almost equally well. Choose forward-flow inversion only when
    // the video evidence is meaningfully better.
    if (c.direction == ExternalMotionDirection::PreviousToCurrent) p += 0.006f;
    if (c.flipX) p += 0.0002f;
    if (c.flipY) p += 0.0002f;
    return p;
}

std::vector<std::string> LimitMotionChannels(std::vector<std::string> channels, size_t maxChannels) {
    std::stable_sort(channels.begin(), channels.end(), [](const std::string& a, const std::string& b) {
        const int pa = std::max(MotionChannelPrior(a, true), MotionChannelPrior(a, false));
        const int pb = std::max(MotionChannelPrior(b, true), MotionChannelPrior(b, false));
        return pa > pb;
    });
    if (channels.size() > maxChannels) channels.resize(maxChannels);
    return channels;
}

} // namespace

namespace video {

ExternalDepthCalibrationResult CalibrateExternalDepthSequence(
    const std::filesystem::path& firstFrame,
    size_t maxSampleFrames,
    size_t maxValuesPerFrame) {
    ExternalDepthCalibrationResult out;
    if (firstFrame.empty()) {
        out.summary = "Select a depth EXR sequence first.";
        return out;
    }
    auto all = FindSequenceFrames(firstFrame);
    if (all.empty()) throw std::runtime_error("No EXR frames were found for depth calibration");
    auto samples = EvenSamplePaths(all, std::max<size_t>(1u, maxSampleFrames));
    const auto channels = ListExrChannels(samples.front());
    if (channels.empty()) throw std::runtime_error("Depth EXR has no channels");

    std::vector<ChannelStats> stats;
    stats.reserve(channels.size());
    for (const auto& c : channels) stats.push_back(AnalyzeDepthChannel(c, samples, maxValuesPerFrame));
    std::sort(stats.begin(), stats.end(), [](const ChannelStats& a, const ChannelStats& b) { return a.score > b.score; });
    const ChannelStats& best = stats.front();
    if (!best.finite || best.robustSpan <= 1e-12f) {
        out.summary = "No non-constant finite depth-like channel was detected.";
        return out;
    }

    out.success = true;
    out.channel = best.channel;
    out.sampledFrames = samples.size();
    // If almost all representative values already occupy the normalized range,
    // preserve them directly. Otherwise use one robust GLOBAL P1-P99 range.
    if (best.in01Ratio >= 0.995f && best.p001 >= -1e-4f && best.p999 <= 1.0001f) {
        out.mapping = ExternalDepthMapping::Raw01;
        out.nearValue = 0.0f;
        out.farValue = 1.0f;
    } else {
        out.mapping = ExternalDepthMapping::FixedRange;
        out.nearValue = best.p01;
        out.farValue = best.p99;
        if (!std::isfinite(out.nearValue) || !std::isfinite(out.farValue) || out.farValue <= out.nearValue) {
            out.nearValue = best.p001;
            out.farValue = best.p999;
        }
    }
    // Metric / Z depth normally grows with distance. We keep near=0/far=1 and
    // tell DLSSNR that larger normalized values are farther. Users can still
    // invert explicitly for renderer-specific reversed-Z data.
    out.depthInverted = false;

    const float nameScore = std::clamp((DepthNamePrior(best.channel) + 2.0f) / 10.0f, 0.0f, 1.0f);
    const float spanScore = std::clamp(best.robustSpan / std::max(1e-6f, std::abs(best.p99)) * 3.0f, 0.0f, 1.0f);
    float separation = 1.0f;
    if (stats.size() > 1u) separation = std::clamp((best.score - stats[1].score + 1.0f) / 6.0f, 0.0f, 1.0f);
    out.confidence = std::clamp(0.35f * best.finiteRatio + 0.25f * spanScore + 0.20f * nameScore + 0.20f * separation, 0.0f, 1.0f);

    std::ostringstream s;
    s.setf(std::ios::fixed); s.precision(4);
    s << "Depth: channel=" << out.channel << ", samples=" << out.sampledFrames
      << ", P1=" << best.p01 << ", P50=" << best.p50 << ", P99=" << best.p99
      << ", mapping=" << (out.mapping == ExternalDepthMapping::Raw01 ? "Raw 0..1" : "Global P1-P99")
      << ", confidence=" << std::lround(out.confidence * 100.0f) << "%";
    out.summary = s.str();
    return out;
}

ExternalMotionCalibrationResult CalibrateExternalMotionSequence(
    const std::filesystem::path& firstFrame,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    const std::vector<MotionCalibrationFramePair>& framePairs,
    size_t maxChannels) {
    ExternalMotionCalibrationResult out;
    if (firstFrame.empty()) { out.summary = "Select a motion EXR sequence first."; return out; }
    if (!sourceWidth || !sourceHeight) throw std::runtime_error("Motion calibration requires the source-video resolution");
    if (framePairs.empty()) { out.summary = "No adjacent video frame pairs are available for motion calibration."; return out; }
    const auto pattern = ParseExrSequencePattern(firstFrame);
    auto channels = LimitMotionChannels(ListExrChannels(firstFrame), std::max<size_t>(2u, maxChannels));
    if (channels.size() < 2u) throw std::runtime_error("Motion EXR must expose at least two channels");

    const uint32_t aw = framePairs.front().current.width;
    const uint32_t ah = framePairs.front().current.height;
    if (!aw || !ah) throw std::runtime_error("Motion calibration video samples are empty");
    MotionGridCache cache; cache.analysisWidth = aw; cache.analysisHeight = ah;

    double zeroSum = 0.0;
    for (const auto& p : framePairs) zeroSum += PairZeroMotionError(p);
    out.zeroMotionError = static_cast<float>(zeroSum / static_cast<double>(framePairs.size()));

    struct Unit { float x; float y; const char* label; };
    const Unit bases[] = {
        {1.0f, 1.0f, "pixels"},
        {static_cast<float>(sourceWidth), static_cast<float>(sourceHeight), "UV"},
        {static_cast<float>(sourceWidth) * 0.5f, static_cast<float>(sourceHeight) * 0.5f, "NDC"},
    };
    const float refine[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};

    std::vector<MotionCandidate> candidates;
    candidates.reserve(channels.size() * (channels.size() - 1u) * 120u);
    for (const auto& x : channels) for (const auto& y : channels) {
        if (x == y) continue;
        for (const auto& base : bases) for (float f : refine) {
            for (int signBits = 0; signBits < 4; ++signBits) {
                for (int dir = 0; dir < 2; ++dir) {
                    MotionCandidate c;
                    c.x = x; c.y = y;
                    c.sx = base.x * f; c.sy = base.y * f;
                    c.flipX = (signBits & 1) != 0;
                    c.flipY = (signBits & 2) != 0;
                    c.direction = dir == 0 ? ExternalMotionDirection::CurrentToPrevious
                                           : ExternalMotionDirection::PreviousToCurrent;
                    c.unit = base.label;
                    if (f != 1.0f) {
                        std::ostringstream u; u << base.label << " x" << f; c.unit = u.str();
                    }
                    try {
                        c.error = EvaluateMotionCandidate(c, pattern, sourceWidth, sourceHeight, framePairs, cache);
                        c.cost = c.error + MotionConventionPenalty(c);
                    } catch (...) {
                        c.error = c.cost = std::numeric_limits<float>::infinity();
                    }
                    if (std::isfinite(c.cost)) candidates.push_back(std::move(c));
                }
            }
        }
    }
    if (candidates.empty()) {
        out.summary = "No valid motion convention candidate could be evaluated.";
        return out;
    }

    std::sort(candidates.begin(), candidates.end(), [](const MotionCandidate& a, const MotionCandidate& b) {
        if (std::abs(a.cost - b.cost) > 1e-7f) return a.cost < b.cost;
        const int pa = MotionChannelPrior(a.x, true) + MotionChannelPrior(a.y, false) + (a.unit == "pixels" ? 5 : 0);
        const int pb = MotionChannelPrior(b.x, true) + MotionChannelPrior(b.y, false) + (b.unit == "pixels" ? 5 : 0);
        return pa > pb;
    });
    const MotionCandidate& best = candidates.front();
    const float second = candidates.size() > 1u ? candidates[1].cost : 1.0f;

    // Auto-calibration must demonstrate that the proposed motion actually
    // explains adjacent frames better than zero motion. Otherwise signs, units
    // and channel order are not observable (for example on a static shot), so
    // applying a guessed convention would be worse than leaving it manual.
    if (!(best.error < out.zeroMotionError * 0.999f)) {
        std::ostringstream reason;
        reason.setf(std::ios::fixed); reason.precision(4);
        reason << "Motion auto-calibration is inconclusive: best reprojection error=" << best.error
               << " does not improve the zero-motion baseline=" << out.zeroMotionError
               << ". Use a segment with visible motion or configure the convention manually.";
        out.summary = reason.str();
        return out;
    }

    out.success = true;
    out.xChannel = best.x;
    out.yChannel = best.y;
    out.importScaleX = best.sx;
    out.importScaleY = best.sy;
    out.flipX = best.flipX;
    out.flipY = best.flipY;
    out.direction = best.direction;
    out.reprojectionError = best.error;
    out.sampledPairs = framePairs.size();
    out.unitLabel = best.unit;

    const float improvement = std::clamp((out.zeroMotionError - best.error) / std::max(1e-5f, out.zeroMotionError), 0.0f, 1.0f);
    const float separation = std::clamp((second - best.cost) / std::max(1e-5f, second), 0.0f, 1.0f);
    const float motionEvidence = std::clamp(best.meanMotion / 2.0f, 0.0f, 1.0f);
    out.confidence = std::clamp(0.65f * improvement + 0.25f * separation + 0.10f * motionEvidence, 0.0f, 1.0f);

    std::ostringstream s;
    s.setf(std::ios::fixed); s.precision(4);
    s << "Motion: X=" << out.xChannel << ", Y=" << out.yChannel
      << ", scale=" << out.importScaleX << "/" << out.importScaleY
      << ", flip=" << (out.flipX ? "X" : "-") << (out.flipY ? "Y" : "-")
      << ", direction=" << (out.direction == ExternalMotionDirection::CurrentToPrevious ? "current->previous" : "previous->current")
      << ", unit=" << out.unitLabel
      << ", error=" << out.reprojectionError << " vs zero=" << out.zeroMotionError
      << ", confidence=" << std::lround(out.confidence * 100.0f) << "%";
    out.summary = s.str();
    return out;
}

} // namespace video
