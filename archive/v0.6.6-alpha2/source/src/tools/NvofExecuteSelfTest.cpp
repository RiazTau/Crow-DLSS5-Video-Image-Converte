#include "D3D12Context.h"
#include "video/NvofFlowSession.h"
#include <Windows.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

Rgba8Image MakePattern(uint32_t w, uint32_t h) {
    Rgba8Image img;
    img.width = w; img.height = h; img.pixels.resize(static_cast<size_t>(w) * h * 4u);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint32_t cell = ((x / 17u) ^ (y / 13u)) & 1u;
            const uint32_t rings = ((x * x + y * y) >> 8u) & 0xffu;
            auto* p = &img.pixels[(static_cast<size_t>(y) * w + x) * 4u];
            p[0] = static_cast<uint8_t>((x * 5u + y * 3u + rings + cell * 71u) & 0xffu);
            p[1] = static_cast<uint8_t>((x * 2u + y * 7u + (x ^ y) + cell * 37u) & 0xffu);
            p[2] = static_cast<uint8_t>((x * 11u + y * 5u + rings * 3u + cell * 19u) & 0xffu);
            p[3] = 255u;
        }
    }
    return img;
}

Rgba8Image Shift(const Rgba8Image& src, int dx, int dy) {
    Rgba8Image out;
    out.width = src.width; out.height = src.height;
    out.pixels.assign(src.pixels.size(), 0u);
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            const int sx = static_cast<int>(x) - dx;
            const int sy = static_cast<int>(y) - dy;
            auto* dst = &out.pixels[(static_cast<size_t>(y) * out.width + x) * 4u];
            if (sx >= 0 && sy >= 0 && sx < static_cast<int>(src.width) && sy < static_cast<int>(src.height)) {
                const auto* in = &src.pixels[(static_cast<size_t>(sy) * src.width + static_cast<uint32_t>(sx)) * 4u];
                dst[0] = in[0]; dst[1] = in[1]; dst[2] = in[2]; dst[3] = 255u;
            } else {
                dst[3] = 255u;
            }
        }
    }
    return out;
}

float Median(std::vector<float>& v) {
    if (v.empty()) throw std::runtime_error("No valid motion samples were produced");
    const size_t m = v.size() / 2u;
    std::nth_element(v.begin(), v.begin() + m, v.end());
    return v[m];
}

} // namespace

int wmain() {
    std::wcout << L"Crow-DLSS5-Video-Image-Converter NVOF Execute Self-Test V0.6.6-alpha2\n";
    std::wcout << L"=============================================\n";
    try {
        if (!video::NvofFlowSession::NativeBackendCompiled()) {
            std::wcout << L"Native bridge : NOT COMPILED\n";
            std::wcout << L"Build status  : " << video::NvofFlowSession::BuildStatusText() << L"\n";
            return 3;
        }

        D3D12Context d3d;
        DXGI_ADAPTER_DESC1 desc{};
        if (d3d.Adapter() && SUCCEEDED(d3d.Adapter()->GetDesc1(&desc))) {
            std::wcout << L"D3D12 adapter : " << desc.Description << L"\n";
        }
        std::wcout << L"Native bridge : READY\n";
        std::wcout << L"Test grid     : 4x4\n";

        constexpr uint32_t W = 640, H = 360;
        constexpr int dx = 24, dy = 8;
        auto previous = MakePattern(W, H);
        auto current = Shift(previous, dx, dy);

        video::NvofSettings nvofSettings{};
        nvofSettings.quality = video::NvofQuality::Slow;
        nvofSettings.outputGridSize = 4;
        nvofSettings.temporalHints = true;
        nvofSettings.outputCost = true;
        video::NvofFlowSession session(d3d, W, H, 0.28f, nvofSettings, nullptr);
        const auto first = session.Process(previous);
        if (!first.sceneCut) throw std::runtime_error("First NVOF frame did not force temporal reset");
        const auto flow = session.Process(current);
        if (flow.sceneCut) {
            throw std::runtime_error("Synthetic translated pair was incorrectly classified as a scene cut; score=" + std::to_string(flow.sceneCutScore));
        }

        std::vector<float> xs, ys, cs;
        for (uint32_t y = 48; y + 48 < H; y += 4u) {
            for (uint32_t x = 64; x + 64 < W; x += 4u) {
                const size_t i = static_cast<size_t>(y) * W + x;
                if (i >= flow.confidence.size() || i * 2u + 1u >= flow.motionXY.size()) continue;
                if (flow.confidence[i] < 0.10f) continue;
                xs.push_back(flow.motionXY[i * 2u]);
                ys.push_back(flow.motionXY[i * 2u + 1u]);
                cs.push_back(flow.confidence[i]);
            }
        }
        float mx = Median(xs), my = Median(ys), mc = Median(cs);
        const float expectedX = -static_cast<float>(dx);
        const float expectedY = -static_cast<float>(dy);

        std::cout << std::fixed << std::setprecision(3);
        std::cout << "Expected flow  : (" << expectedX << ", " << expectedY << ") px current->previous\n";
        std::cout << "Measured median: (" << mx << ", " << my << ") px\n";
        std::cout << "Median conf.   : " << mc << "\n";
        std::cout << "Scene score    : " << flow.sceneCutScore << "\n";
        std::cout << "Samples        : " << xs.size() << "\n";

        const bool scaleDirectionOk = std::abs(mx - expectedX) <= 8.0f && std::abs(my - expectedY) <= 6.0f;
        if (!scaleDirectionOk) {
            std::cerr << "RESULT         : FAIL (flow direction/scale outside alpha2 tolerance)\n";
            return 4;
        }
        std::cout << "RESULT         : PASS - real NvOFExecuteD3D12 produced correctly scaled current->previous motion\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Execute self-test failed: " << e.what() << "\n";
        return 1;
    }
}
