#include "video/MotionPreview.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static std::vector<float> ConstantField(uint32_t w, uint32_t h, float x, float y) {
    std::vector<float> m(static_cast<size_t>(w) * h * 2u);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        m[i * 2u + 0u] = x;
        m[i * 2u + 1u] = y;
    }
    return m;
}

int main() {
    constexpr uint32_t w = 64, h = 32;
    auto right = ConstantField(w, h, 4.0f, 0.0f);
    auto r = video::BuildMotionPreview(right, w, h, 1.0f, 1.0f, 32);
    assert(r.image.width == 32);
    assert(r.image.height == 16);
    assert(!r.image.pixels.empty());
    assert(std::fabs(r.robustMagnitudePixels - 4.0f) < 0.01f);
    const int rightSum = r.image.pixels[0] + r.image.pixels[1] + r.image.pixels[2];
    assert(rightSum > 20);

    auto down = ConstantField(w, h, 0.0f, 4.0f);
    auto d = video::BuildMotionPreview(down, w, h, 1.0f, 1.0f, 32);
    assert(d.image.pixels[0] != r.image.pixels[0] ||
           d.image.pixels[1] != r.image.pixels[1] ||
           d.image.pixels[2] != r.image.pixels[2]);

    auto robust = ConstantField(w, h, 2.0f, 0.0f);
    robust[0] = 1000.0f;
    auto rr = video::BuildMotionPreview(robust, w, h, 1.0f, 1.0f, 64);
    assert(rr.robustMagnitudePixels > 1.9f && rr.robustMagnitudePixels < 2.1f);

    auto scaled = video::BuildMotionPreview(right, w, h, -2.0f, 0.5f, 64);
    assert(std::fabs(scaled.robustMagnitudePixels - 8.0f) < 0.01f);

    std::cout << "PASS: motion HSV preview / robust magnitude / effective scale\n";
    return 0;
}
