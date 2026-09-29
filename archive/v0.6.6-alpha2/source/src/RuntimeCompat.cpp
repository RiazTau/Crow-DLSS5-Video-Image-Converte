#include "RuntimeCompat.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace {
std::string Trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool ParseBool(const std::string& raw) {
    const auto v = Lower(Trim(raw));
    return v == "1" || v == "true" || v == "yes" || v == "on";
}
}

const char* RuntimeCallerModeName(RuntimeCallerMode mode) noexcept {
    switch (mode) {
    case RuntimeCallerMode::Direct: return "direct";
    case RuntimeCallerMode::LegacyHook: return "legacy_hook";
    }
    return "legacy_hook";
}

RuntimeCompatProfile LoadRuntimeCompatProfile(
    const std::filesystem::path& runtimeDll,
    std::optional<RuntimeCallerMode> callerOverride) {
    RuntimeCompatProfile result;
    result.configPath = runtimeDll.parent_path() / L"dlssnr-compat.ini";

    // The V0.6.1.2 production behavior is the hard default: if no compatibility
    // profile is present, the signed-snippet caller hook is used exactly as before.
    if (std::filesystem::exists(result.configPath)) {
        result.configPresent = true;
        std::ifstream in(result.configPath);
        if (!in) throw std::runtime_error("Unable to read runtime compatibility profile: " + result.configPath.string());
        std::string line;
        while (std::getline(in, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const auto key = Lower(Trim(line.substr(0, eq)));
            const auto value = Trim(line.substr(eq + 1));
            const auto lowerValue = Lower(value);
            if (key == "profile") {
                if (!value.empty() && value.size() <= 96) result.profile = value;
            } else if (key == "caller_mode") {
                if (lowerValue == "legacy_hook" || lowerValue == "hook") {
                    result.callerMode = RuntimeCallerMode::LegacyHook;
                } else if (lowerValue == "direct") {
                    result.callerMode = RuntimeCallerMode::Direct;
                } else {
                    throw std::runtime_error("Unsupported caller_mode in dlssnr-compat.ini: " + value);
                }
            } else if (key == "experimental") {
                result.experimental = ParseBool(value);
            }
        }
    }

    if (callerOverride) result.callerMode = *callerOverride;
    return result;
}
