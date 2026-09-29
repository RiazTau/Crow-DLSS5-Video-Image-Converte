#pragma once
#include "ExternalRenderData.h"
#include <Windows.h>
#include <filesystem>

namespace video {

// Modal editor for auxiliary EXR sequence mappings. Sequence paths are treated like
// input media and are not persisted by the main "Save Parameters" profile.
// sourceVideo is optional; when present it enables photometric motion auto-calibration.
bool EditExternalRenderDataSettings(HWND parent,
                                    ExternalRenderDataSettings& settings,
                                    const std::filesystem::path& sourceVideo = {});

} // namespace video
