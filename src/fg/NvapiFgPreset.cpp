#include "NvapiFgPreset.h"
#include <Windows.h>
#ifndef CROW_HAS_NVAPI
#define CROW_HAS_NVAPI 0
#endif
#if CROW_HAS_NVAPI
#include <nvapi.h>
#include <NvApiDriverSettings.h>
#endif
#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

namespace fg {

bool IsFgModelPresetSupported() {
#if CROW_HAS_NVAPI
    return true;
#else
    return false;
#endif
}

#if CROW_HAS_NVAPI
namespace {

std::wstring NvStatusText(NvAPI_Status status) {
    NvAPI_ShortString text{};
    if (NvAPI_GetErrorMessage(status, text) == NVAPI_OK) {
        const int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        if (n <= 0) return L"NVAPI status " + std::to_wstring(static_cast<int>(status));
        std::wstring out(static_cast<size_t>(n), L'\0');
        if (!MultiByteToWideChar(CP_ACP, 0, text, -1, out.data(), n))
            return L"NVAPI status " + std::to_wstring(static_cast<int>(status));
        if (!out.empty() && out.back() == L'\0') out.pop_back();
        return out;
    }
    return L"NVAPI status " + std::to_wstring(static_cast<int>(status));
}

void CopyNvString(NvAPI_UnicodeString& dst, const std::wstring& src) {
    std::fill(std::begin(dst), std::end(dst), 0);
    const size_t count = std::min(src.size(), std::size(dst) - 1);
    for (size_t i = 0; i < count; ++i) dst[i] = static_cast<NvU16>(src[i]);
}

std::wstring ExecutableName() {
    std::array<wchar_t, 32768> path{};
    const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) return L"Crow-DLSS-Rendering-Tool.exe";
    return std::filesystem::path(std::wstring(path.data(), n)).filename().wstring();
}

bool SetDword(NvDRSSessionHandle session, NvDRSProfileHandle profile, NvU32 id, NvU32 value, std::wstring& error) {
    NVDRS_SETTING setting{};
    setting.version = NVDRS_SETTING_VER;
    setting.settingId = id;
    setting.settingType = NVDRS_DWORD_TYPE;
    setting.u32CurrentValue = value;
    const auto st = NvAPI_DRS_SetSetting(session, profile, &setting);
    if (st != NVAPI_OK) { error = NvStatusText(st); return false; }
    return true;
}

NvDRSProfileHandle ResolveCrowProfile(NvDRSSessionHandle session, const std::wstring& exeName, std::wstring& error) {
    NvDRSProfileHandle profile = nullptr;
    NVDRS_APPLICATION app{};
    app.version = NVDRS_APPLICATION_VER;
    NvAPI_UnicodeString nvExe{};
    CopyNvString(nvExe, exeName);

    auto st = NvAPI_DRS_FindApplicationByName(session, nvExe, &profile, &app);
    if (st == NVAPI_OK && profile) return profile;
    if (st != NVAPI_EXECUTABLE_NOT_FOUND) { error = NvStatusText(st); return nullptr; }

    NVDRS_PROFILE profileInfo{};
    profileInfo.version = NVDRS_PROFILE_VER;
    CopyNvString(profileInfo.profileName, L"Crow - DLSS Rendering Tool");
    st = NvAPI_DRS_CreateProfile(session, &profileInfo, &profile);
    if (st != NVAPI_OK && st != NVAPI_PROFILE_NAME_IN_USE) { error = NvStatusText(st); return nullptr; }

    if (st == NVAPI_PROFILE_NAME_IN_USE) {
        NvAPI_UnicodeString profileName{};
        CopyNvString(profileName, L"Crow - DLSS Rendering Tool");
        st = NvAPI_DRS_FindProfileByName(session, profileName, &profile);
        if (st != NVAPI_OK || !profile) { error = NvStatusText(st); return nullptr; }
    }

    NVDRS_APPLICATION create{};
    create.version = NVDRS_APPLICATION_VER;
    CopyNvString(create.appName, exeName);
    CopyNvString(create.userFriendlyName, L"Crow - DLSS Rendering Tool");
    create.isPredefined = 0;
    st = NvAPI_DRS_CreateApplication(session, profile, &create);
    if (st == NVAPI_EXECUTABLE_ALREADY_IN_USE) {
        // The executable belongs to a different existing profile. Re-query and use the actual profile.
        profile = nullptr;
        NVDRS_APPLICATION existing{};
        existing.version = NVDRS_APPLICATION_VER;
        st = NvAPI_DRS_FindApplicationByName(session, nvExe, &profile, &existing);
    }
    if (st != NVAPI_OK || !profile) { error = NvStatusText(st); return nullptr; }
    return profile;
}

} // namespace
#endif

