#include "video/NvofFlowPostprocess.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

static video::NvofPackedVector Pack(float x, float y) {
    return {static_cast<int16_t>(std::lround(x * 32.0f)), static_cast<int16_t>(std::lround(y * 32.0f))};
}

int main() {
    assert(std::abs(video::DecodeNvofFixed11_5(32) - 1.0f) < 1e-6f);
    assert(std::abs(video::DecodeNvofFixed11_5(-96) + 3.0f) < 1e-6f);

    constexpr uint32_t W=8, H=4, GW=4, GH=2;
    std::vector<video::NvofPackedVector> f(GW*GH, Pack(-3.0f, 0.5f));
    std::vector<video::NvofPackedVector> b(GW*GH, Pack(+3.0f,-0.5f));
    std::vector<uint8_t> cost(GW*GH, 8);
    video::NvofPostprocessInput in{};
    in.sourceWidth=W; in.sourceHeight=H; in.gridWidth=GW; in.gridHeight=GH;
    in.forward=&f; in.backward=&b; in.forwardCost=&cost;
    auto out=video::PostprocessNvofFlow(in);
    assert(!out.sceneCut);
    assert(out.motionXY.size()==static_cast<size_t>(W)*H*2u);
    assert(out.confidence.size()==static_cast<size_t>(W)*H);
    for(size_t i=0;i<static_cast<size_t>(W)*H;i++) {
        assert(std::abs(out.motionXY[i*2]+3.0f)<1e-5f);
        assert(std::abs(out.motionXY[i*2+1]-0.5f)<1e-5f);
    }
    // Boundary pixels can legitimately lose FB confidence because q+F leaves the image.
    // Check an interior location where both forward and backward samples are valid.
    assert(out.confidence[static_cast<size_t>(1)*W+6] > 0.90f);

    // Deliberately inconsistent backward flow must lower confidence.
    std::fill(b.begin(), b.end(), Pack(0.0f,0.0f));
    auto bad=video::PostprocessNvofFlow(in);
    float goodMean=0.0f,badMean=0.0f;
    for(float c:out.confidence) goodMean+=c;
    for(float c:bad.confidence) badMean+=c;
    goodMean/=out.confidence.size(); badMean/=bad.confidence.size();
    assert(badMean < goodMean - 0.15f);

    in.sceneCut=true; in.sceneCutScore=.8f;
    auto cut=video::PostprocessNvofFlow(in);
    assert(cut.sceneCut && cut.sceneCutScore==.8f);
    for(float v:cut.motionXY) assert(v==0.0f);
    std::cout << "PASS: NVOF S10.5 decode, grid upsample, FB/cost confidence, scene-cut reset\n";
}
