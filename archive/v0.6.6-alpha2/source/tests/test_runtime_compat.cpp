#include "RuntimeCompat.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "dlss5_runtime_compat_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const auto dll = root / "nvngx_dlssnr.dll";
    { std::ofstream f(dll, std::ios::binary); f << "test"; }

    auto stable = LoadRuntimeCompatProfile(dll);
    if (stable.configPresent || stable.callerMode != RuntimeCallerMode::LegacyHook || stable.experimental) {
        std::cerr << "default profile changed stable behavior\n"; return 1;
    }

    { std::ofstream f(root / "dlssnr-compat.ini");
      f << "profile=rtx40-community\nexperimental=1\ncaller_mode=direct\n"; }
    auto experimental = LoadRuntimeCompatProfile(dll);
    if (!experimental.configPresent || experimental.profile != "rtx40-community" ||
        experimental.callerMode != RuntimeCallerMode::Direct || !experimental.experimental) {
        std::cerr << "experimental profile parse failed\n"; return 2;
    }

    auto overridden = LoadRuntimeCompatProfile(dll, RuntimeCallerMode::LegacyHook);
    if (overridden.callerMode != RuntimeCallerMode::LegacyHook) {
        std::cerr << "override failed\n"; return 3;
    }

    { std::ofstream f(root / "dlssnr-compat.ini"); f << "caller_mode=unsafe_magic\n"; }
    try { (void)LoadRuntimeCompatProfile(dll); }
    catch (const std::exception&) { fs::remove_all(root); std::cout << "PASS: runtime compat default/opt-in/override contract\n"; return 0; }
    std::cerr << "invalid caller mode was accepted\n";
    fs::remove_all(root);
    return 4;
}
