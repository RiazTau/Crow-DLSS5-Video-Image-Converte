#include "video/SpatialMotionConsensus.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    Rgba8Image image;
    image.width=8; image.height=4; image.pixels.resize(8u*4u*4u);
    for (uint32_t y=0;y<image.height;++y) for(uint32_t x=0;x<image.width;++x) {
        auto* p=&image.pixels[(static_cast<size_t>(y)*image.width+x)*4u];
        p[0]=static_cast<uint8_t>(x*20u); p[1]=static_cast<uint8_t>(y*40u); p[2]=80; p[3]=255;
    }
    auto half=video::DownsampleHalfForMotion(image);
    assert(half.width==4 && half.height==2 && half.pixels.size()==4u*2u*4u);
    auto quarter=video::DownsampleQuarterForMotion(image);
    assert(quarter.width==2 && quarter.height==1 && quarter.pixels.size()==2u*1u*4u);

    video::TemporalFlowResult full;
    full.motionXY.assign(8u*4u*2u,0.0f);
    full.rawMotionXY.assign(full.motionXY.size(),0.0f);
    full.confidence.assign(8u*4u,0.85f);
    full.motionUncertainty.assign(8u*4u,0.20f);
    for(size_t i=0;i<8u*4u;++i){ full.motionXY[i*2u]=-4.0f; full.rawMotionXY[i*2u]=-4.0f; }
    const size_t bad=2u*8u+5u;
    full.motionXY[bad*2u]=-12.0f; full.rawMotionXY[bad*2u]=-12.0f; full.confidence[bad]=0.25f; full.motionUncertainty[bad]=0.92f;

    video::TemporalFlowResult coarse;
    coarse.motionXY.assign(4u*2u*2u,0.0f);
    coarse.confidence.assign(4u*2u,0.95f);
    coarse.motionUncertainty.assign(4u*2u,0.10f);
    for(size_t i=0;i<4u*2u;++i) coarse.motionXY[i*2u]=-2.0f; // half-res pixels -> -4 full-res

    auto fused=video::FuseCoarseNvofMotion(full,coarse,8,4,4,2,video::NvofReliabilityMode::Strong);
    assert(fused.spatialConsensusCorrectedFraction>0.0f);
    assert(std::abs(fused.motionXY[bad*2u]+4.0f) < std::abs(full.motionXY[bad*2u]+4.0f));
    assert(fused.rawMotionXY[bad*2u] == full.rawMotionXY[bad*2u]);
    assert(fused.motionUncertainty[bad] < full.motionUncertainty[bad]);
    std::cout << "PASS: anti-aliased multiscale spatial prior corrects a low-confidence phase slip and preserves raw NVOF\n";
}
