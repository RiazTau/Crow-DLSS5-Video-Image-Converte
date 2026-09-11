#include "video/ExternalDataCalibration.h"
#include "video/ExternalSequence.h"
#include "ImageImport.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static constexpr uint32_t W = 64, H = 48;

static int FrameNumber(const fs::path& p) {
    const auto stem = p.stem().string();
    const auto pos = stem.find_last_not_of("0123456789");
    return std::stoi(stem.substr(pos == std::string::npos ? 0 : pos + 1));
}

std::vector<std::string> ListExrChannels(const fs::path&) {
    return {"A", "B", "G", "R"};
}

FloatChannelMap LoadExrFloatChannel(const fs::path& path, const std::string& channel) {
    FloatChannelMap out; out.width = W; out.height = H; out.values.resize(static_cast<size_t>(W) * H);
    const std::string filename = path.filename().string();
    const bool motion = filename.find("motion_") == 0;
    const bool forward = filename.find("forward_") == 0;
    const int f = FrameNumber(path);
    if (!motion && !forward) {
        if (channel == "A") { std::fill(out.values.begin(), out.values.end(), 1.0f); return out; }
        for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) {
            const float d = 100.0f + 8.0f * x + 4.0f * y + 2.0f * f;
            out.values[static_cast<size_t>(y) * W + x] = d;
        }
        return out;
    }
    if (channel == "A") { std::fill(out.values.begin(), out.values.end(), 1.0f); return out; }
    if (channel == "B" || channel == "G") { std::fill(out.values.begin(), out.values.end(), 0.0f); return out; }
    std::fill(out.values.begin(), out.values.end(), 0.0f);
    if (forward) {
        // Forward flow at frame f: previous/source pixel -> next/current pixel.
        if (channel == "R") {
            const int bx = 8 + f * f;
            const float dx = static_cast<float>((8 + (f + 1) * (f + 1)) - bx);
            for (int y = 16; y < 30; ++y) for (int x = bx; x < bx + 16; ++x) {
                if (x >= 0 && x < static_cast<int>(W)) out.values[static_cast<size_t>(y) * W + static_cast<uint32_t>(x)] = dx;
            }
        }
    } else if (channel == "R" && f > 0) {
        // Current frame f has a block at x = 12 + 2*f. Current->previous is -2.
        const int bx = 12 + 2 * f;
        for (int y = 16; y < 30; ++y) for (int x = bx; x < bx + 16; ++x) {
            if (x >= 0 && x < static_cast<int>(W)) out.values[static_cast<size_t>(y) * W + static_cast<uint32_t>(x)] = -2.0f;
        }
    }
    return out;
}

static Rgba8Image Frame(int f) {
    Rgba8Image im; im.width = W; im.height = H; im.pixels.assign(static_cast<size_t>(W) * H * 4u, 20);
    for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) im.pixels[i * 4u + 3u] = 255;
    const int bx = 12 + 2 * f;
    for (int y = 16; y < 30; ++y) for (int x = bx; x < bx + 16; ++x) {
        if (x < 0 || x >= static_cast<int>(W)) continue;
        const size_t i = static_cast<size_t>(y) * W + static_cast<uint32_t>(x);
        im.pixels[i * 4u + 0u] = 235;
        im.pixels[i * 4u + 1u] = 235;
        im.pixels[i * 4u + 2u] = 235;
    }
    return im;
}

static Rgba8Image FrameAccelerated(int f) {
    Rgba8Image im; im.width = W; im.height = H; im.pixels.resize(static_cast<size_t>(W) * H * 4u);
    for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) {
        const size_t i = static_cast<size_t>(y) * W + x;
        const uint8_t v = static_cast<uint8_t>((x * 7u + y * 11u) & 0xffu);
        im.pixels[i * 4u + 0u] = v; im.pixels[i * 4u + 1u] = static_cast<uint8_t>(v / 2u);
        im.pixels[i * 4u + 2u] = static_cast<uint8_t>(255u - v); im.pixels[i * 4u + 3u] = 255;
    }
    const int bx = 8 + f * f;
    for (int y = 16; y < 30; ++y) for (int x = bx; x < bx + 16; ++x) {
        if (x < 0 || x >= static_cast<int>(W)) continue;
        const size_t i = static_cast<size_t>(y) * W + static_cast<uint32_t>(x);
        im.pixels[i * 4u + 0u] = 245; im.pixels[i * 4u + 1u] = 245; im.pixels[i * 4u + 2u] = 245;
    }
    return im;
}

