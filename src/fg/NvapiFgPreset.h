#pragma once
#include "video/VideoConverter.h"
#include <string>

namespace fg {

struct FgPresetResult {
    bool ok = false;
    std::wstring message;
};

// Applies/removes a driver-level DLSS-FG model override for the Crow executable.
// DriverDefault removes Crow's two FG override settings from the application's actual DRS profile.
bool IsFgModelPresetSupported();
FgPresetResult ApplyFgModelPreset(video::FgModelPreset preset);

} // namespace fg
