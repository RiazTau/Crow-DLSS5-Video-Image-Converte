#pragma once

#include "ImageWic.h"
#include "NvofConfig.h"
#include "TemporalFlow.h"
#include <cstdint>

namespace video {

// Anti-aliased spatial-pyramid sources used by the NVOF prior sessions.
Rgba8Image DownsampleHalfForMotion(const Rgba8Image& input);
Rgba8Image DownsampleQuarterForMotion(const Rgba8Image& input);

// Fuses a half-resolution NVOF result into the full-resolution NVOF candidate field.
// rawMotionXY from the full result is preserved so downstream NR can remain conservative.
TemporalFlowResult FuseCoarseNvofMotion(const TemporalFlowResult& full,
                                        const TemporalFlowResult& coarse,
                                        uint32_t fullWidth,
                                        uint32_t fullHeight,
                                        uint32_t coarseWidth,
                                        uint32_t coarseHeight,
                                        NvofReliabilityMode mode,
                                        float priorStrength = 1.0f);

} // namespace video
