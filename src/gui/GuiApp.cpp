#include "GuiApp.h"
#include "../AppPaths.h"
#include "../AutoDepthRunner.h"
#include "../D3D12Context.h"
#include "../DlssNrRunner.h"
#include "../ImageImport.h"
#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

#pragma comment(lib, "Comctl32.lib")

namespace {
constexpr wchar_t MAIN_CLASS[] = L"CrowDLSS5VideoImageConverterMain";
constexpr wchar_t PREVIEW_CLASS[] = L"DLSS5FilterPreview";
constexpr wchar_t DEPTH_PREVIEW_CLASS[] = L"DLSS5FilterDepthPreview";
constexpr UINT WM_PROCESS_DONE = WM_APP + 100;
constexpr int SIDEBAR_W = 470;

enum ControlId : int {
    IDC_SOURCE_PATH = 100,
    IDC_SOURCE_BROWSE,
    IDC_EXR_LAYER,
    IDC_EXPOSURE,
    IDC_TONEMAP,
    IDC_SRGB,
    IDC_APPLY_IMPORT,
    IDC_DEPTH_MODE,
    IDC_AUTO_DEPTH_SETUP,
    IDC_AUTO_DEPTH_GENERATE,
    IDC_DEPTH_PATH,
    IDC_DEPTH_BROWSE,
    IDC_DEPTH_CLEAR,
    IDC_DEPTH_LAYER,
    IDC_DEPTH_CHANNEL,
    IDC_DEPTH_NORMALIZE,
    IDC_DEPTH_INVERSE,
    IDC_DEPTH_SCALE,
    IDC_DEPTH_OFFSET,
    IDC_NEURAL_UPLIFT,
    IDC_PRESET,
    IDC_STYLE,
    IDC_INTENSITY,
    IDC_INTENSITY_VALUE,
    IDC_LOCAL_TONE,
    IDC_LOCAL_TONE_VALUE,
    IDC_LOCAL_STRUCTURE,
    IDC_LOCAL_STRUCTURE_VALUE,
    IDC_SKIN_STRUCTURE,
    IDC_SKIN_STRUCTURE_VALUE,
    IDC_AUTO_MASK,
    IDC_UI_CORRECTION,
    IDC_ITERATIONS,
    IDC_RUNTIME_PATH,
    IDC_RUNTIME_BROWSE,
    IDC_SAVE_PARAMETERS,
    IDC_PROCESS,
    IDC_SAVE,
    IDC_STATUS,
    IDC_PREVIEW,

    IDC_LBL_SOURCE_TITLE = 2000,
    IDC_LBL_SOURCE_EXR,
    IDC_LBL_EXPOSURE,
    IDC_LBL_TONEMAP,
    IDC_LBL_DEPTH_TITLE,
    IDC_LBL_DEPTH_MODE,
    IDC_LBL_DEPTH_EXR,
    IDC_LBL_DEPTH_CHANNEL,
    IDC_LBL_DEPTH_SCALE,
    IDC_LBL_DEPTH_OFFSET,
    IDC_LBL_DLSS_TITLE,
    IDC_LBL_PRESET,
    IDC_LBL_STYLE,
    IDC_LBL_INTENSITY,
    IDC_LBL_LOCAL_TONE,
    IDC_LBL_LOCAL_STRUCTURE,
    IDC_LBL_SKIN_STRUCTURE,
    IDC_LBL_ITERATIONS,
};



enum class ImageParameterKind { Edit, Combo, Check, Track };
struct ImageParameterSpec {
    int id;
    ImageParameterKind kind;
    const wchar_t* key;
    const wchar_t* factoryValue;
    int comboMax = 0;
    float trackMin = 0.0f;
    float trackMax = 1.0f;
};

static constexpr ImageParameterSpec kImageParameterSpecs[] = {
    {IDC_EXPOSURE, ImageParameterKind::Edit, L"exposure_ev", L"0.0"},
    {IDC_TONEMAP, ImageParameterKind::Combo, L"tone_map", L"2", 2},
    {IDC_SRGB, ImageParameterKind::Check, L"srgb_encode", L"1"},
    {IDC_DEPTH_MODE, ImageParameterKind::Combo, L"depth_mode", L"1", 2},
    {IDC_DEPTH_NORMALIZE, ImageParameterKind::Check, L"depth_auto_normalize", L"1"},
    {IDC_DEPTH_INVERSE, ImageParameterKind::Check, L"depth_inverse", L"1"},
    {IDC_DEPTH_SCALE, ImageParameterKind::Edit, L"depth_scale", L"1.0"},
    {IDC_DEPTH_OFFSET, ImageParameterKind::Edit, L"depth_offset", L"0.0"},
    {IDC_NEURAL_UPLIFT, ImageParameterKind::Check, L"neural_uplift", L"1"},
    {IDC_PRESET, ImageParameterKind::Combo, L"nr_preset", L"2", 3},
    {IDC_STYLE, ImageParameterKind::Combo, L"nr_style", L"2", 2},
    {IDC_INTENSITY, ImageParameterKind::Track, L"nr_intensity", L"0.85", 0, 0.0f, 2.0f},
    {IDC_LOCAL_TONE, ImageParameterKind::Track, L"nr_local_tone", L"1.0", 0, 0.0f, 2.0f},
    {IDC_LOCAL_STRUCTURE, ImageParameterKind::Track, L"nr_local_structure", L"1.0", 0, 0.0f, 2.0f},
    {IDC_SKIN_STRUCTURE, ImageParameterKind::Track, L"nr_skin_structure", L"-0.5", 0, -1.0f, 2.0f},
    {IDC_AUTO_MASK, ImageParameterKind::Check, L"nr_auto_skin_mask", L"1"},
    {IDC_UI_CORRECTION, ImageParameterKind::Check, L"nr_ui_correction", L"1"},
    {IDC_ITERATIONS, ImageParameterKind::Combo, L"iterations", L"0", 4},
};

constexpr int IMAGE_RESET_BUTTON_BASE = 6000;
constexpr int ImageResetButtonId(int parameterId) { return IMAGE_RESET_BUTTON_BASE + parameterId; }

void UpdateDepthModeUi(struct State* s);

struct ViewportState {
    double zoom = 1.0;          // 1.0 = fit entire image in pane
    double panX = 0.0;          // screen-space pan from pane centre
    double panY = 0.0;
    bool dragging = false;
    POINT lastMouse{};
};

struct State {
    HWND hwnd = nullptr;
    HWND preview = nullptr;
    HWND depthPreview = nullptr;
    std::filesystem::path sourcePath;
    std::filesystem::path depthPath;
    std::filesystem::path runtimePath = app::DefaultRuntimeDll();
    ImportedImage source;
    Rgba8Image output;
    DepthMap activeDepth;
    Rgba8Image depthPreviewImage;
    std::vector<uint8_t> sourceBgra;
    std::vector<uint8_t> outputBgra;
    std::vector<uint8_t> depthBgra;
    bool hasSource = false;
    bool hasOutput = false;
    bool hasDepthPreview = false;
    bool busy = false;
    bool neuralUplift = true;
    int depthMode = 0; // 0 zero, 1 auto DAV2, 2 manual
    enum class WorkerTask { None, Process, AutoDepth };
    WorkerTask workerTask = WorkerTask::None;
    std::mutex workerMutex;
    std::string workerError;
    Rgba8Image workerOutput;
    DepthMap workerDepth;
    bool workerHasDepth = false;
    std::thread worker;

    // Original and DLSS5 stay synchronized so the same detail is compared
    // at the same zoom/pan. Depth has its own independent viewport.
    ViewportState originalView;
    ViewportState outputView;
    ViewportState depthView;
};

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring GetText(HWND parent, int id) {
    HWND h = GetDlgItem(parent, id);
    const int n = GetWindowTextLengthW(h);
    std::wstring out(static_cast<size_t>(n) + 1u, L'\0');
    if (n) GetWindowTextW(h, out.data(), n + 1);
    out.resize(static_cast<size_t>(n));
    return out;
}
void SetText(HWND parent, int id, const std::wstring& text) { SetWindowTextW(GetDlgItem(parent, id), text.c_str()); }
void SetStatus(State* s, const std::wstring& text) { SetText(s->hwnd, IDC_STATUS, text); }

float ReadFloat(HWND hwnd, int id, float fallback) {
    const auto text = GetText(hwnd, id);
    if (text.empty()) return fallback;
    wchar_t* end = nullptr;
    const float v = wcstof(text.c_str(), &end);
    return end && *end == L'\0' ? v : fallback;
}

std::vector<uint8_t> ToBgra(const Rgba8Image& image) {
    std::vector<uint8_t> out(image.pixels.size());
    for (size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
        out[i + 0] = image.pixels[i + 2];
        out[i + 1] = image.pixels[i + 1];
        out[i + 2] = image.pixels[i + 0];
        out[i + 3] = image.pixels[i + 3];
    }
    return out;
}

Rgba8Image DepthToPreviewImage(const DepthMap& depth) {
    Rgba8Image out;
    out.width = depth.width;
    out.height = depth.height;
    if (!out.width || !out.height || depth.values.empty()) return out;
    out.pixels.resize(static_cast<size_t>(out.width) * out.height * 4u);
    for (size_t i = 0; i < depth.values.size(); ++i) {
        float v = depth.values[i];
        if (!std::isfinite(v)) v = 0.0f;
        v = std::clamp(v, 0.0f, 1.0f);
        const uint8_t g = static_cast<uint8_t>(std::lround(v * 255.0f));
        const size_t p = i * 4u;
        out.pixels[p + 0] = g; out.pixels[p + 1] = g; out.pixels[p + 2] = g; out.pixels[p + 3] = 255;
    }
    return out;
}

void SetDepthPreview(State* s, DepthMap depth) {
    s->activeDepth = std::move(depth);
    s->depthView.zoom = 1.0;
    s->depthView.panX = 0.0;
    s->depthView.panY = 0.0;
    s->depthView.dragging = false;
    s->depthPreviewImage = DepthToPreviewImage(s->activeDepth);
    s->depthBgra = ToBgra(s->depthPreviewImage);
    s->hasDepthPreview = !s->depthPreviewImage.pixels.empty();
    if (s->depthPreview) InvalidateRect(s->depthPreview, nullptr, TRUE);
}

void ClearDepthPreview(State* s) {
    s->activeDepth = {};
    s->depthView = {};
    s->depthPreviewImage = {};
    s->depthBgra.clear();
    s->hasDepthPreview = false;
    if (s->depthPreview) InvalidateRect(s->depthPreview, nullptr, TRUE);
}

void ComboReset(HWND combo, const std::vector<std::wstring>& entries, int selection = 0) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (const auto& e : entries) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(e.c_str()));
    }
    if (!entries.empty()) {
        SendMessageW(combo, CB_SETCURSEL,
                     std::clamp(selection, 0, static_cast<int>(entries.size()) - 1), 0);
        // Ask the native ComboBox to keep all short option lists visible when dropped.
        // The actual drop-list height is also preserved by MoveCombo() in Layout().
#ifdef CB_SETMINVISIBLE
        SendMessageW(combo, CB_SETMINVISIBLE,
                     static_cast<WPARAM>(std::min<size_t>(entries.size(), 12u)), 0);
#endif
    }
}