FgPresetResult ApplyFgModelPreset(video::FgModelPreset preset) {
#if !CROW_HAS_NVAPI
    FgPresetResult result;
    if (preset == video::FgModelPreset::DriverDefault) {
        result.ok = true;
        result.message = L"FG model preset: Driver Default (NVAPI SDK support not compiled in).";
    } else {
        result.ok = false;
        result.message = L"FG model presets A/B/Latest are unavailable because this build was compiled without the optional NVIDIA NVAPI SDK.";
    }
    return result;
#else
    FgPresetResult result;
    auto st = NvAPI_Initialize();
    if (st != NVAPI_OK) { result.message = L"NVAPI initialization failed: " + NvStatusText(st); return result; }

    NvDRSSessionHandle session = nullptr;
    st = NvAPI_DRS_CreateSession(&session);
    if (st != NVAPI_OK) { result.message = L"NVAPI DRS session failed: " + NvStatusText(st); return result; }
    struct SessionGuard { NvDRSSessionHandle h; ~SessionGuard(){ if(h) NvAPI_DRS_DestroySession(h); } } guard{session};

    st = NvAPI_DRS_LoadSettings(session);
    if (st != NVAPI_OK) { result.message = L"NVAPI DRS LoadSettings failed: " + NvStatusText(st); return result; }

    std::wstring error;
    const auto profile = ResolveCrowProfile(session, ExecutableName(), error);
    if (!profile) { result.message = L"Unable to resolve Crow NVIDIA profile: " + error; return result; }

    if (preset == video::FgModelPreset::DriverDefault) {
        // Delete only Crow's FG overrides. NVIDIA App / driver defaults remain authoritative.
        const auto a = NvAPI_DRS_DeleteProfileSetting(session, profile, NGX_DLSS_FG_OVERRIDE_ID);
        const auto b = NvAPI_DRS_DeleteProfileSetting(session, profile, NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID);
        if (a != NVAPI_OK && a != NVAPI_SETTING_NOT_FOUND) { result.message = L"Unable to clear FG override: " + NvStatusText(a); return result; }
        if (b != NVAPI_OK && b != NVAPI_SETTING_NOT_FOUND) { result.message = L"Unable to clear FG preset override: " + NvStatusText(b); return result; }
    } else {
        NvU32 presetValue = NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_A;
        if (preset == video::FgModelPreset::PresetB)
            presetValue = NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_B;
        else if (preset == video::FgModelPreset::Latest)
            presetValue = NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_RENDER_PRESET_Latest;
        if (!SetDword(session, profile, NGX_DLSS_FG_OVERRIDE_ID, NGX_DLSS_FG_OVERRIDE_ON, error) ||
            !SetDword(session, profile, NGX_DLSS_FG_OVERRIDE_RENDER_PRESET_SELECTION_ID, presetValue, error)) {
            result.message = L"Unable to apply FG model preset: " + error;
            return result;
        }
    }

    st = NvAPI_DRS_SaveSettings(session);
    if (st != NVAPI_OK) { result.message = L"NVAPI DRS SaveSettings failed: " + NvStatusText(st); return result; }
    result.ok = true;
    result.message = preset == video::FgModelPreset::DriverDefault
        ? L"FG model preset: Driver Default (Crow override removed)."
        : L"FG model preset override applied to the Crow NVIDIA profile.";
    return result;
#endif
}

} // namespace fg
