#include "video/TemporalMotionConsensus.h"
#include <cassert>
#include <cmath>
#include <iostream>

static video::TemporalFlowResult UniformFlow(uint32_t w, uint32_t h, float vx, float vy, float confidence) {
    video::TemporalFlowResult f;
    f.motionXY.resize(static_cast<size_t>(w) * h * 2u);
    f.confidence.assign(static_cast<size_t>(w) * h, confidence);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        f.motionXY[i * 2u + 0u] = vx;
        f.motionXY[i * 2u + 1u] = vy;
    }
    f.robustGlobalMotionX = vx;
    f.robustGlobalMotionY = vy;
    return f;
}

static float CenterX(const video::TemporalFlowResult& f, uint32_t w, uint32_t h) {
    const size_t i = static_cast<size_t>(h / 2u) * w + w / 2u;
    return f.motionXY[i * 2u + 0u];
}

int main() {
    constexpr uint32_t W = 64, H = 32;

    // Auto: one good previous interval plus the current interval already spans a
    // three-source-frame trajectory. A low/moderate-confidence one-frame phase slip
    // should be pulled toward the stable historical motion.
    video::TemporalMotionConsensus autoConsensus(W, H, video::NvofReliabilityMode::Auto, 4);
    auto stable = UniformFlow(W, H, -6.0f, 0.0f, 0.92f);
    auto first = autoConsensus.Stabilize(stable, nullptr, false);
    assert(first.temporalConsensusCorrectedFraction == 0.0f);
    auto slip = UniformFlow(W, H, -2.0f, 0.0f, 0.58f);
    slip.robustGlobalMotionX = -6.0f;
    auto repaired = autoConsensus.Stabilize(slip, nullptr, false);
    assert(repaired.temporalConsensusCorrectedFraction > 0.0f);
    assert(CenterX(repaired, W, H) < -2.4f); // moved toward -6, not left at the phase slip

    // Strong: build a stable multi-frame trajectory and then inject a high-confidence
    // periodic lock. Strong mode must still correct it because temporal history and global
    // motion agree against the locally plausible current vector.
    video::TemporalMotionConsensus strong(W, H, video::NvofReliabilityMode::Strong, 4);
    for (int i = 0; i < 3; ++i) {
        auto good = UniformFlow(W, H, -6.0f, 0.0f, 0.94f);
        strong.Stabilize(good, nullptr, false);
    }
    auto hardSlip = UniformFlow(W, H, -2.0f, 0.0f, 0.93f);
    hardSlip.robustGlobalMotionX = -6.0f;
    auto strongRepaired = strong.Stabilize(hardSlip, nullptr, false);
    assert(strongRepaired.temporalConsensusCorrectedFraction > 0.0f);
    assert(CenterX(strongRepaired, W, H) < -3.0f);
    assert(strongRepaired.temporalConsensusHistory >= 2u);

    // Smooth acceleration must not be flattened to constant velocity. With history
    // -2, -4, -6 px/frame, Strong predicts approximately -8 for the next interval.
    video::TemporalMotionConsensus accelerating(W, H, video::NvofReliabilityMode::Strong, 2);
    accelerating.Stabilize(UniformFlow(W, H, -2.0f, 0.0f, 0.95f), nullptr, false);
    accelerating.Stabilize(UniformFlow(W, H, -4.0f, 0.0f, 0.95f), nullptr, false);
    accelerating.Stabilize(UniformFlow(W, H, -6.0f, 0.0f, 0.95f), nullptr, false);
    auto accelCurrent = UniformFlow(W, H, -8.0f, 0.0f, 0.95f);
    auto accelOut = accelerating.Stabilize(accelCurrent, nullptr, false);
    assert(std::abs(CenterX(accelOut, W, H) + 8.0f) < 0.6f);


    // Alpha5: a newly revealed/disoccluded current surface has no valid temporal trajectory.
    // Even Strong mode must not drag it toward stale history.
    video::TemporalMotionConsensus visibilityAware(W, H, video::NvofReliabilityMode::Strong, 4);
    for (int i = 0; i < 3; ++i) {
        auto good = UniformFlow(W, H, -6.0f, 0.0f, 0.95f);
        good.historyVisibility.assign(static_cast<size_t>(W)*H, 1.0f);
        good.disocclusionProbability.assign(static_cast<size_t>(W)*H, 0.0f);
        good.motionUncertainty.assign(static_cast<size_t>(W)*H, 0.10f);
        visibilityAware.Stabilize(good, nullptr, false);
    }
    auto revealed = UniformFlow(W, H, -2.0f, 0.0f, 0.60f);
    revealed.historyVisibility.assign(static_cast<size_t>(W)*H, 0.0f);
    revealed.disocclusionProbability.assign(static_cast<size_t>(W)*H, 1.0f);
    revealed.motionUncertainty.assign(static_cast<size_t>(W)*H, 0.95f);
    auto revealedOut = visibilityAware.Stabilize(revealed, nullptr, false);
    assert(revealedOut.temporalConsensusCorrectedFraction == 0.0f);
    assert(std::abs(CenterX(revealedOut, W, H) + 2.0f) < 1e-4f);

    // Scene cut/reset must discard the trajectory so stale motion cannot leak into the
    // first interval of the new scene.
    auto resetOut = strong.Stabilize(hardSlip, nullptr, true);
    assert(resetOut.temporalConsensusCorrectedFraction == 0.0f);
    auto afterReset = strong.Stabilize(hardSlip, nullptr, false);
    assert(afterReset.temporalConsensusCorrectedFraction == 0.0f);

    std::cout << "PASS: trailing temporal consensus repairs phase slips, preserves smooth acceleration, resets on cuts\n";
}
