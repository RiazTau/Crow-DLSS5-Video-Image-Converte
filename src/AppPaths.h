#pragma once
#include <filesystem>

namespace app {
std::filesystem::path ExecutablePath();
std::filesystem::path ExecutableDir();
std::filesystem::path DefaultRuntimeDll();
std::filesystem::path DefaultDepthModel();
}