std::string SelectedComboUtf8(HWND parent, int id) {
    HWND h = GetDlgItem(parent, id);
    const int sel = static_cast<int>(SendMessageW(h, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return {};
    const int len = static_cast<int>(SendMessageW(h, CB_GETLBTEXTLEN, sel, 0));
    std::wstring w(static_cast<size_t>(len) + 1u, L'\0');
    SendMessageW(h, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(w.data()));
    w.resize(static_cast<size_t>(len));
    return Narrow(w);
}

std::optional<std::filesystem::path> OpenFile(HWND owner, bool depth) {
    wchar_t file[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.lpstrFilter = depth
        ? L"Depth/Image Files\0*.exr;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0OpenEXR\0*.exr\0All Files\0*.*\0"
        : L"Image Files\0*.exr;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0OpenEXR\0*.exr\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&ofn) ? std::optional<std::filesystem::path>(file) : std::nullopt;
}

std::optional<std::filesystem::path> OpenRuntimeFile(HWND owner) {
    wchar_t file[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.lpstrFilter = L"NVIDIA DLSS Neural Runtime\0nvngx_dlssnr.dll\0DLL Files\0*.dll\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&ofn) ? std::optional<std::filesystem::path>(file) : std::nullopt;
}

std::optional<std::filesystem::path> SaveFile(HWND owner, const std::filesystem::path& source) {
    wchar_t file[32768]{};
    const auto stem = source.stem().wstring() + L"_DLSS5.png";
    const auto initialDir = source.parent_path().wstring();
    wcsncpy_s(file, stem.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = file;
    ofn.nMaxFile = ARRAYSIZE(file);
    ofn.lpstrFilter = L"PNG Image\0*.png\0OpenEXR (SDR-encoded float)\0*.exr\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetSaveFileNameW(&ofn) ? std::optional<std::filesystem::path>(file) : std::nullopt;
}

ImageImportSettings ReadImportSettings(State* s) {
    ImageImportSettings v;
    v.exrLayer = SelectedComboUtf8(s->hwnd, IDC_EXR_LAYER);
    v.exposureEv = ReadFloat(s->hwnd, IDC_EXPOSURE, 0.0f);
    const int t = static_cast<int>(SendMessageW(GetDlgItem(s->hwnd, IDC_TONEMAP), CB_GETCURSEL, 0, 0));
    v.toneMap = static_cast<ExrToneMap>(std::clamp(t, 0, 2));
    v.srgbEncode = Button_GetCheck(GetDlgItem(s->hwnd, IDC_SRGB)) == BST_CHECKED;
    return v;
}

DepthImportSettings ReadDepthSettings(State* s) {
    DepthImportSettings v;
    v.exrLayer = SelectedComboUtf8(s->hwnd, IDC_DEPTH_LAYER);
    int ch = static_cast<int>(SendMessageW(GetDlgItem(s->hwnd, IDC_DEPTH_CHANNEL), CB_GETCURSEL, 0, 0));
    if (!s->depthPath.empty() && IsExrPath(s->depthPath)) {
        v.exrChannel = SelectedComboUtf8(s->hwnd, IDC_DEPTH_CHANNEL);
        v.channel = 0;
    } else {
        v.channel = ch == CB_ERR ? 0 : ch;
    }
    v.autoNormalize = Button_GetCheck(GetDlgItem(s->hwnd, IDC_DEPTH_NORMALIZE)) == BST_CHECKED;
    v.inverseDepth = Button_GetCheck(GetDlgItem(s->hwnd, IDC_DEPTH_INVERSE)) == BST_CHECKED;
    v.scale = ReadFloat(s->hwnd, IDC_DEPTH_SCALE, 1.0f);
    v.offset = ReadFloat(s->hwnd, IDC_DEPTH_OFFSET, 0.0f);
    return v;
}

float TrackValue(HWND hwnd, int id, float minValue, float maxValue) {
    const int p = static_cast<int>(SendMessageW(GetDlgItem(hwnd, id), TBM_GETPOS, 0, 0));
    return minValue + (maxValue - minValue) * (static_cast<float>(p) / 1000.0f);
}
void SetTrack(HWND hwnd, int id, float value, float minValue, float maxValue) {
    const float t = (std::clamp(value, minValue, maxValue) - minValue) / (maxValue - minValue);
    SendMessageW(GetDlgItem(hwnd, id), TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(t * 1000.0f)));
}
void UpdateSliderLabels(HWND hwnd) {
    wchar_t b[32];
    swprintf_s(b, L"%.2f", TrackValue(hwnd, IDC_INTENSITY, 0.0f, 2.0f)); SetText(hwnd, IDC_INTENSITY_VALUE, b);
    swprintf_s(b, L"%.2f", TrackValue(hwnd, IDC_LOCAL_TONE, 0.0f, 2.0f)); SetText(hwnd, IDC_LOCAL_TONE_VALUE, b);
    swprintf_s(b, L"%.2f", TrackValue(hwnd, IDC_LOCAL_STRUCTURE, 0.0f, 2.0f)); SetText(hwnd, IDC_LOCAL_STRUCTURE_VALUE, b);
    swprintf_s(b, L"%.2f", TrackValue(hwnd, IDC_SKIN_STRUCTURE, -1.0f, 2.0f)); SetText(hwnd, IDC_SKIN_STRUCTURE_VALUE, b);
}

const ImageParameterSpec* FindImageParameterSpec(int id) {
    for (const auto& spec : kImageParameterSpecs) if (spec.id == id) return &spec;
    return nullptr;
}

std::filesystem::path ImageParameterSettingsPath() {
    return app::ExecutableDir() / L"image" / L"image-parameters.ini";
}

void ApplyImageParameterValue(HWND hwnd, const ImageParameterSpec& spec, const std::wstring& value) {
    try {
        switch (spec.kind) {
        case ImageParameterKind::Edit:
            SetText(hwnd, spec.id, value);
            break;
        case ImageParameterKind::Combo: {
            const int selection = std::clamp(std::stoi(value), 0, spec.comboMax);
            SendMessageW(GetDlgItem(hwnd, spec.id), CB_SETCURSEL, selection, 0);
            break;
        }
        case ImageParameterKind::Check:
            Button_SetCheck(GetDlgItem(hwnd, spec.id), std::stoi(value) != 0 ? BST_CHECKED : BST_UNCHECKED);
            break;
        case ImageParameterKind::Track:
            SetTrack(hwnd, spec.id, std::stof(value), spec.trackMin, spec.trackMax);
            break;
        }
    } catch (...) {
        ApplyImageParameterValue(hwnd, spec, spec.factoryValue);
    }
}

std::wstring ReadImageParameterValue(HWND hwnd, const ImageParameterSpec& spec) {
    switch (spec.kind) {
    case ImageParameterKind::Edit:
        return GetText(hwnd, spec.id);
    case ImageParameterKind::Combo: {
        const int fallback = std::stoi(spec.factoryValue);
        const int selection = static_cast<int>(SendMessageW(GetDlgItem(hwnd, spec.id), CB_GETCURSEL, 0, 0));
        return std::to_wstring(selection == CB_ERR ? fallback : std::clamp(selection, 0, spec.comboMax));
    }
    case ImageParameterKind::Check:
        return Button_GetCheck(GetDlgItem(hwnd, spec.id)) == BST_CHECKED ? L"1" : L"0";
    case ImageParameterKind::Track:
        return std::to_wstring(TrackValue(hwnd, spec.id, spec.trackMin, spec.trackMax));
    }
    return {};
}

bool LoadSavedImageParameters(State* s) {
    const auto path = ImageParameterSettingsPath();
    if (!std::filesystem::exists(path)) return false;
    constexpr wchar_t section[] = L"ImageParameters";
    constexpr wchar_t missing[] = L"{DLSS5_MISSING_VALUE}";
    wchar_t buffer[512]{};
    for (const auto& spec : kImageParameterSpecs) {
        buffer[0] = L'\0';
        GetPrivateProfileStringW(section, spec.key, missing, buffer, ARRAYSIZE(buffer), path.c_str());
        if (std::wstring(buffer) == missing) continue;
        ApplyImageParameterValue(s->hwnd, spec, buffer);
    }
    UpdateSliderLabels(s->hwnd);
    return true;
}

void SaveImageParameters(State* s) {
    const auto path = ImageParameterSettingsPath();
    try {
        std::filesystem::create_directories(path.parent_path());
        constexpr wchar_t section[] = L"ImageParameters";
        if (!WritePrivateProfileStringW(section, L"schema", L"1", path.c_str()))
            throw std::runtime_error("Unable to create the image parameter profile.");
        for (const auto& spec : kImageParameterSpecs) {
            const auto value = ReadImageParameterValue(s->hwnd, spec);
            if (!WritePrivateProfileStringW(section, spec.key, value.c_str(), path.c_str()))
                throw std::runtime_error("Unable to write an image parameter profile entry.");
        }
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
        SetStatus(s, L"Image parameters saved. Source/depth file paths, EXR layer/channel selections and runtime DLL are input-specific and are not persisted.");
        MessageBoxW(s->hwnd, (L"Image parameters saved to:\n" + path.wstring()).c_str(), L"Crow-DLSS5-Video-Image-Converter", MB_OK | MB_ICONINFORMATION);
    } catch (const std::exception& e) {
        MessageBoxW(s->hwnd, Wide(e.what()).c_str(), L"Save image parameters failed", MB_ICONERROR);
    }
}

void ResetOneImageParameter(State* s, int parameterId) {
    const auto* spec = FindImageParameterSpec(parameterId);
    if (!spec) return;
    ApplyImageParameterValue(s->hwnd, *spec, spec->factoryValue);
    UpdateSliderLabels(s->hwnd);
    if (parameterId == IDC_DEPTH_MODE) UpdateDepthModeUi(s);
    SetStatus(s, L"Factory default restored for this image parameter. Click Save Parameters to persist it.");
}

DlssNrSettings ReadDlssSettings(State* s) {
    DlssNrSettings v;
    int preset = static_cast<int>(SendMessageW(GetDlgItem(s->hwnd, IDC_PRESET), CB_GETCURSEL, 0, 0));
    int styleSelection = static_cast<int>(SendMessageW(GetDlgItem(s->hwnd, IDC_STYLE), CB_GETCURSEL, 0, 0));
    v.preset = preset == CB_ERR ? 2 : preset;
    // UI order is Default, Natural, Cinematic. The currently validated RenoDX/Feature-18
    // raw mapping is 0=Default, 1=Cinematic, 2=Natural, so translate explicitly.
    static constexpr int kStyleRawByUi[] = {0, 2, 1};
    const int styleIndex = styleSelection == CB_ERR ? 2 : std::clamp(styleSelection, 0, 2);
    v.style = kStyleRawByUi[styleIndex];
    v.intensity = TrackValue(s->hwnd, IDC_INTENSITY, 0.0f, 2.0f);
    v.localToneStrength = TrackValue(s->hwnd, IDC_LOCAL_TONE, 0.0f, 2.0f);
    v.localStructureStrength = TrackValue(s->hwnd, IDC_LOCAL_STRUCTURE, 0.0f, 2.0f);
    v.skinStructureStrength = TrackValue(s->hwnd, IDC_SKIN_STRUCTURE, -1.0f, 2.0f);
    v.autoMask = Button_GetCheck(GetDlgItem(s->hwnd, IDC_AUTO_MASK)) == BST_CHECKED;
    v.uiCorrection = Button_GetCheck(GetDlgItem(s->hwnd, IDC_UI_CORRECTION)) == BST_CHECKED;
    const int itSel = static_cast<int>(SendMessageW(GetDlgItem(s->hwnd, IDC_ITERATIONS), CB_GETCURSEL, 0, 0));
    static const uint32_t its[] = {1, 2, 4, 8, 16};
    v.iterations = its[std::clamp(itSel, 0, 4)];
    return v;
}

void PopulateExrLayers(HWND combo, const std::filesystem::path& path, bool preferCombined) {
    std::vector<std::wstring> entries;
    int selected = 0;
    try {
        const auto layers = ListExrLayers(path);
        for (size_t i = 0; i < layers.size(); ++i) {
            entries.push_back(Wide(layers[i]));
            if (preferCombined && (layers[i].find("Combined") != std::string::npos || layers[i].find("combined") != std::string::npos)) selected = static_cast<int>(i);
        }
    } catch (...) {}
    if (entries.empty()) entries.push_back(L"");
    ComboReset(combo, entries, selected);
}


void PopulateDepthChannels(State* s) {
    HWND combo = GetDlgItem(s->hwnd, IDC_DEPTH_CHANNEL);
    if (s->depthPath.empty() || !IsExrPath(s->depthPath)) {
        ComboReset(combo, {L"R", L"G", L"B", L"A", L"Luminance"}, 0);
        return;
    }
    std::vector<std::wstring> entries;
    int selected = 0;
    try {
        const auto channels = ListExrChannels(s->depthPath);
        const std::string layer = SelectedComboUtf8(s->hwnd, IDC_DEPTH_LAYER);
        const std::string prefix = layer.empty() ? std::string{} : layer + ".";
        for (const auto& channel : channels) {
            if (!prefix.empty() && channel.rfind(prefix, 0) != 0) continue;
            if (channel.ends_with(".Z") || channel == "Z" || channel.find("Depth") != std::string::npos) {
                selected = static_cast<int>(entries.size());
            }
            entries.push_back(Wide(channel));
        }
        // Some EXRs expose a layer name that does not map cleanly to a prefix. Fall back to all channels.
        if (entries.empty()) {
            for (const auto& channel : channels) {
                if (channel.ends_with(".Z") || channel == "Z" || channel.find("Depth") != std::string::npos) {
                    selected = static_cast<int>(entries.size());
                }
                entries.push_back(Wide(channel));
            }
        }
    } catch (...) {}
    if (entries.empty()) entries.push_back(L"");
    ComboReset(combo, entries, selected);
}

void ReloadSource(State* s) {
    if (s->sourcePath.empty()) return;
    const auto settings = ReadImportSettings(s);
    s->source = LoadImageForDlss(s->sourcePath, settings);
    s->sourceBgra = ToBgra(s->source.display);
    s->hasSource = true;
    s->originalView = {};
    s->outputView = {};
    s->hasOutput = false;
    s->output = {};
    s->outputBgra.clear();
    ClearDepthPreview(s);
    wchar_t status[256];
    swprintf_s(status, L"Source ready: %ux%u%s", s->source.display.width, s->source.display.height,
               s->source.fromExr ? L" | EXR -> SDR import" : L"");
    SetStatus(s, status);
    InvalidateRect(s->preview, nullptr, TRUE);
}

RECT FitRect(const RECT& client, uint32_t w, uint32_t h) {
    RECT r = client;
    const int cw = std::max(1, static_cast<int>(client.right - client.left) - 24);
    const int ch = std::max(1, static_cast<int>(client.bottom - client.top) - 24);
    const double scale = std::min(static_cast<double>(cw) / std::max(1u, w), static_cast<double>(ch) / std::max(1u, h));
    const int dw = std::max(1, static_cast<int>(std::lround(w * scale)));
    const int dh = std::max(1, static_cast<int>(std::lround(h * scale)));
    r.left = static_cast<LONG>(static_cast<int>(client.left) + (static_cast<int>(client.right - client.left) - dw) / 2);
    r.top = static_cast<LONG>(static_cast<int>(client.top) + (static_cast<int>(client.bottom - client.top) - dh) / 2);
    r.right = r.left + dw;
    r.bottom = r.top + dh;
    return r;
}

void GetMainPreviewPanes(HWND hwnd, RECT& leftPane, RECT& rightPane, RECT* leftLabel = nullptr, RECT* rightLabel = nullptr) {
    RECT c{}; GetClientRect(hwnd, &c);
    const int clientW = std::max(1, static_cast<int>(c.right - c.left));
    const int clientH = std::max(1, static_cast<int>(c.bottom - c.top));
    const int gap = 10;
    const int labelH = std::min(30, std::max(24, clientH / 20));
    const int paneW = std::max(1, (clientW - gap) / 2);
    leftPane = RECT{0, labelH, paneW, clientH};
    rightPane = RECT{paneW + gap, labelH, clientW, clientH};
    if (leftLabel) *leftLabel = RECT{0, 2, paneW, labelH};
    if (rightLabel) *rightLabel = RECT{paneW + gap, 2, clientW, labelH};
}

RECT ZoomedImageRect(const RECT& pane, uint32_t imageW, uint32_t imageH, const ViewportState& view) {
    const RECT fit = FitRect(pane, imageW, imageH);
    const int fitW = std::max(1, static_cast<int>(fit.right - fit.left));
    const int fitH = std::max(1, static_cast<int>(fit.bottom - fit.top));
    const int dw = std::max(1, static_cast<int>(std::lround(fitW * view.zoom)));
    const int dh = std::max(1, static_cast<int>(std::lround(fitH * view.zoom)));
    const double cx = (static_cast<double>(pane.left) + pane.right) * 0.5 + view.panX;
    const double cy = (static_cast<double>(pane.top) + pane.bottom) * 0.5 + view.panY;
    RECT r{};
    r.left = static_cast<LONG>(std::lround(cx - dw * 0.5));
    r.top = static_cast<LONG>(std::lround(cy - dh * 0.5));
    r.right = r.left + dw;
    r.bottom = r.top + dh;
    return r;
}

void ClampViewportPan(ViewportState& view, const RECT& pane, uint32_t imageW, uint32_t imageH) {
    if (view.zoom <= 1.00001) {
        view.zoom = 1.0;
        view.panX = 0.0;
        view.panY = 0.0;
        return;
    }
    const RECT fit = FitRect(pane, imageW, imageH);
    const double dw = std::max(1.0, static_cast<double>(fit.right - fit.left) * view.zoom);
    const double dh = std::max(1.0, static_cast<double>(fit.bottom - fit.top) * view.zoom);
    const double pw = std::max(1.0, static_cast<double>(pane.right - pane.left));
    const double ph = std::max(1.0, static_cast<double>(pane.bottom - pane.top));
    const double maxX = std::max(0.0, (dw - pw) * 0.5);
    const double maxY = std::max(0.0, (dh - ph) * 0.5);
    view.panX = std::clamp(view.panX, -maxX, maxX);
    view.panY = std::clamp(view.panY, -maxY, maxY);
}

void ZoomViewportAt(ViewportState& view, const RECT& pane, uint32_t imageW, uint32_t imageH, POINT point, int wheelDelta) {
    if (!imageW || !imageH || !PtInRect(&pane, point)) return;
    const RECT oldRect = ZoomedImageRect(pane, imageW, imageH, view);
    const double oldW = std::max(1.0, static_cast<double>(oldRect.right - oldRect.left));
    const double oldH = std::max(1.0, static_cast<double>(oldRect.bottom - oldRect.top));
    const double u = std::clamp((static_cast<double>(point.x) - oldRect.left) / oldW, 0.0, 1.0);
    const double v = std::clamp((static_cast<double>(point.y) - oldRect.top) / oldH, 0.0, 1.0);

    const double steps = static_cast<double>(wheelDelta) / WHEEL_DELTA;
    const double newZoom = std::clamp(view.zoom * std::pow(1.18, steps), 1.0, 20.0);
    if (std::abs(newZoom - view.zoom) < 1e-9) return;
    view.zoom = newZoom;

    const RECT fit = FitRect(pane, imageW, imageH);
    const double newW = std::max(1.0, static_cast<double>(fit.right - fit.left) * view.zoom);
    const double newH = std::max(1.0, static_cast<double>(fit.bottom - fit.top) * view.zoom);
    const double paneCx = (static_cast<double>(pane.left) + pane.right) * 0.5;
    const double paneCy = (static_cast<double>(pane.top) + pane.bottom) * 0.5;
    const double desiredLeft = static_cast<double>(point.x) - u * newW;
    const double desiredTop = static_cast<double>(point.y) - v * newH;
    view.panX = desiredLeft - (paneCx - newW * 0.5);
    view.panY = desiredTop - (paneCy - newH * 0.5);
    ClampViewportPan(view, pane, imageW, imageH);
}

void PanViewport(ViewportState& view, const RECT& pane, uint32_t imageW, uint32_t imageH, int dx, int dy) {
    if (view.zoom <= 1.0) return;
    view.panX += dx;
    view.panY += dy;
    ClampViewportPan(view, pane, imageW, imageH);
}

void ResetViewport(ViewportState& view) {
    view.zoom = 1.0;
    view.panX = 0.0;
    view.panY = 0.0;
    view.dragging = false;
}

void DrawBgra(HDC dc, const RECT& dst, const Rgba8Image& image, const std::vector<uint8_t>& bgra) {
    if (bgra.empty() || !image.width || !image.height) return;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(image.width);
    bi.bmiHeader.biHeight = -static_cast<LONG>(image.height);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc, dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top,
                  0, 0, image.width, image.height, bgra.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
}

void DrawClippedImage(HDC dc, const RECT& pane, const Rgba8Image& image,
                      const std::vector<uint8_t>& bgra, const ViewportState& view) {
    if (bgra.empty() || !image.width || !image.height) return;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, pane.left, pane.top, pane.right, pane.bottom);
    const RECT dst = ZoomedImageRect(pane, image.width, image.height, view);
    DrawBgra(dc, dst, image, bgra);
    RestoreDC(dc, saved);
}

void DrawZoomHint(HDC dc, const RECT& label, const ViewportState& view, const wchar_t* title) {
    wchar_t text[128]{};
    if (view.zoom <= 1.00001) {
        swprintf_s(text, L"%s  |  Fit  |  Wheel: Zoom  Drag: Pan  Double-click: Fit", title);
    } else {
        swprintf_s(text, L"%s  |  %.2fx  |  Wheel: Zoom  Drag: Pan  Double-click: Fit", title, view.zoom);
    }
    DrawTextW(dc, text, -1, const_cast<RECT*>(&label), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* s = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        s = reinterpret_cast<State*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEWHEEL: {
        if (!s) return 0;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        RECT leftPane{}, rightPane{}; GetMainPreviewPanes(hwnd, leftPane, rightPane);
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (s->hasSource && PtInRect(&leftPane, p)) {
            ZoomViewportAt(s->originalView, leftPane, s->source.display.width, s->source.display.height, p, delta);
            s->outputView = s->originalView;
        } else if (s->hasOutput && PtInRect(&rightPane, p)) {
            ZoomViewportAt(s->outputView, rightPane, s->output.width, s->output.height, p, delta);
            s->originalView = s->outputView;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!s) return 0;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT leftPane{}, rightPane{}; GetMainPreviewPanes(hwnd, leftPane, rightPane);
        s->originalView.dragging = false;
        s->outputView.dragging = false;
        if (s->hasSource && s->originalView.zoom > 1.0 && PtInRect(&leftPane, p)) {
            s->originalView.dragging = true;
            s->originalView.lastMouse = p;
            SetCapture(hwnd);
        } else if (s->hasOutput && s->outputView.zoom > 1.0 && PtInRect(&rightPane, p)) {
            s->outputView.dragging = true;
            s->outputView.lastMouse = p;
            SetCapture(hwnd);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!s || GetCapture() != hwnd) return 0;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT leftPane{}, rightPane{}; GetMainPreviewPanes(hwnd, leftPane, rightPane);
        if (s->originalView.dragging) {
            const int dx = p.x - s->originalView.lastMouse.x;
            const int dy = p.y - s->originalView.lastMouse.y;
            s->originalView.lastMouse = p;
            PanViewport(s->originalView, leftPane, s->source.display.width, s->source.display.height, dx, dy);
            s->outputView.zoom = s->originalView.zoom;
            s->outputView.panX = s->originalView.panX;
            s->outputView.panY = s->originalView.panY;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (s->outputView.dragging) {
            const int dx = p.x - s->outputView.lastMouse.x;
            const int dy = p.y - s->outputView.lastMouse.y;
            s->outputView.lastMouse = p;
            PanViewport(s->outputView, rightPane, s->output.width, s->output.height, dx, dy);
            s->originalView.zoom = s->outputView.zoom;
            s->originalView.panX = s->outputView.panX;
            s->originalView.panY = s->outputView.panY;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (s) { s->originalView.dragging = false; s->outputView.dragging = false; }
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (s) { s->originalView.dragging = false; s->outputView.dragging = false; }
        return 0;
    case WM_LBUTTONDBLCLK:
        if (s) {
            ResetViewport(s->originalView);
            ResetViewport(s->outputView);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SIZE:
        if (s) {
            RECT leftPane{}, rightPane{}; GetMainPreviewPanes(hwnd, leftPane, rightPane);
            if (s->hasSource) ClampViewportPan(s->originalView, leftPane, s->source.display.width, s->source.display.height);
            if (s->hasOutput) ClampViewportPan(s->outputView, rightPane, s->output.width, s->output.height);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT c{}; GetClientRect(hwnd, &c);
        const int clientW = std::max(1, static_cast<int>(c.right - c.left));
        const int clientH = std::max(1, static_cast<int>(c.bottom - c.top));
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, clientW, clientH);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);

        HBRUSH bg = CreateSolidBrush(RGB(25, 27, 31));
        FillRect(mem, &c, bg); DeleteObject(bg);

        RECT leftPane{}, rightPane{}, leftLabel{}, rightLabel{};
        GetMainPreviewPanes(hwnd, leftPane, rightPane, &leftLabel, &rightLabel);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(235, 238, 244));
        if (s) {
            DrawZoomHint(mem, leftLabel, s->originalView, L"ORIGINAL");
            DrawZoomHint(mem, rightLabel, s->outputView, L"DLSS5");
        }

        if (s && s->hasSource) DrawClippedImage(mem, leftPane, s->source.display, s->sourceBgra, s->originalView);
        else {
            SetTextColor(mem, RGB(165, 170, 180));
            DrawTextW(mem, L"Open an image to begin", -1, &leftPane, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        if (s && s->hasOutput) DrawClippedImage(mem, rightPane, s->output, s->outputBgra, s->outputView);
        else {
            SetTextColor(mem, RGB(165, 170, 180));
            DrawTextW(mem, L"DLSS5 result appears here after processing", -1, &rightPane, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const int dividerX = static_cast<int>(leftPane.right) + 5;
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(58, 62, 70));
        HPEN old = static_cast<HPEN>(SelectObject(mem, pen));
        MoveToEx(mem, dividerX, 0, nullptr); LineTo(mem, dividerX, c.bottom);
        SelectObject(mem, old); DeleteObject(pen);

        BitBlt(dc, 0, 0, clientW, clientH, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK DepthPreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* s = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        s = reinterpret_cast<State*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEWHEEL: {
        if (!s || !s->hasDepthPreview) return 0;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(hwnd, &p);
        RECT c{}; GetClientRect(hwnd, &c);
        RECT body{8, 32, std::max(9, static_cast<int>(c.right) - 8), std::max(33, static_cast<int>(c.bottom) - 8)};
        ZoomViewportAt(s->depthView, body, s->depthPreviewImage.width, s->depthPreviewImage.height, p, GET_WHEEL_DELTA_WPARAM(wp));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!s || !s->hasDepthPreview || s->depthView.zoom <= 1.0) return 0;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT c{}; GetClientRect(hwnd, &c);
        RECT body{8, 32, std::max(9, static_cast<int>(c.right) - 8), std::max(33, static_cast<int>(c.bottom) - 8)};
        if (PtInRect(&body, p)) {
            s->depthView.dragging = true; s->depthView.lastMouse = p; SetCapture(hwnd);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!s || !s->depthView.dragging || GetCapture() != hwnd) return 0;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        const int dx = p.x - s->depthView.lastMouse.x;
        const int dy = p.y - s->depthView.lastMouse.y;
        s->depthView.lastMouse = p;
        RECT c{}; GetClientRect(hwnd, &c);
        RECT body{8, 32, std::max(9, static_cast<int>(c.right) - 8), std::max(33, static_cast<int>(c.bottom) - 8)};
        PanViewport(s->depthView, body, s->depthPreviewImage.width, s->depthPreviewImage.height, dx, dy);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP:
        if (s) s->depthView.dragging = false;
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        if (s) s->depthView.dragging = false;
        return 0;
    case WM_LBUTTONDBLCLK:
        if (s) { ResetViewport(s->depthView); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_SIZE:
        if (s && s->hasDepthPreview) {
            RECT c{}; GetClientRect(hwnd, &c);
            RECT body{8, 32, std::max(9, static_cast<int>(c.right) - 8), std::max(33, static_cast<int>(c.bottom) - 8)};
            ClampViewportPan(s->depthView, body, s->depthPreviewImage.width, s->depthPreviewImage.height);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
        RECT c{}; GetClientRect(hwnd, &c);
        const int clientW = std::max(1, static_cast<int>(c.right - c.left));
        const int clientH = std::max(1, static_cast<int>(c.bottom - c.top));
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, clientW, clientH);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(RGB(20, 22, 26)); FillRect(mem, &c, bg); DeleteObject(bg);
        SetBkMode(mem, TRANSPARENT); SetTextColor(mem, RGB(235, 238, 244));
        RECT title{8, 4, std::max(9, static_cast<int>(c.right) - 8), 30};
        wchar_t titleText[180]{};
        const double z = s ? s->depthView.zoom : 1.0;
        swprintf_s(titleText, L"DEPTH GUIDANCE  |  %.2fx  |  Wheel: Zoom  Drag: Pan  Double-click: Fit", z);
        DrawTextW(mem, titleText, -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        RECT body{8, 32, std::max(9, static_cast<int>(c.right) - 8), std::max(33, static_cast<int>(c.bottom) - 8)};
        if (s && s->hasDepthPreview) DrawClippedImage(mem, body, s->depthPreviewImage, s->depthBgra, s->depthView);
        else {
            SetTextColor(mem, RGB(155, 160, 170));
            DrawTextW(mem, L"Auto/Manual depth preview appears here after generation or processing", -1, &body,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_WORDBREAK);
        }
        BitBlt(dc, 0, 0, clientW, clientH, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, 0,0,10,10,
                           parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                           reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
}
void Label(HWND p, const wchar_t* text, int id) { Make(p, L"STATIC", text, SS_LEFT, id); }

void CreateControls(State* s) {
    HWND h = s->hwnd;
    Label(h, L"SOURCE IMAGE", IDC_LBL_SOURCE_TITLE);
    Make(h, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, IDC_SOURCE_PATH);
    Make(h, L"BUTTON", L"Browse...", BS_PUSHBUTTON, IDC_SOURCE_BROWSE);
    Label(h, L"EXR Layer", IDC_LBL_SOURCE_EXR); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_EXR_LAYER);
    Label(h, L"Exposure EV", IDC_LBL_EXPOSURE); Make(h, L"EDIT", L"0.0", WS_BORDER | ES_AUTOHSCROLL, IDC_EXPOSURE);
    Label(h, L"Tone Map", IDC_LBL_TONEMAP); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_TONEMAP);
    ComboReset(GetDlgItem(h, IDC_TONEMAP), {L"Clamp / Linear", L"Reinhard", L"ACES Fitted"}, 2);
    Make(h, L"BUTTON", L"sRGB Encode after tone map", BS_AUTOCHECKBOX, IDC_SRGB); Button_SetCheck(GetDlgItem(h, IDC_SRGB), BST_CHECKED);
    Make(h, L"BUTTON", L"Apply Import Settings", BS_PUSHBUTTON, IDC_APPLY_IMPORT);

    Label(h, L"DEPTH GUIDANCE", IDC_LBL_DEPTH_TITLE);
    Label(h, L"Depth Mode", IDC_LBL_DEPTH_MODE); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_DEPTH_MODE);
    ComboReset(GetDlgItem(h, IDC_DEPTH_MODE), {L"Zero Depth", L"Auto Depth - Depth Anything V2", L"Manual Depth Map"}, 1);
    s->depthMode = 1;
    Make(h, L"BUTTON", L"Setup Auto Depth...", BS_PUSHBUTTON, IDC_AUTO_DEPTH_SETUP);
    Make(h, L"BUTTON", L"Generate / Refresh Auto Depth", BS_PUSHBUTTON, IDC_AUTO_DEPTH_GENERATE);
    Make(h, L"EDIT", L"No manual depth map selected", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, IDC_DEPTH_PATH);
    Make(h, L"BUTTON", L"Browse...", BS_PUSHBUTTON, IDC_DEPTH_BROWSE);
    Make(h, L"BUTTON", L"Clear", BS_PUSHBUTTON, IDC_DEPTH_CLEAR);
    Label(h, L"EXR Layer", IDC_LBL_DEPTH_EXR); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_DEPTH_LAYER);
    Label(h, L"Channel", IDC_LBL_DEPTH_CHANNEL); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_DEPTH_CHANNEL);
    ComboReset(GetDlgItem(h, IDC_DEPTH_CHANNEL), {L"R",L"G",L"B",L"A",L"Luminance"}, 0);
    Make(h, L"BUTTON", L"Auto normalize depth", BS_AUTOCHECKBOX, IDC_DEPTH_NORMALIZE); Button_SetCheck(GetDlgItem(h, IDC_DEPTH_NORMALIZE), BST_CHECKED);
    Make(h, L"BUTTON", L"Inverse depth / White = Near", BS_AUTOCHECKBOX, IDC_DEPTH_INVERSE); Button_SetCheck(GetDlgItem(h, IDC_DEPTH_INVERSE), BST_CHECKED);
    Label(h, L"Scale", IDC_LBL_DEPTH_SCALE); Make(h, L"EDIT", L"1.0", WS_BORDER, IDC_DEPTH_SCALE);
    Label(h, L"Offset", IDC_LBL_DEPTH_OFFSET); Make(h, L"EDIT", L"0.0", WS_BORDER, IDC_DEPTH_OFFSET);

    Label(h, L"DLSS 5 NEURAL RENDERING", IDC_LBL_DLSS_TITLE);
    Make(h, L"BUTTON", L"Neural Uplift / Enable", BS_AUTOCHECKBOX, IDC_NEURAL_UPLIFT); Button_SetCheck(GetDlgItem(h, IDC_NEURAL_UPLIFT), BST_CHECKED);
    Label(h, L"NR Preset", IDC_LBL_PRESET); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_PRESET);
    ComboReset(GetDlgItem(h, IDC_PRESET), {L"Default",L"Preset #1",L"Preset #2",L"Preset #3"}, 2);
    Label(h, L"NR Style / Look", IDC_LBL_STYLE); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_STYLE);
    ComboReset(GetDlgItem(h, IDC_STYLE), {L"Default",L"Natural",L"Cinematic"}, 2);

    auto slider = [&](int id) { HWND x=Make(h, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS, id); SendMessageW(x, TBM_SETRANGE, TRUE, MAKELONG(0,1000)); };
    Label(h, L"NR Intensity", IDC_LBL_INTENSITY); slider(IDC_INTENSITY); Label(h, L"", IDC_INTENSITY_VALUE);
    Label(h, L"Local Tone", IDC_LBL_LOCAL_TONE); slider(IDC_LOCAL_TONE); Label(h, L"", IDC_LOCAL_TONE_VALUE);
    Label(h, L"Local Structure", IDC_LBL_LOCAL_STRUCTURE); slider(IDC_LOCAL_STRUCTURE); Label(h, L"", IDC_LOCAL_STRUCTURE_VALUE);
    Label(h, L"Skin Structure", IDC_LBL_SKIN_STRUCTURE); slider(IDC_SKIN_STRUCTURE); Label(h, L"", IDC_SKIN_STRUCTURE_VALUE);
    SetTrack(h, IDC_INTENSITY, 0.85f, 0.0f, 2.0f);
    SetTrack(h, IDC_LOCAL_TONE, 1.0f, 0.0f, 2.0f);
    SetTrack(h, IDC_LOCAL_STRUCTURE, 1.0f, 0.0f, 2.0f);
    SetTrack(h, IDC_SKIN_STRUCTURE, -0.5f, -1.0f, 2.0f);
    UpdateSliderLabels(h);
    Make(h, L"BUTTON", L"Auto Skin Mask", BS_AUTOCHECKBOX, IDC_AUTO_MASK); Button_SetCheck(GetDlgItem(h, IDC_AUTO_MASK), BST_CHECKED);
    Make(h, L"BUTTON", L"UI Correction", BS_AUTOCHECKBOX, IDC_UI_CORRECTION); Button_SetCheck(GetDlgItem(h, IDC_UI_CORRECTION), BST_CHECKED);
    Label(h, L"Iterations", IDC_LBL_ITERATIONS); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_ITERATIONS);
    ComboReset(GetDlgItem(h, IDC_ITERATIONS), {L"1",L"2",L"4",L"8",L"16"}, 0);
    Make(h, L"EDIT", s->runtimePath.wstring().c_str(), WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, IDC_RUNTIME_PATH);
    Make(h, L"BUTTON", L"Runtime DLL...", BS_PUSHBUTTON, IDC_RUNTIME_BROWSE);
    Make(h, L"BUTTON", L"Save Parameters", BS_PUSHBUTTON, IDC_SAVE_PARAMETERS);
    for (const auto& spec : kImageParameterSpecs) Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, ImageResetButtonId(spec.id));

    Make(h, L"BUTTON", L"PROCESS DLSS5", BS_DEFPUSHBUTTON, IDC_PROCESS);
    Make(h, L"BUTTON", L"Save Output...", BS_PUSHBUTTON, IDC_SAVE); EnableWindow(GetDlgItem(h, IDC_SAVE), FALSE);
    Label(h, L"Ready. EXR is converted to the verified SDR RGBA8 DLSSNR contract.", IDC_STATUS);

    s->preview = CreateWindowExW(0, PREVIEW_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_CLIPSIBLINGS,
        0,0,10,10,h,reinterpret_cast<HMENU>(IDC_PREVIEW),reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(h,GWLP_HINSTANCE)),s);
    s->depthPreview = CreateWindowExW(0, DEPTH_PREVIEW_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_BORDER | WS_CLIPSIBLINGS,
        0,0,10,10,h,nullptr,reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(h,GWLP_HINSTANCE)),s);
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (HWND child = GetWindow(h, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (const auto& spec : kImageParameterSpecs) ApplyImageParameterValue(h, spec, spec.factoryValue);
    const bool loadedSavedParameters = LoadSavedImageParameters(s);
    if (loadedSavedParameters) SetStatus(s, L"Saved image parameters loaded. Per-parameter Reset restores factory defaults.");
}

void MoveCtrl(HWND h, int id, int x, int y, int w, int ht) { MoveWindow(GetDlgItem(h,id),x,y,w,ht,TRUE); }

// IMPORTANT: for CBS_DROPDOWNLIST controls, the HWND height controls the height of
// the drop-down list as well as the closed field. Moving a ComboBox with rowH (24px)
// collapses its list to roughly one visible item. Keep a larger window height while
// retaining the same y-spacing in the sidebar layout.
void MoveCombo(HWND h, int id, int x, int y, int w, int dropHeight = 160) {
    HWND combo = GetDlgItem(h, id);
    MoveWindow(combo, x, y, w, dropHeight, TRUE);
    SendMessageW(combo, CB_SETDROPPEDWIDTH, static_cast<WPARAM>(w), 0);
}

void Layout(State* s) {
    RECT c{}; GetClientRect(s->hwnd, &c);
    const int x0=12, w=SIDEBAR_W-24, labelW=108, rowH=24, resetW=50, gap=6;
    const int resetX=x0+w-resetW, valueX=x0+labelW;
    const int standardValueW=std::max(70,resetX-gap-valueX);
    int y=10;
    auto title=[&](int id){ MoveCtrl(s->hwnd,id,x0,y,w,20); y += 22; };
    auto reset=[&](int parameterId,int Y){ MoveCtrl(s->hwnd,ImageResetButtonId(parameterId),resetX,Y,resetW,rowH); };
    auto standardEdit=[&](int parameterId,int labelId,int Y,int width=0){
        MoveCtrl(s->hwnd,labelId,x0,Y,labelW-4,rowH);
        MoveCtrl(s->hwnd,parameterId,valueX,Y,width>0?std::min(width,standardValueW):standardValueW,rowH);
        reset(parameterId,Y);
    };
    auto standardCombo=[&](int parameterId,int labelId,int Y,int dropHeight){
        MoveCtrl(s->hwnd,labelId,x0,Y,labelW-4,rowH);
        MoveCombo(s->hwnd,parameterId,valueX,Y,standardValueW,dropHeight);
        reset(parameterId,Y);
    };
    auto checkboxReset=[&](int parameterId,int Y,int X,int ww){
        const int right=X+ww;
        MoveCtrl(s->hwnd,parameterId,X,Y,std::max(40,right-X-resetW-gap),rowH);
        MoveCtrl(s->hwnd,ImageResetButtonId(parameterId),right-resetW,Y,resetW,rowH);
    };
    auto checkboxResetFull=[&](int parameterId,int Y){ checkboxReset(parameterId,Y,x0,w); };

    title(IDC_LBL_SOURCE_TITLE);
    MoveCtrl(s->hwnd,IDC_SOURCE_PATH,x0,y,w-86,rowH); MoveCtrl(s->hwnd,IDC_SOURCE_BROWSE,x0+w-80,y,80,rowH); y+=30;
    MoveCtrl(s->hwnd,IDC_LBL_SOURCE_EXR,x0,y,labelW-4,rowH); MoveCombo(s->hwnd,IDC_EXR_LAYER,valueX,y,w-labelW-8,220); y+=30;
    standardEdit(IDC_EXPOSURE,IDC_LBL_EXPOSURE,y,100); y+=28;
    standardCombo(IDC_TONEMAP,IDC_LBL_TONEMAP,y,120); y+=30;
    checkboxResetFull(IDC_SRGB,y); y+=26;
    MoveCtrl(s->hwnd,IDC_APPLY_IMPORT,x0,y,w,rowH); y+=34;

    title(IDC_LBL_DEPTH_TITLE);
    standardCombo(IDC_DEPTH_MODE,IDC_LBL_DEPTH_MODE,y,120); y+=30;
    MoveCtrl(s->hwnd,IDC_AUTO_DEPTH_SETUP,x0,y,(w-8)/2,rowH); MoveCtrl(s->hwnd,IDC_AUTO_DEPTH_GENERATE,x0+(w+8)/2,y,(w-8)/2,rowH); y+=30;
    MoveCtrl(s->hwnd,IDC_DEPTH_PATH,x0,y,w-150,rowH); MoveCtrl(s->hwnd,IDC_DEPTH_BROWSE,x0+w-144,y,82,rowH); MoveCtrl(s->hwnd,IDC_DEPTH_CLEAR,x0+w-58,y,58,rowH); y+=30;
    MoveCtrl(s->hwnd,IDC_LBL_DEPTH_EXR,x0,y,labelW-4,rowH); MoveCombo(s->hwnd,IDC_DEPTH_LAYER,valueX,y,w-labelW-8,220); y+=30;
    MoveCtrl(s->hwnd,IDC_LBL_DEPTH_CHANNEL,x0,y,labelW-4,rowH); MoveCombo(s->hwnd,IDC_DEPTH_CHANNEL,valueX,y,w-labelW-8,180); y+=30;
    checkboxResetFull(IDC_DEPTH_NORMALIZE,y); y+=25;
    checkboxResetFull(IDC_DEPTH_INVERSE,y); y+=26;
    {
        const int groupGap=8, groupW=(w-groupGap)/2;
        const int rightX=x0+groupW+groupGap;
        auto pair=[&](int parameterId,int labelId,int X){
            const int localReset=X+groupW-resetW;
            MoveCtrl(s->hwnd,labelId,X,y,48,rowH);
            MoveCtrl(s->hwnd,parameterId,X+52,y,std::max(42,localReset-gap-(X+52)),rowH);
            MoveCtrl(s->hwnd,ImageResetButtonId(parameterId),localReset,y,resetW,rowH);
        };
        pair(IDC_DEPTH_SCALE,IDC_LBL_DEPTH_SCALE,x0);
        pair(IDC_DEPTH_OFFSET,IDC_LBL_DEPTH_OFFSET,rightX);
    }
    y+=34;

    title(IDC_LBL_DLSS_TITLE);
    checkboxResetFull(IDC_NEURAL_UPLIFT,y); y+=26;
    standardCombo(IDC_PRESET,IDC_LBL_PRESET,y,150); y+=30;
    standardCombo(IDC_STYLE,IDC_LBL_STYLE,y,130); y+=30;
    auto moveSlider=[&](int label,int id,int val){
        const int valW=44;
        const int valX=resetX-gap-valW;
        MoveCtrl(s->hwnd,label,x0,y,labelW-4,rowH);
        MoveCtrl(s->hwnd,id,valueX,y,std::max(70,valX-gap-valueX),rowH);
        MoveCtrl(s->hwnd,val,valX,y,valW,rowH);
        reset(id,y);
        y+=28;
    };
    moveSlider(IDC_LBL_INTENSITY,IDC_INTENSITY,IDC_INTENSITY_VALUE);
    moveSlider(IDC_LBL_LOCAL_TONE,IDC_LOCAL_TONE,IDC_LOCAL_TONE_VALUE);
    moveSlider(IDC_LBL_LOCAL_STRUCTURE,IDC_LOCAL_STRUCTURE,IDC_LOCAL_STRUCTURE_VALUE);
    moveSlider(IDC_LBL_SKIN_STRUCTURE,IDC_SKIN_STRUCTURE,IDC_SKIN_STRUCTURE_VALUE);
    {
        const int groupGap=8, groupW=(w-groupGap)/2;
        checkboxReset(IDC_AUTO_MASK,y,x0,groupW);
        checkboxReset(IDC_UI_CORRECTION,y,x0+groupW+groupGap,groupW);
    }
    y+=28;
    standardCombo(IDC_ITERATIONS,IDC_LBL_ITERATIONS,y,180); y+=30;
    MoveCtrl(s->hwnd,IDC_RUNTIME_PATH,x0,y,w-112,rowH); MoveCtrl(s->hwnd,IDC_RUNTIME_BROWSE,x0+w-106,y,106,rowH); y+=32;
    MoveCtrl(s->hwnd,IDC_SAVE_PARAMETERS,x0,y,w,rowH); y+=32;
    MoveCtrl(s->hwnd,IDC_PROCESS,x0,y,(w-8)/2,32); MoveCtrl(s->hwnd,IDC_SAVE,x0+(w+8)/2,y,(w-8)/2,32); y+=39;
    const int clientBottom = static_cast<int>(c.bottom);
    const int clientRight = static_cast<int>(c.right);
    MoveCtrl(s->hwnd,IDC_STATUS,x0,y,w,std::max(38,clientBottom-y-10));

    // Responsive preview region. Keep the child windows inside the client area even
    // when maximizing/restoring across monitors with different work-area sizes.
    const int rightX = std::min(SIDEBAR_W, std::max(0, clientRight - 320));
    const int rightW = std::max(10, clientRight - rightX - 8);
    const int rightH = std::max(10, clientBottom - 16);
    const int previewGap = 8;
    int depthH = std::clamp(static_cast<int>(std::lround(rightH * 0.30)), 150, std::max(150, rightH - 260));
    if (rightH < 460) depthH = std::max(100, rightH / 3);
    int previewH = std::max(10, rightH - depthH - previewGap);
    if (previewH < 180) {
        previewH = std::max(10, rightH * 2 / 3);
        depthH = std::max(10, rightH - previewH - previewGap);
    }

    SetWindowPos(s->preview, nullptr, rightX, 8, rightW, previewH,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(s->depthPreview, nullptr, rightX, 8 + previewH + previewGap, rightW, std::max(10, depthH),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(s->preview, nullptr, FALSE);
    InvalidateRect(s->depthPreview, nullptr, FALSE);
}

int CurrentDepthMode(State* s) {
    const int sel = static_cast<int>(SendMessageW(GetDlgItem(s->hwnd, IDC_DEPTH_MODE), CB_GETCURSEL, 0, 0));
    return sel == CB_ERR ? 0 : std::clamp(sel, 0, 2);
}

void UpdateDepthModeUi(State* s) {
    s->depthMode = CurrentDepthMode(s);
    const bool manual = s->depthMode == 2;
    const bool autoDepth = s->depthMode == 1;
    const int manualIds[] = {IDC_DEPTH_PATH,IDC_DEPTH_BROWSE,IDC_DEPTH_CLEAR,IDC_DEPTH_LAYER,IDC_DEPTH_CHANNEL,
                             IDC_DEPTH_NORMALIZE,IDC_DEPTH_INVERSE,IDC_DEPTH_SCALE,IDC_DEPTH_OFFSET};
    for (int id : manualIds) EnableWindow(GetDlgItem(s->hwnd,id), manual && !s->busy ? TRUE : FALSE);
    const int manualParameterIds[] = {IDC_DEPTH_NORMALIZE,IDC_DEPTH_INVERSE,IDC_DEPTH_SCALE,IDC_DEPTH_OFFSET};
    for (int id : manualParameterIds) EnableWindow(GetDlgItem(s->hwnd,ImageResetButtonId(id)), manual && !s->busy ? TRUE : FALSE);
    EnableWindow(GetDlgItem(s->hwnd, IDC_AUTO_DEPTH_GENERATE), autoDepth && !s->busy ? TRUE : FALSE);
}

void SetBusy(State* s, bool busy) {
    s->busy = busy;
    EnableWindow(GetDlgItem(s->hwnd,IDC_PROCESS), busy ? FALSE : TRUE);
    EnableWindow(GetDlgItem(s->hwnd,IDC_SAVE), (!busy && s->hasOutput) ? TRUE : FALSE);
    EnableWindow(GetDlgItem(s->hwnd,IDC_SAVE_PARAMETERS), busy ? FALSE : TRUE);
    for (const auto& spec : kImageParameterSpecs)
        EnableWindow(GetDlgItem(s->hwnd,ImageResetButtonId(spec.id)), busy ? FALSE : TRUE);
    UpdateDepthModeUi(s);
}

void LaunchAutoDepthSetup(State* s) {
    const auto script = autodepth::SetupScript();
    if (!std::filesystem::exists(script)) {
        MessageBoxW(s->hwnd, L"Auto Depth setup script was not found next to the executable.", L"Auto Depth", MB_ICONERROR);
        return;
    }
    std::wstring args = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() + L"\"";
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(s->hwnd, L"open", L"powershell.exe", args.c_str(),
                                                           script.parent_path().c_str(), SW_SHOWNORMAL));
    if (r <= 32) {
        MessageBoxW(s->hwnd, L"Could not launch Auto Depth setup.", L"Auto Depth", MB_ICONERROR);
    } else {
        SetStatus(s, L"Auto Depth setup launched. When it finishes, click Generate / Refresh Auto Depth.");
    }
}

void StartAutoDepth(State* s) {
    if (s->busy || s->sourcePath.empty()) return;
    try { ReloadSource(s); } catch (const std::exception& e) { MessageBoxA(s->hwnd,e.what(),"Import error",MB_ICONERROR); return; }
    const auto input = s->source.display;
    SendMessageW(GetDlgItem(s->hwnd,IDC_DEPTH_MODE),CB_SETCURSEL,1,0);
    s->depthMode = 1;
    s->workerTask = State::WorkerTask::AutoDepth;
    SetBusy(s, true);
    SetStatus(s,L"Generating Auto Depth with Depth Anything V2...");
    if(s->worker.joinable()) s->worker.join();
    s->worker=std::thread([s,input]() mutable {
        std::string error;
        DepthMap depth;
        bool hasDepth = false;
        try {
            depth = autodepth::Generate(input, 518);
            hasDepth = true;
        } catch(const std::exception& e) { error = e.what(); }
        {
            std::lock_guard<std::mutex> lock(s->workerMutex);
            s->workerDepth = std::move(depth);
            s->workerHasDepth = hasDepth;
            s->workerError = std::move(error);
        }
        PostMessageW(s->hwnd,WM_PROCESS_DONE,0,0);
    });
}

void StartProcessing(State* s) {
    if (s->busy || s->sourcePath.empty()) return;
    try { ReloadSource(s); } catch (const std::exception& e) { MessageBoxA(s->hwnd,e.what(),"Import error",MB_ICONERROR); return; }
    const auto input = s->source.display;
    const auto depthPath = s->depthPath;
    const auto depthSettings = ReadDepthSettings(s);
    const int depthMode = CurrentDepthMode(s);
    auto dlss = ReadDlssSettings(s);
    dlss.depthInverted = depthMode == 1 ? true : depthSettings.inverseDepth;
    s->neuralUplift = Button_GetCheck(GetDlgItem(s->hwnd,IDC_NEURAL_UPLIFT))==BST_CHECKED;
    const bool enabled=s->neuralUplift;
    const auto runtime=s->runtimePath;
    s->workerTask = State::WorkerTask::Process;
    SetBusy(s, true);
    SetStatus(s, depthMode == 1 ? L"Generating Auto Depth, then processing DLSS5..." : L"Processing DLSS5...");
    if(s->worker.joinable()) s->worker.join();
    s->worker=std::thread([s,input,depthPath,depthSettings,depthMode,dlss,runtime,enabled]() mutable {
        Rgba8Image result;
        DepthMap depthResult;
        bool hasDepth = false;
        std::string error;
        try {
            std::optional<DepthMap> depth;
            if (depthMode == 1) {
                depth = autodepth::Generate(input, 518);
            } else if (depthMode == 2) {
                if (depthPath.empty()) throw std::runtime_error("Manual Depth mode is selected but no depth image was imported.");
                depth = LoadDepthMap(depthPath,depthSettings,input.width,input.height);
            }
            if (depth) { depthResult = *depth; hasDepth = true; }
            if(!enabled){ result=input; }
            else {
                D3D12Context d3d;
                DlssNrRunner runner(d3d,runtime,dlss);
                result=runner.Process(input,depth?&depth->values:nullptr);
            }
        } catch(const std::exception& e){ error=e.what(); }
        {
            std::lock_guard<std::mutex> lock(s->workerMutex);
            s->workerOutput=std::move(result);
            s->workerDepth=std::move(depthResult);
            s->workerHasDepth=hasDepth;
            s->workerError=std::move(error);
        }
        PostMessageW(s->hwnd,WM_PROCESS_DONE,0,0);
    });
}

void HandleProcessDone(State* s) {
    if(s->worker.joinable()) s->worker.join();
    std::string error;
    Rgba8Image out;
    DepthMap depth;
    bool hasDepth = false;
    const auto task = s->workerTask;
    {
        std::lock_guard<std::mutex> lock(s->workerMutex);
        error=s->workerError;
        out=std::move(s->workerOutput);
        depth=std::move(s->workerDepth);
        hasDepth=s->workerHasDepth;
        s->workerError.clear();
        s->workerHasDepth=false;
    }
    s->workerTask = State::WorkerTask::None;
    SetBusy(s, false);
    if(!error.empty()){
        SetStatus(s, task == State::WorkerTask::AutoDepth ? L"Auto Depth failed." : L"Processing failed.");
        MessageBoxA(s->hwnd,error.c_str(), task == State::WorkerTask::AutoDepth ? "Auto Depth error" : "DLSS5 processing error",MB_ICONERROR);
        return;
    }
    if (hasDepth) SetDepthPreview(s, std::move(depth));
    if (task == State::WorkerTask::AutoDepth) {
        SetStatus(s,L"Auto Depth ready. The preview below is the same relative inverse-depth guidance that will be sent to DLSS5.");
        return;
    }
    s->output=std::move(out); s->outputBgra=ToBgra(s->output); s->hasOutput=true;
    s->outputView = s->originalView;
    EnableWindow(GetDlgItem(s->hwnd,IDC_SAVE),TRUE);
    SetStatus(s,L"DLSS5 complete. Original and processed images are shown simultaneously at full-fit scale.");
    InvalidateRect(s->preview,nullptr,TRUE);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* s=reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){ auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp); s=reinterpret_cast<State*>(cs->lpCreateParams); s->hwnd=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s)); }
    switch(msg){
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = 1120;
        mmi->ptMinTrackSize.y = 920;
        return 0;
    }
    case WM_CREATE: CreateControls(s); Layout(s); UpdateDepthModeUi(s); return 0;
    case WM_SIZE: if(s && wp != SIZE_MINIMIZED) Layout(s); return 0;
    case WM_HSCROLL: UpdateSliderLabels(hwnd); return 0;
    case WM_COMMAND: {
        const int id=LOWORD(wp); const int code=HIWORD(wp);
        if (id >= IMAGE_RESET_BUTTON_BASE && code == BN_CLICKED) {
            ResetOneImageParameter(s, id - IMAGE_RESET_BUTTON_BASE);
            return 0;
        }
        if(id==IDC_SOURCE_BROWSE && code==BN_CLICKED){
            if(auto p=OpenFile(hwnd,false)){ s->sourcePath=*p; SetText(hwnd,IDC_SOURCE_PATH,p->wstring()); if(IsExrPath(*p)) PopulateExrLayers(GetDlgItem(hwnd,IDC_EXR_LAYER),*p,true); else ComboReset(GetDlgItem(hwnd,IDC_EXR_LAYER),{L""}); try{ReloadSource(s);}catch(const std::exception&e){MessageBoxA(hwnd,e.what(),"Import error",MB_ICONERROR);} }
        } else if(id==IDC_APPLY_IMPORT && code==BN_CLICKED){ try{ReloadSource(s);}catch(const std::exception&e){MessageBoxA(hwnd,e.what(),"Import error",MB_ICONERROR);} }
        else if(id==IDC_DEPTH_MODE && code==CBN_SELCHANGE){
            UpdateDepthModeUi(s);
            if(CurrentDepthMode(s)==0){ ClearDepthPreview(s); SetStatus(s,L"Zero Depth mode selected."); }
            else if(CurrentDepthMode(s)==1){ SetStatus(s,L"Auto Depth mode selected. Generate a preview or press Process to generate it automatically."); }
            else { SetStatus(s,L"Manual Depth mode selected. Import a depth image below."); }
        }
        else if(id==IDC_AUTO_DEPTH_SETUP && code==BN_CLICKED){ LaunchAutoDepthSetup(s); }
        else if(id==IDC_AUTO_DEPTH_GENERATE && code==BN_CLICKED){ StartAutoDepth(s); }
        else if(id==IDC_DEPTH_BROWSE && code==BN_CLICKED){ if(auto p=OpenFile(hwnd,true)){
            s->depthPath=*p; SetText(hwnd,IDC_DEPTH_PATH,p->wstring());
            SendMessageW(GetDlgItem(hwnd,IDC_DEPTH_MODE),CB_SETCURSEL,2,0); UpdateDepthModeUi(s);
            if(IsExrPath(*p))PopulateExrLayers(GetDlgItem(hwnd,IDC_DEPTH_LAYER),*p,false);else ComboReset(GetDlgItem(hwnd,IDC_DEPTH_LAYER),{L""});
            PopulateDepthChannels(s);
            try{ if(s->hasSource) SetDepthPreview(s,LoadDepthMap(s->depthPath,ReadDepthSettings(s),s->source.display.width,s->source.display.height)); }catch(...){}
            SetStatus(s,L"Manual depth selected and previewed. It will be normalized/resized during processing.");
        } }
        else if(id==IDC_DEPTH_CLEAR && code==BN_CLICKED){
            s->depthPath.clear();SetText(hwnd,IDC_DEPTH_PATH,L"No manual depth map selected");ComboReset(GetDlgItem(hwnd,IDC_DEPTH_LAYER),{L""});PopulateDepthChannels(s);
            SendMessageW(GetDlgItem(hwnd,IDC_DEPTH_MODE),CB_SETCURSEL,0,0);UpdateDepthModeUi(s);ClearDepthPreview(s);
        }
        else if(id==IDC_DEPTH_LAYER && code==CBN_SELCHANGE){ PopulateDepthChannels(s); }
        else if(id==IDC_DEPTH_CHANNEL && code==CBN_SELCHANGE && CurrentDepthMode(s)==2 && s->hasSource && !s->depthPath.empty()){
            try{ SetDepthPreview(s,LoadDepthMap(s->depthPath,ReadDepthSettings(s),s->source.display.width,s->source.display.height)); }catch(...){}
        }
        else if(id==IDC_RUNTIME_BROWSE && code==BN_CLICKED){ if(auto p=OpenRuntimeFile(hwnd)){
            try {
                const auto dest = app::DefaultRuntimeDll();
                std::filesystem::create_directories(dest.parent_path());
                std::error_code eqError;
                const bool same = std::filesystem::equivalent(*p, dest, eqError);
                if (!same) std::filesystem::copy_file(*p, dest, std::filesystem::copy_options::overwrite_existing);
                s->runtimePath=dest; SetText(hwnd,IDC_RUNTIME_PATH,dest.wstring()); SetStatus(s,L"DLSSNR runtime imported into dist/runtime and will persist across launches.");
            } catch(const std::exception& e) {
                s->runtimePath=*p; SetText(hwnd,IDC_RUNTIME_PATH,p->wstring());
                MessageBoxA(hwnd,e.what(),"Runtime import warning - using selected DLL for this session",MB_ICONWARNING);
            }
        } }
        else if(id==IDC_SAVE_PARAMETERS && code==BN_CLICKED){ SaveImageParameters(s); }
        else if(id==IDC_PROCESS && code==BN_CLICKED) StartProcessing(s);
        else if(id==IDC_SAVE && code==BN_CLICKED && s->hasOutput){ if(auto p=SaveFile(hwnd,s->sourcePath)){ try{SaveOutputImage(*p,s->output);SetStatus(s,L"Output saved: "+p->wstring());}catch(const std::exception&e){MessageBoxA(hwnd,e.what(),"Save error",MB_ICONERROR);} } }
        return 0;
    }
    case WM_PROCESS_DONE: HandleProcessDone(s); return 0;
    case WM_CLOSE: if(s&&s->busy){MessageBoxW(hwnd,L"DLSS5 is still processing. Wait for the current conversion to finish before closing.",L"Processing",MB_ICONINFORMATION);return 0;} DestroyWindow(hwnd);return 0;
    case WM_DESTROY: if(s&&s->worker.joinable())s->worker.join(); PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
}

int RunGuiApp(void* instanceRaw, int showCommand) {
    HINSTANCE instance=reinterpret_cast<HINSTANCE>(instanceRaw);
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&ic);
    WNDCLASSEXW pc{sizeof(pc)};pc.style=CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS;pc.hInstance=instance;pc.lpfnWndProc=PreviewProc;pc.lpszClassName=PREVIEW_CLASS;pc.hCursor=LoadCursor(nullptr,IDC_ARROW);pc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassExW(&pc);
    WNDCLASSEXW dc{sizeof(dc)};dc.style=CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS;dc.hInstance=instance;dc.lpfnWndProc=DepthPreviewProc;dc.lpszClassName=DEPTH_PREVIEW_CLASS;dc.hCursor=LoadCursor(nullptr,IDC_ARROW);dc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassExW(&dc);
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=instance;wc.lpfnWndProc=MainProc;wc.lpszClassName=MAIN_CLASS;wc.hCursor=LoadCursor(nullptr,IDC_ARROW);wc.hIcon=LoadIcon(nullptr,IDI_APPLICATION);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassExW(&wc);
    State state;
    HWND hwnd=CreateWindowExW(0,MAIN_CLASS,L"Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1580,980,nullptr,nullptr,instance,&state);
    if(!hwnd)return 1;
    ShowWindow(hwnd,showCommand);UpdateWindow(hwnd);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return static_cast<int>(msg.wParam);
}
