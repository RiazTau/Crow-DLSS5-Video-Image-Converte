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
    std::vector<uint8_t> bcost(GW*GH, 8);
    video::NvofPostprocessInput in{};
    in.sourceWidth=W; in.sourceHeight=H; in.gridWidth=GW; in.gridHeight=GH;
    in.forward=&f; in.backward=&b; in.forwardCost=&cost; in.backwardCost=&bcost;
    in.flowGridSize=2; in.reliabilityMode=video::NvofReliabilityMode::Off;
    auto out=video::PostprocessNvofFlow(in);
    assert(!out.sceneCut);
    assert(out.motionXY.size()==static_cast<size_t>(W)*H*2u);
    assert(out.confidence.size()==static_cast<size_t>(W)*H);
    assert(out.historyVisibility.size()==static_cast<size_t>(W)*H);
    assert(out.occlusionProbability.size()==static_cast<size_t>(W)*H);
    assert(out.disocclusionProbability.size()==static_cast<size_t>(W)*H);
    assert(out.motionUncertainty.size()==static_cast<size_t>(W)*H);
    for(size_t i=0;i<static_cast<size_t>(W)*H;i++) {
        assert(std::abs(out.motionXY[i*2]+3.0f)<1e-5f);
        assert(std::abs(out.motionXY[i*2+1]-0.5f)<1e-5f);
    }
    // Boundary pixels can legitimately lose FB confidence because q+F leaves the image.
    // Check an interior location where both forward and backward samples are valid.
    const size_t goodInterior=static_cast<size_t>(1)*W+6;
    assert(out.confidence[goodInterior] > 0.90f);
    assert(out.historyVisibility[goodInterior] > 0.70f);
    assert(out.motionUncertainty[goodInterior] < 0.35f);
    // Current pixels whose current->previous reprojection leaves the image are explicit
    // disocclusions in alpha5 and must not be allowed to accumulate NR history.
    const size_t revealedEdge=static_cast<size_t>(1)*W+0u;
    assert(out.disocclusionProbability[revealedEdge] > 0.95f);
    assert(out.historyVisibility[revealedEdge] < 0.05f);

    // Deliberately inconsistent backward flow must lower confidence.
    std::fill(b.begin(), b.end(), Pack(0.0f,0.0f));
    auto bad=video::PostprocessNvofFlow(in);
    float goodMean=0.0f,badMean=0.0f,goodUncertainty=0.0f,badUncertainty=0.0f;
    for(float c:out.confidence) goodMean+=c;
    for(float c:bad.confidence) badMean+=c;
    for(float u:out.motionUncertainty) goodUncertainty+=u;
    for(float u:bad.motionUncertainty) badUncertainty+=u;
    goodMean/=out.confidence.size(); badMean/=bad.confidence.size();
    goodUncertainty/=out.motionUncertainty.size(); badUncertainty/=bad.motionUncertainty.size();
    assert(badMean < goodMean - 0.15f);
    assert(badUncertainty > goodUncertainty + 0.10f);

    // Reliable-motion repair: a single low-confidence periodic-texture outlier should
    // move toward the consistent neighbourhood/global vector instead of remaining raw.
    constexpr uint32_t RW=16, RH=8, RGW=8, RGH=4;
    std::vector<video::NvofPackedVector> rf(RGW*RGH, Pack(-4.0f, 0.0f));
    std::vector<video::NvofPackedVector> rb(RGW*RGH, Pack(+4.0f, 0.0f));
    std::vector<uint8_t> rc(RGW*RGH, 4), rbc(RGW*RGH, 4);
    const size_t badGrid = 1u * RGW + 4u;
    rf[badGrid] = Pack(-12.0f, 0.0f);
    rb[badGrid] = Pack(0.0f, 0.0f);
    rc[badGrid] = 250; rbc[badGrid] = 250;
    video::NvofPostprocessInput rin{};
    rin.sourceWidth=RW; rin.sourceHeight=RH; rin.gridWidth=RGW; rin.gridHeight=RGH;
    rin.forward=&rf; rin.backward=&rb; rin.forwardCost=&rc; rin.backwardCost=&rbc;
    rin.flowGridSize=2; rin.reliabilityMode=video::NvofReliabilityMode::Strong;
    auto repaired=video::PostprocessNvofFlow(rin);
    const size_t center=static_cast<size_t>(3)*RW+9u;
    assert(repaired.repairedFraction > 0.0f);
    assert(repaired.rawMotionXY.size() == repaired.motionXY.size());
    assert(std::abs(repaired.motionXY[center*2u] + 4.0f) < 4.0f);

    // Alpha4 dual-path contract: NR starts from raw NVOF, moves conservatively toward the
    // reliable FG candidate, and keeps history confidence cautious.
    auto nrSafe = video::BuildNrSafeReliableMotion(repaired, nullptr, RW, RH,
                                                    video::NvofReliabilityMode::Strong, 2);
    const float rawCenter = repaired.rawMotionXY[center*2u];
    const float fgCenter = repaired.motionXY[center*2u];
    const float nrCenter = nrSafe.motionXY[center*2u];
    assert(nrSafe.nrSafeCorrectedFraction > 0.0f);
    assert(std::abs(nrCenter - rawCenter) > 0.01f);
    assert(std::abs(nrCenter - rawCenter) <= std::abs(fgCenter - rawCenter) + 1e-4f);
    assert(nrSafe.confidence[center] <= repaired.confidence[center] + 1e-5f);
    auto edgeNr = video::BuildNrSafeReliableMotion(out, nullptr, W, H,
                                                   video::NvofReliabilityMode::Auto, 2);
    assert(edgeNr.confidence[revealedEdge] == 0.0f);


    // Alpha6 effective-trust gate: a periodic lock can be high-confidence according to
    // NVOFA yet independently uncertain. It must still be eligible for active repair.
    video::TemporalFlowResult highConfSlip;
    constexpr uint32_t UW=20, UH=12;
    const size_t UN=static_cast<size_t>(UW)*UH;
    highConfSlip.motionXY.assign(UN*2u,0.0f);
    highConfSlip.rawMotionXY.assign(UN*2u,0.0f);
    highConfSlip.confidence.assign(UN,0.95f);
    highConfSlip.historyVisibility.assign(UN,1.0f);
    highConfSlip.disocclusionProbability.assign(UN,0.0f);
    highConfSlip.occlusionProbability.assign(UN,0.0f);
    highConfSlip.motionUncertainty.assign(UN,0.08f);
    for(size_t i=0;i<UN;++i){ highConfSlip.motionXY[i*2u]=highConfSlip.rawMotionXY[i*2u]=-4.0f; }
    const size_t ui=static_cast<size_t>(UH/2u)*UW+UW/2u;
    highConfSlip.motionXY[ui*2u]=highConfSlip.rawMotionXY[ui*2u]=-12.0f;
    highConfSlip.motionUncertainty[ui]=0.95f; // independent evidence rejects the confident lock
    std::vector<float> uncertaintyDepth(UN,0.5f);
    video::RefineReliableMotionWithDepth(highConfSlip,&uncertaintyDepth,UW,UH,video::NvofReliabilityMode::Strong,1);
    assert(std::abs(highConfSlip.motionXY[ui*2u]+4.0f) < 4.0f);

    // Alpha6 robust affine global fallback: remove all reliable local neighbours around
    // the center so repair must use the camera model. A pan+zoom/rotation-like affine field
    // should reconstruct the center much closer to the analytic motion than the bad raw vector.
    constexpr uint32_t AW=64, AH=40;
    video::TemporalFlowResult affine;
    const size_t AN=static_cast<size_t>(AW)*AH;
    affine.motionXY.resize(AN*2u);
    affine.rawMotionXY.resize(AN*2u);
    affine.confidence.assign(AN,0.95f);
    affine.historyVisibility.assign(AN,1.0f);
    affine.disocclusionProbability.assign(AN,0.0f);
    affine.occlusionProbability.assign(AN,0.0f);
    affine.motionUncertainty.assign(AN,0.08f);
    const float acx=0.5f*static_cast<float>(AW-1u), acy=0.5f*static_cast<float>(AH-1u);
    for(uint32_t y=0;y<AH;++y) for(uint32_t x=0;x<AW;++x){
        const size_t i=static_cast<size_t>(y)*AW+x;
        const float dx=static_cast<float>(x)-acx, dy=static_cast<float>(y)-acy;
        const float vx=3.0f+0.020f*dx+0.010f*dy;
        const float vy=-2.0f-0.015f*dx+0.020f*dy;
        affine.motionXY[i*2u]=affine.rawMotionXY[i*2u]=vx;
        affine.motionXY[i*2u+1u]=affine.rawMotionXY[i*2u+1u]=vy;
    }
    const uint32_t ax=AW/2u, ay=AH/2u;
    const size_t ai=static_cast<size_t>(ay)*AW+ax;
    const float expectedAX=affine.motionXY[ai*2u], expectedAY=affine.motionXY[ai*2u+1u];
    affine.motionXY[ai*2u]=affine.rawMotionXY[ai*2u]=30.0f;
    affine.motionXY[ai*2u+1u]=affine.rawMotionXY[ai*2u+1u]=-20.0f;
    // Strong searches three rings. Make that entire local support unreliable to force affine fallback.
    for(int oy=-3;oy<=3;++oy) for(int ox=-3;ox<=3;++ox){
        const int xx=static_cast<int>(ax)+ox, yy=static_cast<int>(ay)+oy;
        if(xx<0||yy<0||xx>=static_cast<int>(AW)||yy>=static_cast<int>(AH)) continue;
        const size_t j=static_cast<size_t>(yy)*AW+static_cast<size_t>(xx);
        affine.confidence[j]=0.05f; affine.motionUncertainty[j]=0.95f;
    }
    std::vector<float> affineDepth(AN,0.5f);
    video::RefineReliableMotionWithDepth(affine,&affineDepth,AW,AH,video::NvofReliabilityMode::Strong,1);
    const float beforeAffine=std::hypot(30.0f-expectedAX,-20.0f-expectedAY);
    const float afterAffine=std::hypot(affine.motionXY[ai*2u]-expectedAX,affine.motionXY[ai*2u+1u]-expectedAY);
    assert(afterAffine < beforeAffine*0.45f);

    in.sceneCut=true; in.sceneCutScore=.8f;
    auto cut=video::PostprocessNvofFlow(in);
    assert(cut.sceneCut && cut.sceneCutScore==.8f);
    for(float v:cut.motionXY) assert(v==0.0f);
    std::cout << "PASS: NVOF decode, visibility/uncertainty topology, modal/affine repair, NR rejection, scene-cut reset\n";
}
