#include "SpatialMotionConsensus.h"
#include "ParallelRows.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

namespace video {
namespace {

bool BilinearVector(const std::vector<float>& v, uint32_t w, uint32_t h,
                    float x, float y, float& vx, float& vy) {
    if (!w || !h || v.size() != static_cast<size_t>(w) * h * 2u) return false;
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1u));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(w - 1u, x0 + 1u);
    const uint32_t y1 = std::min(h - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    auto sample = [&](uint32_t sx, uint32_t sy, int c) {
        return v[(static_cast<size_t>(sy) * w + sx) * 2u + static_cast<size_t>(c)];
    };
    const float ax = sample(x0,y0,0), bx = sample(x1,y0,0), cx = sample(x0,y1,0), dx = sample(x1,y1,0);
    const float ay = sample(x0,y0,1), by = sample(x1,y0,1), cy = sample(x0,y1,1), dy = sample(x1,y1,1);
    vx = (ax + (bx-ax)*tx) + ((cx + (dx-cx)*tx) - (ax + (bx-ax)*tx))*ty;
    vy = (ay + (by-ay)*tx) + ((cy + (dy-cy)*tx) - (ay + (by-ay)*tx))*ty;
    return std::isfinite(vx) && std::isfinite(vy);
}

float BilinearScalar(const std::vector<float>& v, uint32_t w, uint32_t h, float x, float y) {
    if (!w || !h || v.size() != static_cast<size_t>(w) * h) return 0.0f;
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1u));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1u));
    const uint32_t x0 = static_cast<uint32_t>(std::floor(x));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(y));
    const uint32_t x1 = std::min(w - 1u, x0 + 1u);
    const uint32_t y1 = std::min(h - 1u, y0 + 1u);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);
    const float a=v[static_cast<size_t>(y0)*w+x0], b=v[static_cast<size_t>(y0)*w+x1];
    const float c=v[static_cast<size_t>(y1)*w+x0], d=v[static_cast<size_t>(y1)*w+x1];
    const float top=a+(b-a)*tx, bottom=c+(d-c)*tx;
    return top+(bottom-top)*ty;
}

} // namespace

Rgba8Image DownsampleHalfForMotion(const Rgba8Image& input) {
    Rgba8Image out;
    if (!input.width || !input.height || input.pixels.size() != static_cast<size_t>(input.width) * input.height * 4u)
        return out;

    // Alpha6 replaces the old 2x2 box average with a true separable 5-tap binomial
    // low-pass [1 4 6 4 1] / 16 before 2x decimation. The horizontal pass already
    // decimates X, so the vertical pass processes only half as many columns. This keeps
    // the anti-aliasing benefit without paying for a direct 25-tap 2D kernel at 4K/8K.
    out.width = std::max(1u, (input.width + 1u) / 2u);
    out.height = std::max(1u, (input.height + 1u) / 2u);
    out.pixels.resize(static_cast<size_t>(out.width) * out.height * 4u);
    static constexpr int k[5] = {1, 4, 6, 4, 1};

    // Horizontal filtered sums remain in [0, 255*16], so uint16_t is sufficient.
    std::vector<uint16_t> horizontal(static_cast<size_t>(input.height) * out.width * 4u, 0u);
    perf::ParallelForRows(input.height, 16u, [&](uint32_t y0, uint32_t y1, unsigned) {
        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = 0; x < out.width; ++x) {
                const int sx = static_cast<int>(x * 2u);
                auto* dst = &horizontal[(static_cast<size_t>(y) * out.width + x) * 4u];
                for (int c = 0; c < 4; ++c) {
                    uint32_t sum = 0u;
                    for (int ox = -2; ox <= 2; ++ox) {
                        const int xx = std::clamp(sx + ox, 0, static_cast<int>(input.width) - 1);
                        sum += static_cast<uint32_t>(k[ox + 2]) *
                               input.pixels[(static_cast<size_t>(y) * input.width + static_cast<size_t>(xx)) * 4u + c];
                    }
                    dst[c] = static_cast<uint16_t>(sum);
                }
            }
        }
    });

    perf::ParallelForRows(out.height, 16u, [&](uint32_t y0, uint32_t y1, unsigned) {
        for (uint32_t y = y0; y < y1; ++y) {
            const int sy = static_cast<int>(y * 2u);
            for (uint32_t x = 0; x < out.width; ++x) {
                auto* dst = &out.pixels[(static_cast<size_t>(y) * out.width + x) * 4u];
                for (int c = 0; c < 4; ++c) {
                    uint32_t sum = 0u;
                    for (int oy = -2; oy <= 2; ++oy) {
                        const int yy = std::clamp(sy + oy, 0, static_cast<int>(input.height) - 1);
                        sum += static_cast<uint32_t>(k[oy + 2]) *
                               horizontal[(static_cast<size_t>(yy) * out.width + x) * 4u + c];
                    }
                    // Horizontal and vertical kernels each sum to 16 => divide by 256.
                    dst[c] = static_cast<uint8_t>((sum + 128u) >> 8u);
                }
            }
        }
    });
    return out;
}