int main() {
    const fs::path root = fs::temp_directory_path() / "dlss5_v0651_cal_test";
    fs::remove_all(root); fs::create_directories(root);
    for (int f = 0; f < 6; ++f) {
        std::ofstream(root / ("depth_" + (f < 10 ? std::string("000") : std::string{}) + std::to_string(f) + ".exr")).put('\n');
        std::ofstream(root / ("motion_" + (f < 10 ? std::string("000") : std::string{}) + std::to_string(f) + ".exr")).put('\n');
        std::ofstream(root / ("forward_" + (f < 10 ? std::string("000") : std::string{}) + std::to_string(f) + ".exr")).put('\n');
    }

    const auto depth = video::CalibrateExternalDepthSequence(root / "depth_0000.exr", 5, 4096);
    assert(depth.success);
    assert(depth.channel == "R");
    assert(depth.mapping == video::ExternalDepthMapping::FixedRange);
    assert(depth.farValue > depth.nearValue);
    assert(depth.nearValue > 90.0f && depth.farValue < 1000.0f);

    std::vector<video::MotionCalibrationFramePair> pairs;
    for (int f = 1; f < 6; ++f) {
        video::MotionCalibrationFramePair p;
        p.currentOneBasedFrame = static_cast<uint64_t>(f + 1);
        p.previous = Frame(f - 1);
        p.current = Frame(f);
        pairs.push_back(std::move(p));
    }
    const auto motion = video::CalibrateExternalMotionSequence(root / "motion_0000.exr", W, H, pairs, 4);
    assert(motion.success);
    std::cerr << "motion result: " << motion.summary << "\n";
    assert(motion.xChannel == "R");
    assert(motion.yChannel == "G" || motion.yChannel == "B");
    assert(motion.direction == video::ExternalMotionDirection::CurrentToPrevious);
    assert(std::abs(std::abs(motion.importScaleX) - 1.0f) < 0.01f);
    assert(!motion.flipX); // raw R already stores -2 current->previous
    assert(motion.reprojectionError < motion.zeroMotionError);

    std::vector<video::MotionCalibrationFramePair> forwardPairs;
    for (int f = 1; f < 6; ++f) {
        video::MotionCalibrationFramePair p;
        p.currentOneBasedFrame = static_cast<uint64_t>(f + 1);
        p.previous = FrameAccelerated(f - 1);
        p.current = FrameAccelerated(f);
        forwardPairs.push_back(std::move(p));
    }
    const auto forwardMotion = video::CalibrateExternalMotionSequence(root / "forward_0000.exr", W, H, forwardPairs, 4);
    std::cerr << "forward result: " << forwardMotion.summary << "\n";
    assert(forwardMotion.success);
    assert(forwardMotion.xChannel == "R");
    assert(forwardMotion.direction == video::ExternalMotionDirection::PreviousToCurrent);
    assert(forwardMotion.reprojectionError < forwardMotion.zeroMotionError);

    video::ExternalRenderDataSettings readerSettings;
    readerSettings.motion.firstFrame = root / "forward_0000.exr";
    readerSettings.motion.xChannel = "R";
    readerSettings.motion.yChannel = "G";
    readerSettings.motion.direction = video::ExternalMotionDirection::PreviousToCurrent;
    video::ExternalRenderDataReader reader(readerSettings, W, H, 6, false, true);
    const auto prev2 = FrameAccelerated(1);
    const auto cur2 = FrameAccelerated(2);
    const auto inv = reader.LoadMotion(3, &prev2, cur2, 64, 0.95f);
    const int centerX = 8 + 2 * 2 + 8;
    const int centerY = 22;
    const size_t ci = static_cast<size_t>(centerY) * W + static_cast<uint32_t>(centerX);
    // Frame 1 -> 2 accelerated forward displacement is +3, so internal
    // current->previous vector at the current block is approximately -3.
    assert(!inv.sceneCut);
    assert(std::abs(inv.motionXY[ci * 2u + 0u] + 3.0f) < 0.25f);

    fs::remove_all(root);
    std::cout << "PASS auto calibration depth=" << depth.channel << " " << depth.nearValue << ".." << depth.farValue
              << " motion=" << motion.xChannel << "/" << motion.yChannel << " scale=" << motion.importScaleX
              << " err=" << motion.reprojectionError << " zero=" << motion.zeroMotionError << "\n";
    return 0;
}
