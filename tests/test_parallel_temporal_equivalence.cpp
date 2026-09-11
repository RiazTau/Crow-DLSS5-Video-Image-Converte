#include "video/TemporalDenoiser.h"
#include "video/TemporalFlow.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

static void SetThreads(const char* value) {
#ifdef _WIN32
    _putenv_s("DLSS5_PERF_THREADS", value);
#else
    setenv("DLSS5_PERF_THREADS", value, 1);
#endif
}

static Rgba8Image MakeFrame(uint32_t w, uint32_t h, int seed) {
    Rgba8Image img; img.width=w; img.height=h; img.pixels.resize(static_cast<size_t>(w)*h*4u);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> d(0,255);
    for (size_t i=0;i<static_cast<size_t>(w)*h;++i) {
        img.pixels[i*4u+0]=static_cast<uint8_t>(d(rng));
        img.pixels[i*4u+1]=static_cast<uint8_t>(d(rng));
        img.pixels[i*4u+2]=static_cast<uint8_t>(d(rng));
        img.pixels[i*4u+3]=255;
    }
    return img;
}

int main() {
    constexpr uint32_t w=192, h=128;
    const size_t n=static_cast<size_t>(w)*h;
    video::TemporalFlowResult flow;
    flow.motionXY.resize(n*2u);
    flow.confidence.resize(n);
    for (size_t i=0;i<n;++i) {
        flow.motionXY[i*2u+0u]=std::sin(static_cast<float>(i)*0.013f)*1.5f;
        flow.motionXY[i*2u+1u]=std::cos(static_cast<float>(i)*0.011f)*0.8f;
        flow.confidence[i]=0.35f + 0.65f * static_cast<float>(i%97u)/96.0f;
    }

    video::TemporalDenoiseSettings ds;
    ds.mode=video::DenoiseMode::FullHq;
    video::TemporalDenoiser single(ds), parallel(ds);
    std::vector<Rgba8Image> singleOut, parallelOut;

    SetThreads("1");
    for (int f=0;f<5;++f) singleOut.push_back(single.Process(MakeFrame(w,h,100+f), &flow, f==0, nullptr));
    SetThreads("8");
    for (int f=0;f<5;++f) parallelOut.push_back(parallel.Process(MakeFrame(w,h,100+f), &flow, f==0, nullptr));

    if (singleOut.size()!=parallelOut.size()) return 2;
    for (size_t f=0;f<singleOut.size();++f) {
        if (singleOut[f].pixels != parallelOut[f].pixels) {
            std::cerr << "Temporal denoiser pixel mismatch at frame " << f << "\n";
            return 3;
        }
    }

    video::TemporalFlowEstimator estimator;
    std::vector<float> curDepth(n), prevDepth(n);
    for (size_t i=0;i<n;++i) {
        curDepth[i]=static_cast<float>(i%101u)/100.0f;
        prevDepth[i]=std::clamp(curDepth[i] + 0.01f*std::sin(static_cast<float>(i)), 0.0f, 1.0f);
    }
    SetThreads("1");
    auto depth1=estimator.StabilizeDepth(curDepth, prevDepth, w, h, flow);
    auto out1=estimator.StabilizeOutput(singleOut.back(), singleOut[3], flow);
    SetThreads("8");
    auto depth8=estimator.StabilizeDepth(curDepth, prevDepth, w, h, flow);
    auto out8=estimator.StabilizeOutput(singleOut.back(), singleOut[3], flow);
    if (depth1 != depth8 || out1.pixels != out8.pixels) {
        std::cerr << "Parallel stabilizer output mismatch\n";
        return 4;
    }

    std::cout << "PASS: parallel temporal stages are pixel-equivalent to single-thread mode\n";
    return 0;
}