Rgba8Image DownsampleQuarterForMotion(const Rgba8Image& input) {
    const Rgba8Image half = DownsampleHalfForMotion(input);
    return DownsampleHalfForMotion(half);
}

TemporalFlowResult FuseCoarseNvofMotion(const TemporalFlowResult& full,
                                        const TemporalFlowResult& coarse,
                                        uint32_t fullWidth,
                                        uint32_t fullHeight,
                                        uint32_t coarseWidth,
                                        uint32_t coarseHeight,
                                        NvofReliabilityMode mode,
                                        float priorStrength) {
    TemporalFlowResult out=full;
    if (mode==NvofReliabilityMode::Off || full.sceneCut || coarse.sceneCut) return out;
    const size_t fullN=static_cast<size_t>(fullWidth)*fullHeight;
    const size_t coarseN=static_cast<size_t>(coarseWidth)*coarseHeight;
    if (!fullWidth || !fullHeight || !coarseWidth || !coarseHeight ||
        full.motionXY.size()!=fullN*2u || full.confidence.size()!=fullN ||
        coarse.motionXY.size()!=coarseN*2u || coarse.confidence.size()!=coarseN) return out;

    const float scaleX=static_cast<float>(fullWidth)/static_cast<float>(coarseWidth);
    const float scaleY=static_cast<float>(fullHeight)/static_cast<float>(coarseHeight);
    priorStrength=std::clamp(priorStrength,0.20f,1.0f);
    const float maxBlend=(mode==NvofReliabilityMode::Strong ? 0.68f : 0.42f)*priorStrength;
    const float coarseMin=mode==NvofReliabilityMode::Strong ? 0.66f : 0.72f;
    const float highFullProtect=mode==NvofReliabilityMode::Strong ? 0.90f : 0.82f;
    const bool fullHasUncertainty=full.motionUncertainty.size()==fullN;
    const bool coarseHasUncertainty=coarse.motionUncertainty.size()==coarseN;
    std::atomic<uint64_t> corrected{0};
    std::atomic<uint64_t> residualMilli{0};

    perf::ParallelForRows(fullHeight,32u,[&](uint32_t y0,uint32_t y1,unsigned){
        for(uint32_t y=y0;y<y1;++y){
            const float cy=(static_cast<float>(y)+0.5f)/scaleY-0.5f;
            for(uint32_t x=0;x<fullWidth;++x){
                const size_t i=static_cast<size_t>(y)*fullWidth+x;
                const float cx=(static_cast<float>(x)+0.5f)/scaleX-0.5f;
                float cvx=0.0f,cvy=0.0f;
                if(!BilinearVector(coarse.motionXY,coarseWidth,coarseHeight,cx,cy,cvx,cvy)) continue;
                cvx*=scaleX; cvy*=scaleY;
                const float cc=std::clamp(BilinearScalar(coarse.confidence,coarseWidth,coarseHeight,cx,cy),0.0f,1.0f);
                if(cc<coarseMin) continue;
                const float coarseUncertainty=coarseHasUncertainty ?
                    std::clamp(BilinearScalar(coarse.motionUncertainty,coarseWidth,coarseHeight,cx,cy),0.0f,1.0f) : (1.0f-cc);
                const float fx=full.motionXY[i*2u], fy=full.motionXY[i*2u+1u];
                const float fc=std::clamp(full.confidence[i],0.0f,1.0f);
                const float residual=std::hypot(fx-cvx,fy-cvy);
                const float coarseMag=std::hypot(cvx,cvy);
                const float threshold=(mode==NvofReliabilityMode::Strong ? 0.95f : 1.35f) +
                                      (mode==NvofReliabilityMode::Strong ? 0.075f : 0.10f)*coarseMag;
                if(residual<=threshold){
                    // Agreement between scales is useful evidence even if local texture is weak.
                    out.confidence[i]=std::max(out.confidence[i],std::min(0.94f,0.65f*fc+0.35f*cc));
                    if(fullHasUncertainty){
                        const float agreement=std::clamp(1.0f-residual/std::max(0.5f,threshold),0.0f,1.0f);
                        const float fused=0.62f*out.motionUncertainty[i]+0.38f*coarseUncertainty;
                        out.motionUncertainty[i]=std::clamp(fused*(1.0f-0.28f*agreement),0.0f,1.0f);
                    }
                    continue;
                }
                const float fullUncertainty=fullHasUncertainty ? std::clamp(full.motionUncertainty[i],0.0f,1.0f) : (1.0f-fc);
                // High confidence is only protective when independent uncertainty is also low.
                // This lets a coarse pyramid level repair periodic phase locks that NVOF itself
                // scored highly but cross-scale/temporal evidence considers ambiguous.
                if(fc>=highFullProtect && fullUncertainty<0.34f && mode!=NvofReliabilityMode::Strong) continue;
                if(mode==NvofReliabilityMode::Strong && fc>=highFullProtect && fullUncertainty<0.28f && cc<fc+0.06f) continue;
                const float severity=std::clamp((residual-threshold)/std::max(1.0f,threshold*2.0f),0.0f,1.0f);
                float blend=maxBlend*(0.25f+0.75f*severity)*(0.55f+0.45f*cc)*
                            (0.72f+0.28f*fullUncertainty);
                if(fc>0.75f) blend*=mode==NvofReliabilityMode::Strong ? 0.72f : 0.45f;
                if(blend<0.03f) continue;
                out.motionXY[i*2u]=fx+(cvx-fx)*blend;
                out.motionXY[i*2u+1u]=fy+(cvy-fy)*blend;
                // Do not make a corrected vector artificially perfect; later NR/FG branches
                // still need room to reject or refine it independently.
                out.confidence[i]=std::max(fc,std::min(0.90f,0.58f*fc+0.42f*cc));
                if(fullHasUncertainty){
                    // Cross-scale disagreement is itself uncertainty evidence even when the
                    // coarse candidate wins the correction vote. A correction must not become
                    // artificially "certain" just because it was applied.
                    const float prior=std::clamp(out.motionUncertainty[i],0.0f,1.0f);
                    out.motionUncertainty[i]=std::clamp(0.46f*prior+0.29f*coarseUncertainty+0.25f*severity,0.0f,1.0f);
                }
                corrected.fetch_add(1u,std::memory_order_relaxed);
                residualMilli.fetch_add(static_cast<uint64_t>(std::min(100000.0f,residual*1000.0f)),std::memory_order_relaxed);
            }
        }
    });
    const uint64_t count=corrected.load(std::memory_order_relaxed);
    const float thisFraction=fullN?static_cast<float>(count)/static_cast<float>(fullN):0.0f;
    const float thisResidual=count?static_cast<float>(residualMilli.load(std::memory_order_relaxed))/(1000.0f*static_cast<float>(count)):0.0f;
    // Sequential 1/2 + 1/4 fusion must not erase the diagnostics of the earlier scale.
    // Exact pixel-set union would require another full mask; this bounded union estimate is
    // sufficient for A/B telemetry and never decreases when another scale contributes.
    const float previousFraction=std::clamp(full.spatialConsensusCorrectedFraction,0.0f,1.0f);
    out.spatialConsensusCorrectedFraction=1.0f-(1.0f-previousFraction)*(1.0f-thisFraction);
    const float weightSum=previousFraction+thisFraction;
    out.spatialConsensusMeanResidual=weightSum>1e-6f ?
        (full.spatialConsensusMeanResidual*previousFraction + thisResidual*thisFraction)/weightSum : 0.0f;
    return out;
}

} // namespace video
