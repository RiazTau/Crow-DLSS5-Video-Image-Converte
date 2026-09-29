#pragma once
#include <filesystem>

namespace models {
struct ModelStatus {
    bool present = false;
    bool hashOk = false;
    std::filesystem::path path;
};

ModelStatus CheckDepthAnythingV2();
std::filesystem::path EnsureDepthAnythingV2();
const char* DepthAnythingV2Sha256();
const wchar_t* DepthAnythingV2Url();
}
