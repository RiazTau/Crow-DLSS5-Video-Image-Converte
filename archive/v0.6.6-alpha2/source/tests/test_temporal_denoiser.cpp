#include "video/TemporalDenoiser.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>

static Rgba8Image NoisyFrame(uint32_t w, uint32_t h, int seed, int base = 128, float sigma = 18.0f) {
    Rgba8Image img; img.width = w; img.height = h; img.pixels.resize(static_cast<size_t>(w) * h * 4u);
    std::mt19937 rng(seed); std::normal_distribution<float> n(0.0f, sigma);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        for (int c = 0; c < 3; ++c) img.pixels[i*4u+c] = static_cast<uint8_t>(std::clamp(std::lround(base+n(rng)),0l,255l));
        img.pixels[i*4u+3] = 255;
    }
    return img;
}

static double Rmse(const Rgba8Image& img, int target) {
    double e = 0.0; size_t n = 0;
    for (size_t i=0;i<img.pixels.size()/4u;++i) for(int c=0;c<3;++c) {
        const double d = static_cast<double>(img.pixels[i*4u+c]) - target; e += d*d; ++n;
    }
    return std::sqrt(e / static_cast<double>(n));
}

int main() {
    constexpr uint32_t w=64,h=48; const size_t px=static_cast<size_t>(w)*h;
    video::TemporalDenoiseSettings s; s.mode=video::DenoiseMode::FullHq; s.strength=0.90f; s.historyWeight=0.92f;
    video::TemporalDenoiser d(s);
    video::TemporalFlowResult flow; flow.motionXY.assign(px*2u,0.0f); flow.confidence.assign(px,1.0f); flow.sceneCut=false;
    double input=0.0, output=0.0;
    for(int f=0;f<12;++f) {
        auto frame=NoisyFrame(w,h,100+f);
        input += Rmse(frame,128);
        auto out=d.Process(frame,&flow,f==0,nullptr);
        output += Rmse(out,128);
    }
    input/=12.0; output/=12.0;
    std::cout << "input_rmse=" << input << " output_rmse=" << output << "\n";
    if (!(output < input * 0.80)) return 2;

    // A scene cut must not drag the previous gray history into a bright frame.
    auto bright=NoisyFrame(w,h,999,220,3.0f); flow.sceneCut=true;
    auto cut=d.Process(bright,&flow,true,nullptr);
    double mean=0.0;
    for(size_t i=0;i<px;++i) mean += cut.pixels[i*4u];
    mean/=px;
    std::cout << "cut_mean=" << mean << "\n";
    if (mean < 200.0) return 3;
    return 0;
}
