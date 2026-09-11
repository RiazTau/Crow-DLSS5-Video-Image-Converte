#pragma once
#include <filesystem>
#include <optional>
#include <string>

enum class RuntimeCallerMode {
    LegacyHook,
    Direct,
};

struct RuntimeCompatProfile {
    std::string profile = "stable-rtx50";
    RuntimeCallerMode callerMode = RuntimeCallerMode::LegacyHook;
    bool experimental = false;
    bool configPresent = false;
    std::filesystem::path configPath;
};

RuntimeCompatProfile LoadRuntimeCompatProfile(
    const std::filesystem::path& runtimeDll,
    std::optional<RuntimeCallerMode> callerOverride = std::nullopt);

const char* RuntimeCallerModeName(RuntimeCallerMode mode) noexcept;
