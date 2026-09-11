#pragma once
#include "ImageImport.h"
#include "ImageWic.h"
#include <filesystem>

namespace autodepth {

struct RuntimeStatus {
    bool pythonReady = false;
    bool helperReady = false;
    bool modelReady = false;
    std::filesystem::path python;
    std::filesystem::path helper;
    std::filesystem::path model;
};

RuntimeStatus CheckRuntime();
DepthMap Generate(const Rgba8Image& input, uint32_t inferenceSize = 518);
std::filesystem::path SetupScript();

}
