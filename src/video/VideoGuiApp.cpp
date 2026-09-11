#include "VideoGuiApp.h"
#include "VideoConverter.h"
#include "FfmpegProcess.h"
#include "ExternalRenderDataDialog.h"
#include "MotionPreview.h"
#include "AppPaths.h"
#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Comctl32.lib")

namespace {
constexpr wchar_t MAIN_CLASS[] = L"DLSS5VideoConverterMainV066A1";
constexpr wchar_t PREVIEW_CLASS[] = L"DLSS5VideoPreviewV066A1";
constexpr UINT WM_APP_PROGRESS = WM_APP + 20;
constexpr UINT WM_APP_PREVIEW = WM_APP + 21;
constexpr UINT WM_APP_FINISHED = WM_APP + 22;
constexpr UINT WM_APP_PROBE_FINISHED = WM_APP + 23;
constexpr int SIDEBAR = 480;

enum : int {
    IDC_INPUT = 100, IDC_INPUT_BROWSE, IDC_OUTPUT, IDC_OUTPUT_BROWSE,
    IDC_INFO, IDC_DEPTH_MODE, IDC_DEPTH_SIZE, IDC_PRESET, IDC_STYLE,
    IDC_INTENSITY, IDC_LOCAL_TONE, IDC_LOCAL_STRUCTURE, IDC_SKIN_STRUCTURE,
    IDC_AUTO_MASK, IDC_UI_CORRECTION, IDC_ITERATIONS,
    IDC_TEMPORAL_MODE, IDC_FLOW_WIDTH, IDC_SCENE_CUT, IDC_MV_SCALE_X, IDC_MV_SCALE_Y,
    IDC_NVOF_QUALITY, IDC_NVOF_GRID, IDC_NVOF_TEMPORAL_HINTS, IDC_NVOF_OUTPUT_COST,
    IDC_DEPTH_STABILIZE, IDC_OUTPUT_STABILIZE,
    IDC_DENOISE_MODE, IDC_DENOISE_STRENGTH, IDC_DENOISE_HISTORY, IDC_DENOISE_SPATIAL, IDC_DENOISE_DETAIL,
    IDC_CODEC, IDC_QUALITY, IDC_SETUP_VIDEO, IDC_SETUP_DEPTH, IDC_RUNTIME, IDC_SAVE_PARAMETERS,
    IDC_EXTERNAL_DATA, IDC_EXTERNAL_SUMMARY,
    IDC_START, IDC_CANCEL, IDC_PROGRESS, IDC_PROGRESS_TEXT, IDC_STATUS, IDC_SIDEBAR_SCROLL,
    IDC_PREVIEW_ORIGINAL, IDC_PREVIEW_OUTPUT, IDC_PREVIEW_DEPTH, IDC_PREVIEW_MOTION
};

struct State;
struct PreviewContext { State* state = nullptr; int kind = 0; };

enum class ParameterKind { Edit, Combo, Check };
struct ParameterSpec {
    int id;
    ParameterKind kind;
    const wchar_t* key;
    const wchar_t* factoryValue;
    int comboMax = 0;
};

// Factory defaults are intentionally centralized here. V0.6.4 adopts the anti-warp
// temporal/denoise defaults selected after V0.6.3 visual tuning: more flow detail, less temporal
// history pressure, stronger detail protection and no post-NR output history by default.
static constexpr ParameterSpec kParameterSpecs[] = {
    {IDC_DEPTH_MODE, ParameterKind::Combo, L"depth_mode", L"0", 2},
    {IDC_DEPTH_SIZE, ParameterKind::Edit, L"dav2_size", L"518"},
    {IDC_PRESET, ParameterKind::Combo, L"nr_preset", L"2", 3},
    {IDC_STYLE, ParameterKind::Combo, L"nr_style", L"1", 2},
    {IDC_INTENSITY, ParameterKind::Edit, L"nr_intensity", L"0.60"},
    {IDC_LOCAL_TONE, ParameterKind::Edit, L"nr_local_tone", L"0.80"},
    {IDC_LOCAL_STRUCTURE, ParameterKind::Edit, L"nr_local_structure", L"0.35"},
    {IDC_SKIN_STRUCTURE, ParameterKind::Edit, L"nr_skin_structure", L"0.0"},
    {IDC_AUTO_MASK, ParameterKind::Check, L"nr_auto_skin_mask", L"0"},
    {IDC_UI_CORRECTION, ParameterKind::Check, L"nr_ui_correction", L"0"},
    {IDC_TEMPORAL_MODE, ParameterKind::Combo, L"temporal_mode", L"1", 4},
    {IDC_NVOF_QUALITY, ParameterKind::Combo, L"nvof_quality", L"0", 2},
    {IDC_NVOF_GRID, ParameterKind::Combo, L"nvof_grid", L"0", 2},
    {IDC_NVOF_TEMPORAL_HINTS, ParameterKind::Check, L"nvof_temporal_hints", L"1"},
    {IDC_NVOF_OUTPUT_COST, ParameterKind::Check, L"nvof_output_cost", L"1"},
    {IDC_FLOW_WIDTH, ParameterKind::Edit, L"flow_width", L"480"},
    {IDC_SCENE_CUT, ParameterKind::Edit, L"scene_cut", L"0.28"},
    {IDC_MV_SCALE_X, ParameterKind::Edit, L"mv_scale_x", L"1.0"},
    {IDC_MV_SCALE_Y, ParameterKind::Edit, L"mv_scale_y", L"1.0"},
    {IDC_DEPTH_STABILIZE, ParameterKind::Check, L"stable_depth", L"1"},
    {IDC_OUTPUT_STABILIZE, ParameterKind::Check, L"stable_output", L"0"},
    {IDC_DENOISE_MODE, ParameterKind::Combo, L"denoise_mode", L"2", 2},
    {IDC_DENOISE_STRENGTH, ParameterKind::Edit, L"denoise_strength", L"0.72"},
    {IDC_DENOISE_HISTORY, ParameterKind::Edit, L"denoise_history", L"0.76"},
    {IDC_DENOISE_SPATIAL, ParameterKind::Edit, L"denoise_spatial", L"0.22"},
    {IDC_DENOISE_DETAIL, ParameterKind::Edit, L"denoise_detail_protect", L"0.86"},
    {IDC_ITERATIONS, ParameterKind::Combo, L"iterations", L"0", 3},
    {IDC_CODEC, ParameterKind::Combo, L"encoder", L"0", 2},
    {IDC_QUALITY, ParameterKind::Edit, L"quality", L"18"},
};

constexpr int RESET_BUTTON_BASE = 5000;
constexpr int ResetButtonId(int parameterId) { return RESET_BUTTON_BASE + parameterId; }

void SyncTemporalUi(struct State* s);

struct State {
    HWND hwnd = nullptr;
    HWND previewOriginal = nullptr;
    HWND previewOutput = nullptr;
    HWND previewDepth = nullptr;
    HWND previewMotion = nullptr;
    PreviewContext previewCtx[4]{};

    std::mutex mutex;
    Rgba8Image original;
    Rgba8Image output;
    Rgba8Image depth;
    Rgba8Image motion;
    video::VideoProgress progress;
    std::wstring error;
    bool hasOriginal = false;
    bool hasOutput = false;
    bool hasDepth = false;
    bool hasMotion = false;
    float motionPreviewP95 = 0.0f;
    bool running = false;
    std::atomic_bool cancel{false};
    std::thread worker;
    std::thread probeWorker;
    std::atomic_bool probeCancel{false};
    bool probing = false;
    video::VideoInfo probeInfo;
    std::wstring probeError;
    int sidebarScrollY = 0;
    int sidebarContentHeight = 0;
    std::filesystem::path inputPath;
    std::filesystem::path outputPath;
    video::ExternalRenderDataSettings externalData;
};

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

void SetText(HWND p, int id, const std::wstring& text) { SetWindowTextW(GetDlgItem(p, id), text.c_str()); }
std::wstring GetText(HWND p, int id) {
    HWND h = GetDlgItem(p, id);
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1); s.resize(static_cast<size_t>(n)); return s;
}

HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
    return CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, 0,0,10,10,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
}
void Label(HWND p, const wchar_t* text, int id) { Make(p, L"STATIC", text, SS_LEFT, id); }

LRESULT CALLBACK SidebarChildSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclassId, DWORD_PTR refData) {
    if(msg==WM_MOUSEWHEEL){
        auto* s=reinterpret_cast<State*>(refData);
        if(s && s->hwnd) return SendMessageW(s->hwnd,WM_MOUSEWHEEL,wp,lp);
    }
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,SidebarChildSubclassProc,subclassId);
    return DefSubclassProc(hwnd,msg,wp,lp);
}

void Combo(HWND parent, int id, const std::vector<std::wstring>& items, int selection) {
    HWND c = GetDlgItem(parent, id);
    SendMessageW(c, CB_RESETCONTENT, 0, 0);
    for (const auto& x : items) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(x.c_str()));
    SendMessageW(c, CB_SETCURSEL, selection, 0);
#ifdef CB_SETMINVISIBLE
    SendMessageW(c, CB_SETMINVISIBLE, static_cast<WPARAM>(items.size()), 0);
#endif
}

int ComboSel(HWND parent, int id, int fallback = 0) {
    const auto v = static_cast<int>(SendMessageW(GetDlgItem(parent, id), CB_GETCURSEL, 0, 0));
    return v == CB_ERR ? fallback : v;
}

float ReadFloat(HWND p, int id, float fallback) {
    try { return std::stof(GetText(p, id)); } catch (...) { return fallback; }
}
int ReadInt(HWND p, int id, int fallback) {
    try { return std::stoi(GetText(p, id)); } catch (...) { return fallback; }
}

const ParameterSpec* FindParameterSpec(int id) {
    for (const auto& spec : kParameterSpecs) if (spec.id == id) return &spec;
    return nullptr;
}

std::filesystem::path ParameterSettingsPath() {
    return app::ExecutableDir() / L"video" / L"video-parameters.ini";
}

void LoadExternalParameterSettings(State* s) {
    const auto path = ParameterSettingsPath();
    if (!std::filesystem::exists(path)) return;
    constexpr wchar_t section[] = L"ExternalRenderData";
    wchar_t value[1024]{};
    auto read = [&](const wchar_t* key, const wchar_t* fallback) {
        GetPrivateProfileStringW(section, key, fallback, value, ARRAYSIZE(value), path.c_str());
        return std::wstring(value);
    };
    s->externalData.depth.channel = ToUtf8(read(L"depth_channel", L""));
    s->externalData.depth.mapping = read(L"depth_mapping", L"0") == L"1"
        ? video::ExternalDepthMapping::Raw01 : video::ExternalDepthMapping::FixedRange;
    try { s->externalData.depth.nearValue = std::stof(read(L"depth_near", L"0.1")); } catch (...) {}
    try { s->externalData.depth.farValue = std::stof(read(L"depth_far", L"100.0")); } catch (...) {}
    s->externalData.depth.depthInverted = read(L"depth_inverted", L"1") != L"0";
    s->externalData.motion.xChannel = ToUtf8(read(L"motion_x_channel", L""));
    s->externalData.motion.yChannel = ToUtf8(read(L"motion_y_channel", L""));
    try { s->externalData.motion.importScaleX = std::stof(read(L"motion_import_scale_x", L"1.0")); } catch (...) {}
    try { s->externalData.motion.importScaleY = std::stof(read(L"motion_import_scale_y", L"1.0")); } catch (...) {}
    s->externalData.motion.flipX = read(L"motion_flip_x", L"0") != L"0";
    s->externalData.motion.flipY = read(L"motion_flip_y", L"0") != L"0";
    s->externalData.motion.direction = read(L"motion_direction", L"0") == L"1"
        ? video::ExternalMotionDirection::PreviousToCurrent : video::ExternalMotionDirection::CurrentToPrevious;
}

void SaveExternalParameterSettings(State* s, const std::filesystem::path& path) {
    constexpr wchar_t section[] = L"ExternalRenderData";
    auto write = [&](const wchar_t* key, const std::wstring& value) {
        if (!WritePrivateProfileStringW(section, key, value.c_str(), path.c_str()))
            throw std::runtime_error("Unable to save an External Render Data parameter.");
    };
    write(L"schema", L"2");
    write(L"depth_channel", ToWide(s->externalData.depth.channel));
    write(L"depth_mapping", s->externalData.depth.mapping == video::ExternalDepthMapping::Raw01 ? L"1" : L"0");
    write(L"depth_near", std::to_wstring(s->externalData.depth.nearValue));
    write(L"depth_far", std::to_wstring(s->externalData.depth.farValue));
    write(L"depth_inverted", s->externalData.depth.depthInverted ? L"1" : L"0");
    write(L"motion_x_channel", ToWide(s->externalData.motion.xChannel));
    write(L"motion_y_channel", ToWide(s->externalData.motion.yChannel));
    write(L"motion_import_scale_x", std::to_wstring(s->externalData.motion.importScaleX));
    write(L"motion_import_scale_y", std::to_wstring(s->externalData.motion.importScaleY));
    write(L"motion_flip_x", s->externalData.motion.flipX ? L"1" : L"0");
    write(L"motion_flip_y", s->externalData.motion.flipY ? L"1" : L"0");
    write(L"motion_direction", s->externalData.motion.direction == video::ExternalMotionDirection::PreviousToCurrent ? L"1" : L"0");
}

void ApplyParameterValue(HWND hwnd, const ParameterSpec& spec, const std::wstring& value) {
    try {
        switch (spec.kind) {
        case ParameterKind::Edit:
            SetText(hwnd, spec.id, value);
            break;
        case ParameterKind::Combo: {
            const int selection = std::clamp(std::stoi(value), 0, spec.comboMax);
            SendMessageW(GetDlgItem(hwnd, spec.id), CB_SETCURSEL, selection, 0);
            break;
        }
        case ParameterKind::Check:
            Button_SetCheck(GetDlgItem(hwnd, spec.id), std::stoi(value) != 0 ? BST_CHECKED : BST_UNCHECKED);
            break;
        }
    } catch (...) {
        // A malformed saved entry is isolated to that one parameter and falls back
        // to the factory value rather than invalidating the whole user profile.
        ApplyParameterValue(hwnd, spec, spec.factoryValue);
    }
}

std::wstring ReadParameterValue(HWND hwnd, const ParameterSpec& spec) {
    switch (spec.kind) {
    case ParameterKind::Edit:
        return GetText(hwnd, spec.id);
    case ParameterKind::Combo:
        return std::to_wstring(std::clamp(ComboSel(hwnd, spec.id, std::stoi(spec.factoryValue)), 0, spec.comboMax));
    case ParameterKind::Check:
        return Button_GetCheck(GetDlgItem(hwnd, spec.id)) == BST_CHECKED ? L"1" : L"0";
    }
    return {};
}

void ResetOneParameter(State* s, int parameterId) {
    const auto* spec = FindParameterSpec(parameterId);
    if (!spec) return;
    ApplyParameterValue(s->hwnd, *spec, spec->factoryValue);
    SyncTemporalUi(s);
    SetText(s->hwnd, IDC_STATUS, L"Factory default restored for this parameter. Click Save Parameters to persist it.");
}

bool LoadSavedParameters(State* s) {
    const auto path = ParameterSettingsPath();
    if (!std::filesystem::exists(path)) return false;
    constexpr wchar_t section[] = L"VideoParameters";
    constexpr wchar_t missing[] = L"{DLSS5_MISSING_VALUE}";
    wchar_t buffer[512]{};
    for (const auto& spec : kParameterSpecs) {
        buffer[0] = L'\0';
        GetPrivateProfileStringW(section, spec.key, missing, buffer, ARRAYSIZE(buffer), path.c_str());
        if (std::wstring(buffer) == missing) continue;
        ApplyParameterValue(s->hwnd, spec, buffer);
    }
    LoadExternalParameterSettings(s);
    return true;
}

void SaveParameters(State* s) {
    const auto path = ParameterSettingsPath();
    try {
        std::filesystem::create_directories(path.parent_path());
        constexpr wchar_t section[] = L"VideoParameters";
        if (!WritePrivateProfileStringW(section, L"schema", L"2", path.c_str())) {
            throw std::runtime_error("Unable to create the parameter profile.");
        }
        for (const auto& spec : kParameterSpecs) {
            const auto value = ReadParameterValue(s->hwnd, spec);
            if (!WritePrivateProfileStringW(section, spec.key, value.c_str(), path.c_str())) {
                throw std::runtime_error("Unable to write a parameter profile entry.");
            }
        }
        SaveExternalParameterSettings(s, path);
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
        SetText(s->hwnd, IDC_STATUS, L"Parameters saved. External channel/mapping settings are saved; EXR sequence paths remain input-specific.");
        MessageBoxW(s->hwnd, (L"Parameters saved to:\n" + path.wstring()).c_str(), L"Crow-DLSS5-Video-Image-Converter", MB_OK | MB_ICONINFORMATION);
    } catch (const std::exception& e) {
        MessageBoxW(s->hwnd, ToWide(e.what()).c_str(), L"Save parameters failed", MB_ICONERROR);
    }
}

void UpdateExternalSummary(State* s) {
    const auto depthName = s->externalData.depth.firstFrame.empty()
        ? L"not selected" : s->externalData.depth.firstFrame.filename().wstring();
    const auto motionName = s->externalData.motion.firstFrame.empty()
        ? L"not selected" : s->externalData.motion.firstFrame.filename().wstring();
    const std::wstring depthChannel = s->externalData.depth.channel.empty() ? L"auto/unset" : ToWide(s->externalData.depth.channel);
    const std::wstring mx = s->externalData.motion.xChannel.empty() ? L"unset" : ToWide(s->externalData.motion.xChannel);
    const std::wstring my = s->externalData.motion.yChannel.empty() ? L"unset" : ToWide(s->externalData.motion.yChannel);
    const std::wstring direction = s->externalData.motion.direction == video::ExternalMotionDirection::PreviousToCurrent
        ? L"P->C/invert" : L"C->P";
    SetText(s->hwnd, IDC_EXTERNAL_SUMMARY, std::wstring(L"Depth: ") + depthName + L" [" + depthChannel + L"]   Motion: " +
            motionName + L" [" + mx + L", " + my + L"; " + direction + L"]");
}

void EditExternalData(State* s) {
    auto edited = s->externalData;
    if (video::EditExternalRenderDataSettings(s->hwnd, edited, s->inputPath)) {
        s->externalData = std::move(edited);
        UpdateExternalSummary(s);
        SetText(s->hwnd, IDC_STATUS, L"External Render Data settings updated. Sequence paths apply to this session only.");
    }
}

std::optional<std::filesystem::path> OpenVideo(HWND owner) {
    wchar_t file[32768]{};
    OPENFILENAMEW o{}; o.lStructSize = sizeof(o); o.hwndOwner = owner; o.lpstrFile = file; o.nMaxFile = ARRAYSIZE(file);
    o.lpstrFilter = L"Video Files\0*.mp4;*.mkv;*.mov;*.avi;*.webm;*.m4v;*.ts;*.mts;*.m2ts\0All Files\0*.*\0";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&o) ? std::optional<std::filesystem::path>(file) : std::nullopt;
}

std::optional<std::filesystem::path> SaveVideo(HWND owner, const std::filesystem::path& input) {
    wchar_t file[32768]{};
    const auto proposed = input.stem().wstring() + L"_DLSS5.mp4";
    wcsncpy_s(file, proposed.c_str(), _TRUNCATE);
    const auto dir = input.parent_path().wstring();
    OPENFILENAMEW o{}; o.lStructSize = sizeof(o); o.hwndOwner = owner; o.lpstrFile = file; o.nMaxFile = ARRAYSIZE(file);
    o.lpstrFilter = L"MP4 Video\0*.mp4\0Matroska Video\0*.mkv\0MOV Video\0*.mov\0";
    o.lpstrDefExt = L"mp4"; o.lpstrInitialDir = dir.empty() ? nullptr : dir.c_str();
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetSaveFileNameW(&o) ? std::optional<std::filesystem::path>(file) : std::nullopt;
}

std::wstring TimeText(double sec) {
    if (!std::isfinite(sec) || sec < 0) sec = 0;
    const int s = static_cast<int>(std::llround(sec));
    wchar_t b[64]{};
    swprintf_s(b, L"%02d:%02d:%02d", s / 3600, (s / 60) % 60, s % 60);
    return b;
}

void MakeDepthPreview(const std::vector<float>& d, uint32_t w, uint32_t h, Rgba8Image& out) {
    out.width = w; out.height = h; out.pixels.resize(static_cast<size_t>(w) * h * 4u);
    for (size_t i = 0; i < d.size(); ++i) {
        const uint8_t v = static_cast<uint8_t>(std::lround(std::clamp(d[i], 0.0f, 1.0f) * 255.0f));
        out.pixels[i*4+0] = v; out.pixels[i*4+1] = v; out.pixels[i*4+2] = v; out.pixels[i*4+3] = 255;
    }
}

void DrawImageFit(HDC dc, RECT area, const Rgba8Image& img) {
    if (!img.width || !img.height || img.pixels.empty()) return;
    const int aw = std::max(1, static_cast<int>(area.right - area.left));
    const int ah = std::max(1, static_cast<int>(area.bottom - area.top));
    const double scale = std::min(static_cast<double>(aw) / img.width, static_cast<double>(ah) / img.height);
    const int dw = std::max(1, static_cast<int>(std::lround(img.width * scale)));
    const int dh = std::max(1, static_cast<int>(std::lround(img.height * scale)));
    const int x = area.left + (aw - dw) / 2, y = area.top + (ah - dh) / 2;

    BITMAPV4HEADER b{};
    b.bV4Size = sizeof(b); b.bV4Width = static_cast<LONG>(img.width); b.bV4Height = -static_cast<LONG>(img.height);
    b.bV4Planes = 1; b.bV4BitCount = 32; b.bV4V4Compression = BI_BITFIELDS;
    b.bV4RedMask = 0x000000FF; b.bV4GreenMask = 0x0000FF00; b.bV4BlueMask = 0x00FF0000; b.bV4AlphaMask = 0xFF000000;
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc, x, y, dw, dh, 0, 0, img.width, img.height, img.pixels.data(),
                  reinterpret_cast<BITMAPINFO*>(&b), DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<PreviewContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ctx = static_cast<PreviewContext*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps);
        RECT c{}; GetClientRect(hwnd, &c);
        const int cw = std::max(1, static_cast<int>(c.right - c.left));
        const int ch = std::max(1, static_cast<int>(c.bottom - c.top));
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, cw, ch);
        HGDIOBJ old = SelectObject(mem, bmp);
        HBRUSH bg = CreateSolidBrush(RGB(18,20,24)); FillRect(mem, &c, bg); DeleteObject(bg);
        SetBkMode(mem, TRANSPARENT); SetTextColor(mem, RGB(235,238,244));
        std::wstring title = L"PREVIEW";
        if (ctx) {
            if (ctx->kind == 0) title = L"ORIGINAL - LIVE";
            else if (ctx->kind == 1) title = L"DLSS5 - LIVE";
            else if (ctx->kind == 2) title = L"DEPTH GUIDANCE - LIVE";
            else if (ctx->kind == 3) title = L"MOTION VECTORS - LIVE | HSV direction / magnitude";
        }
        RECT tr{8,4,std::max(9, static_cast<int>(c.right)-8),28}; DrawTextW(mem, title.c_str(), -1, &tr, DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        RECT body{8,30,std::max(9, static_cast<int>(c.right)-8),std::max(31, static_cast<int>(c.bottom)-8)};
        if (ctx && ctx->state) {
            std::lock_guard lock(ctx->state->mutex);
            const Rgba8Image* image = nullptr; bool has = false;
            if (ctx->kind == 0) { image=&ctx->state->original; has=ctx->state->hasOriginal; }
            else if (ctx->kind == 1) { image=&ctx->state->output; has=ctx->state->hasOutput; }
            else if (ctx->kind == 2) { image=&ctx->state->depth; has=ctx->state->hasDepth; }
            else { image=&ctx->state->motion; has=ctx->state->hasMotion; }
            if (has) DrawImageFit(mem, body, *image);
            else {
                SetTextColor(mem, RGB(140,145,155));
                const wchar_t* waiting = (ctx->kind == 3) ? L"Waiting for usable temporal motion vectors..." : L"Waiting for frames...";
                DrawTextW(mem, waiting, -1, &body, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
        }
        BitBlt(dc,0,0,c.right,c.bottom,mem,0,0,SRCCOPY);
        SelectObject(mem,old); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd,&ps); return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

void EnableJobControls(State* s, bool running) {
    const int ids[] = {IDC_INPUT_BROWSE,IDC_OUTPUT_BROWSE,IDC_DEPTH_MODE,IDC_DEPTH_SIZE,IDC_PRESET,IDC_STYLE,
        IDC_INTENSITY,IDC_LOCAL_TONE,IDC_LOCAL_STRUCTURE,IDC_SKIN_STRUCTURE,IDC_AUTO_MASK,IDC_UI_CORRECTION,
        IDC_ITERATIONS,IDC_TEMPORAL_MODE,IDC_FLOW_WIDTH,IDC_SCENE_CUT,IDC_MV_SCALE_X,IDC_MV_SCALE_Y,
        IDC_NVOF_QUALITY,IDC_NVOF_GRID,IDC_NVOF_TEMPORAL_HINTS,IDC_NVOF_OUTPUT_COST,IDC_DEPTH_STABILIZE,IDC_OUTPUT_STABILIZE,IDC_DENOISE_MODE,IDC_DENOISE_STRENGTH,IDC_DENOISE_HISTORY,
        IDC_DENOISE_SPATIAL,IDC_DENOISE_DETAIL,IDC_CODEC,IDC_QUALITY,IDC_SETUP_VIDEO,IDC_SETUP_DEPTH,IDC_RUNTIME,IDC_SAVE_PARAMETERS,IDC_EXTERNAL_DATA,IDC_START};
    for (int id : ids) EnableWindow(GetDlgItem(s->hwnd,id), !running);
    for (const auto& spec : kParameterSpecs) EnableWindow(GetDlgItem(s->hwnd, ResetButtonId(spec.id)), !running);
    EnableWindow(GetDlgItem(s->hwnd,IDC_CANCEL), running);
    if (!running) SyncTemporalUi(s);
}

void StartProbe(State* s, const std::filesystem::path& path) {
    if (s->probing) return;
    if (s->probeWorker.joinable()) s->probeWorker.join();
    s->probing = true;
    s->probeCancel.store(false, std::memory_order_relaxed);
    s->probeError.clear();
    EnableWindow(GetDlgItem(s->hwnd, IDC_INPUT_BROWSE), FALSE);
    EnableWindow(GetDlgItem(s->hwnd, IDC_START), FALSE);
    SetText(s->hwnd, IDC_INFO, L"Probing video metadata... (UI remains responsive)");
    s->probeWorker = std::thread([s, path]() {
        try {
            const auto info = video::ProbeVideo(path, 15000, &s->probeCancel);
            std::lock_guard lock(s->mutex);
            s->probeInfo = info;
            s->probeError.clear();
        } catch (const std::exception& e) {
            std::lock_guard lock(s->mutex);
            s->probeError = ToWide(e.what());
        }
        PostMessageW(s->hwnd, WM_APP_PROBE_FINISHED, 0, 0);
    });
}

video::VideoSettings ReadSettings(State* s) {
    video::VideoSettings v;
    v.input = s->inputPath; v.output = s->outputPath;
    switch (std::clamp(ComboSel(s->hwnd, IDC_DEPTH_MODE, 0), 0, 2)) {
    case 1: v.depthMode = video::DepthMode::AutoDepth; break;
    case 2: v.depthMode = video::DepthMode::ExternalExr; break;
    default: v.depthMode = video::DepthMode::Zero; break;
    }
    v.autoDepthSize = static_cast<uint32_t>(std::clamp(ReadInt(s->hwnd,IDC_DEPTH_SIZE,518),280,1036));
    v.externalData = s->externalData;
    v.dlss.preset = std::clamp(ComboSel(s->hwnd,IDC_PRESET,2),0,3);
    static constexpr int styleRaw[] = {0,2,1};
    v.dlss.style = styleRaw[std::clamp(ComboSel(s->hwnd,IDC_STYLE,1),0,2)];
    v.dlss.intensity = std::clamp(ReadFloat(s->hwnd,IDC_INTENSITY,0.60f),0.0f,2.0f);
    v.dlss.localToneStrength = std::clamp(ReadFloat(s->hwnd,IDC_LOCAL_TONE,0.80f),0.0f,2.0f);
    v.dlss.localStructureStrength = std::clamp(ReadFloat(s->hwnd,IDC_LOCAL_STRUCTURE,0.35f),0.0f,2.0f);
    v.dlss.skinStructureStrength = std::clamp(ReadFloat(s->hwnd,IDC_SKIN_STRUCTURE,0.0f),-1.0f,2.0f);
    v.dlss.autoMask = Button_GetCheck(GetDlgItem(s->hwnd,IDC_AUTO_MASK)) == BST_CHECKED;
    v.dlss.uiCorrection = Button_GetCheck(GetDlgItem(s->hwnd,IDC_UI_CORRECTION)) == BST_CHECKED;
    v.dlss.depthInverted = v.depthMode == video::DepthMode::AutoDepth ||
        (v.depthMode == video::DepthMode::ExternalExr && v.externalData.depth.depthInverted);
    const uint32_t its[] = {1,2,4,8}; v.dlss.iterations = its[std::clamp(ComboSel(s->hwnd,IDC_ITERATIONS,0),0,3)];
    switch (std::clamp(ComboSel(s->hwnd,IDC_TEMPORAL_MODE,1),0,4)) {
    case 0: v.temporalMode = video::TemporalMode::LegacyResetEveryFrame; break;
    case 2: v.temporalMode = video::TemporalMode::CpuFlow; break;
    case 3: v.temporalMode = video::TemporalMode::ExternalExr; break;
    case 4: v.temporalMode = video::TemporalMode::NvidiaOpticalFlow; break;
    default: v.temporalMode = video::TemporalMode::DisOpticalFlow; break;
    }
    switch (std::clamp(ComboSel(s->hwnd,IDC_NVOF_QUALITY,0),0,2)) {
    case 1: v.nvof.quality = video::NvofQuality::Medium; break;
    case 2: v.nvof.quality = video::NvofQuality::Fast; break;
    default: v.nvof.quality = video::NvofQuality::Slow; break;
    }
    switch (std::clamp(ComboSel(s->hwnd,IDC_NVOF_GRID,0),0,2)) {
    case 1: v.nvof.outputGridSize = 2; break;
    case 2: v.nvof.outputGridSize = 1; break;
    default: v.nvof.outputGridSize = 4; break;
    }
    v.nvof.temporalHints = Button_GetCheck(GetDlgItem(s->hwnd,IDC_NVOF_TEMPORAL_HINTS)) == BST_CHECKED;
    v.nvof.outputCost = Button_GetCheck(GetDlgItem(s->hwnd,IDC_NVOF_OUTPUT_COST)) == BST_CHECKED;
    v.flowAnalysisWidth = static_cast<uint32_t>(std::clamp(ReadInt(s->hwnd,IDC_FLOW_WIDTH,480),128,960));
    v.sceneCutThreshold = std::clamp(ReadFloat(s->hwnd,IDC_SCENE_CUT,0.28f),0.05f,0.95f);
    v.mvecScaleX = std::clamp(ReadFloat(s->hwnd,IDC_MV_SCALE_X,1.0f),-8.0f,8.0f);
    v.mvecScaleY = std::clamp(ReadFloat(s->hwnd,IDC_MV_SCALE_Y,1.0f),-8.0f,8.0f);
    v.temporalDepthStabilization = Button_GetCheck(GetDlgItem(s->hwnd,IDC_DEPTH_STABILIZE)) == BST_CHECKED;
    v.temporalOutputStabilization = Button_GetCheck(GetDlgItem(s->hwnd,IDC_OUTPUT_STABILIZE)) == BST_CHECKED;
    switch (std::clamp(ComboSel(s->hwnd,IDC_DENOISE_MODE,2),0,2)) {
    case 0: v.denoiseMode = video::DenoiseMode::Off; break;
    case 1: v.denoiseMode = video::DenoiseMode::TemporalHq; break;
    default: v.denoiseMode = video::DenoiseMode::FullHq; break;
    }
    v.denoiseStrength = std::clamp(ReadFloat(s->hwnd,IDC_DENOISE_STRENGTH,0.72f),0.0f,1.0f);
    v.denoiseHistoryWeight = std::clamp(ReadFloat(s->hwnd,IDC_DENOISE_HISTORY,0.76f),0.0f,0.98f);
    v.denoiseSpatialStrength = std::clamp(ReadFloat(s->hwnd,IDC_DENOISE_SPATIAL,0.22f),0.0f,1.0f);
    v.denoiseDetailProtection = std::clamp(ReadFloat(s->hwnd,IDC_DENOISE_DETAIL,0.86f),0.0f,1.0f);
    if (v.temporalMode != video::TemporalMode::LegacyResetEveryFrame) v.dlss.iterations = 1;
    v.codec = static_cast<video::VideoCodec>(std::clamp(ComboSel(s->hwnd,IDC_CODEC,0),0,2));
    v.quality = std::clamp(ReadInt(s->hwnd,IDC_QUALITY,18),0,51);
    return v;
}

void UpdateProgressUi(State* s) {
    video::VideoProgress p;
    { std::lock_guard lock(s->mutex); p = s->progress; }
    const int pos = static_cast<int>(std::lround(std::clamp(p.fraction,0.0,1.0)*1000.0));
    SendMessageW(GetDlgItem(s->hwnd,IDC_PROGRESS),PBM_SETPOS,pos,0);
    wchar_t b[512]{};
    swprintf_s(b,L"%.1f%%   Frame %llu / %llu   Speed %.2f fps   Elapsed %s   ETA %s",
        p.fraction*100.0, static_cast<unsigned long long>(p.frameIndex), static_cast<unsigned long long>(p.totalFrames),
        p.processingFps, TimeText(p.elapsedSeconds).c_str(), TimeText(p.etaSeconds).c_str());
    SetText(s->hwnd,IDC_PROGRESS_TEXT,b);
    SetText(s->hwnd,IDC_STATUS,p.message);
}

void StartJob(State* s) {
    if (s->running) return;
    if (s->probing) { MessageBoxW(s->hwnd,L"Video metadata is still being probed. Please wait a moment.",L"Crow-DLSS5-Video-Image-Converter",MB_ICONINFORMATION); return; }
    if (s->inputPath.empty() || s->outputPath.empty()) { MessageBoxW(s->hwnd,L"Choose input and output video paths first.",L"Crow-DLSS5-Video-Image-Converter",MB_ICONWARNING); return; }
    auto settings = ReadSettings(s);
    s->cancel.store(false); s->error.clear(); s->running=true; EnableJobControls(s,true);
    SendMessageW(GetDlgItem(s->hwnd,IDC_PROGRESS),PBM_SETPOS,0,0);
    s->worker = std::thread([s, settings=std::move(settings)]() mutable {
        try {
            video::VideoCallbacks cb;
            cb.onProgress = [s](const video::VideoProgress& p) {
                { std::lock_guard lock(s->mutex); s->progress=p; }
                PostMessageW(s->hwnd,WM_APP_PROGRESS,0,0);
            };
            cb.onPreview = [s](const Rgba8Image& a, const Rgba8Image& b, const std::vector<float>* d,
                               const std::vector<float>* motion, float motionScaleX, float motionScaleY) {
                {
                    std::lock_guard lock(s->mutex);
                    s->original=a; s->output=b; s->hasOriginal=true; s->hasOutput=true;
                    if (d) { MakeDepthPreview(*d,a.width,a.height,s->depth); s->hasDepth=true; }
                    else s->hasDepth=false;
                    if (motion) {
                        auto preview = video::BuildMotionPreview(*motion, a.width, a.height, motionScaleX, motionScaleY, 960);
                        s->motion = std::move(preview.image);
                        s->motionPreviewP95 = preview.robustMagnitudePixels;
                        s->hasMotion = !s->motion.pixels.empty();
                    } else {
                        s->motion = {};
                        s->motionPreviewP95 = 0.0f;
                        s->hasMotion = false;
                    }
                }
                PostMessageW(s->hwnd,WM_APP_PREVIEW,0,0);
            };
            video::ConvertVideo(settings,cb,s->cancel);
        } catch (const std::exception& e) {
            std::lock_guard lock(s->mutex); s->error=ToWide(e.what());
        }
        PostMessageW(s->hwnd,WM_APP_FINISHED,0,0);
    });
}

void Layout(State* s) {
    RECT c{}; GetClientRect(s->hwnd,&c);
    const int W=std::max(1, static_cast<int>(c.right-c.left));
    const int H=std::max(1, static_cast<int>(c.bottom-c.top));
    const int side=std::min(SIDEBAR,std::max(330,W/3));
    const int scrollBarW=17, scrollGap=6;
    const int scrollX=std::max(314,side-scrollBarW-5);
    const int x=12, w=std::max(260,scrollX-scrollGap-x), label=112, row=24;
    const int scrollOffset=s->sidebarScrollY;
    int y=10;
    const int resetW=50, gap=6, valueX=x+label, resetX=x+w-resetW;
    const int standardValueW=std::max(60,resetX-gap-valueX);
    auto moveFixed=[&](int id,int X,int Y,int ww,int hh){ MoveWindow(GetDlgItem(s->hwnd,id),X,Y,std::max(1,ww),std::max(1,hh),TRUE); };
    auto move=[&](int id,int X,int Y,int ww,int hh){ moveFixed(id,X,Y-scrollOffset,ww,hh); };
    auto reset=[&](int parameterId,int X,int Y){ move(ResetButtonId(parameterId),X,Y,resetW,row); };
    auto standard=[&](int parameterId,int Y){ move(parameterId,valueX,Y,standardValueW,row); reset(parameterId,resetX,Y); };
    auto standardH=[&](int parameterId,int Y,int hh){ move(parameterId,valueX,Y,standardValueW,hh); reset(parameterId,resetX,Y); };
    auto pairChecks=[&](int leftId,int rightId,int Y){
        const int half=(w-gap)/2; const int checkW=std::max(70,half-resetW-gap);
        move(leftId,x,Y,checkW,row); reset(leftId,x+checkW+gap,Y);
        const int rx=x+half+gap; move(rightId,rx,Y,checkW,row); reset(rightId,rx+checkW+gap,Y);
    };
    move(IDC_INPUT,x,y,w-86,row); move(IDC_INPUT_BROWSE,x+w-80,y,80,row); y+=30;
    move(IDC_OUTPUT,x,y,w-86,row); move(IDC_OUTPUT_BROWSE,x+w-80,y,80,row); y+=30;
    move(IDC_INFO,x,y,w,42); y+=48;
    move(IDC_SETUP_VIDEO,x,y,w/2-4,row); move(IDC_SETUP_DEPTH,x+w/2+4,y,w/2-4,row); y+=30;
    move(IDC_RUNTIME,x,y,w,row); y+=30;
    move(IDC_SAVE_PARAMETERS,x,y,w,row); y+=30;
    move(IDC_EXTERNAL_DATA,x,y,w,row); y+=28;
    move(IDC_EXTERNAL_SUMMARY,x,y,w,34); y+=40;
    standardH(IDC_DEPTH_MODE,y,row*6); y+=30;
    standard(IDC_DEPTH_SIZE,y); y+=34;
    standardH(IDC_PRESET,y,150); y+=30;
    standardH(IDC_STYLE,y,130); y+=30;
    standard(IDC_INTENSITY,y); y+=28;
    standard(IDC_LOCAL_TONE,y); y+=28;
    standard(IDC_LOCAL_STRUCTURE,y); y+=28;
    standard(IDC_SKIN_STRUCTURE,y); y+=28;
    pairChecks(IDC_AUTO_MASK,IDC_UI_CORRECTION,y); y+=30;
    standardH(IDC_TEMPORAL_MODE,y,120); y+=30;
    standardH(IDC_NVOF_QUALITY,y,100); y+=30;
    standardH(IDC_NVOF_GRID,y,100); y+=30;
    pairChecks(IDC_NVOF_TEMPORAL_HINTS,IDC_NVOF_OUTPUT_COST,y); y+=30;
    standard(IDC_FLOW_WIDTH,y); y+=28;
    standard(IDC_SCENE_CUT,y); y+=28;
    {
        const int avail=w-label; const int unit=(avail-gap)/2; const int fieldW=std::max(44,unit-resetW-gap);
        const int x1=valueX; move(IDC_MV_SCALE_X,x1,y,fieldW,row); reset(IDC_MV_SCALE_X,x1+fieldW+gap,y);
        const int x2=valueX+unit+gap; move(IDC_MV_SCALE_Y,x2,y,fieldW,row); reset(IDC_MV_SCALE_Y,x2+fieldW+gap,y);
    }
    y+=28;
    pairChecks(IDC_DEPTH_STABILIZE,IDC_OUTPUT_STABILIZE,y); y+=30;
    standardH(IDC_DENOISE_MODE,y,120); y+=30;
    standard(IDC_DENOISE_STRENGTH,y); y+=28;
    standard(IDC_DENOISE_HISTORY,y); y+=28;
    standard(IDC_DENOISE_SPATIAL,y); y+=28;
    standard(IDC_DENOISE_DETAIL,y); y+=30;
    standardH(IDC_ITERATIONS,y,120); y+=30;
    standardH(IDC_CODEC,y,140); y+=30;
    standard(IDC_QUALITY,y); y+=36;
    move(IDC_START,x,y,w/2-4,30); move(IDC_CANCEL,x+w/2+4,y,w/2-4,30); y+=38;
    move(IDC_PROGRESS,x,y,w,22); y+=26;
    move(IDC_PROGRESS_TEXT,x,y,w,42); y+=46;
    constexpr int statusH=68;
    move(IDC_STATUS,x,y,w,statusH); y+=statusH+10;

    s->sidebarContentHeight=y;
    HWND sidebarScroll=GetDlgItem(s->hwnd,IDC_SIDEBAR_SCROLL);
    moveFixed(IDC_SIDEBAR_SCROLL,scrollX,8,scrollBarW,std::max(24,H-16));
    const int maxScroll=std::max(0,s->sidebarContentHeight-H);
    const int clampedScroll=std::clamp(s->sidebarScrollY,0,maxScroll);
    SCROLLINFO scrollInfo{};
    scrollInfo.cbSize=sizeof(scrollInfo);
    scrollInfo.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
    scrollInfo.nMin=0;
    scrollInfo.nMax=std::max(0,s->sidebarContentHeight-1);
    scrollInfo.nPage=static_cast<UINT>(std::max(1,H));
    scrollInfo.nPos=clampedScroll;
    SetScrollInfo(sidebarScroll,SB_CTL,&scrollInfo,TRUE);
    ShowWindow(sidebarScroll,maxScroll>0?SW_SHOW:SW_HIDE);
    EnableWindow(sidebarScroll,maxScroll>0);
    if(clampedScroll!=s->sidebarScrollY){
        s->sidebarScrollY=clampedScroll;
        Layout(s);
        return;
    }

    // V0.6.5.3 preview matrix: Original / DLSS5 on the first row,
    // Depth / Motion on the second row. All four panes receive equal logical area.
    const int px=side+8, pw=std::max(200,W-px-8), gapPreview=8;
    const int availableH=std::max(200,H-16);
    const int leftW=std::max(100,(pw-gapPreview)/2);
    const int rightW=std::max(100,pw-leftW-gapPreview);
    const int topH=std::max(100,(availableH-gapPreview)/2);
    const int bottomH=std::max(100,availableH-topH-gapPreview);
    const int bottomY=8+topH+gapPreview;
    MoveWindow(s->previewOriginal,px,8,leftW,topH,TRUE);
    MoveWindow(s->previewOutput,px+leftW+gapPreview,8,rightW,topH,TRUE);
    MoveWindow(s->previewDepth,px,bottomY,leftW,bottomH,TRUE);
    MoveWindow(s->previewMotion,px+leftW+gapPreview,bottomY,rightW,bottomH,TRUE);
}

void AddLabelAt(HWND h, const wchar_t* text, int x, int& y, int labelId, int controlId, const wchar_t* controlClass,
                const wchar_t* controlText, DWORD controlStyle) {
    Label(h,text,labelId); Make(h,controlClass,controlText,controlStyle,controlId); (void)x; (void)y;
}

void CreateControls(State* s) {
    HWND h=s->hwnd;
    Make(h,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|ES_READONLY,IDC_INPUT); Make(h,L"BUTTON",L"Input...",BS_PUSHBUTTON,IDC_INPUT_BROWSE);
    Make(h,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|ES_READONLY,IDC_OUTPUT); Make(h,L"BUTTON",L"Output...",BS_PUSHBUTTON,IDC_OUTPUT_BROWSE);
    Make(h,L"STATIC",L"Choose a video to probe resolution / FPS / duration.",SS_LEFT,IDC_INFO);
    Make(h,L"BUTTON",L"Setup FFmpeg",BS_PUSHBUTTON,IDC_SETUP_VIDEO);
    Make(h,L"BUTTON",L"Setup Auto Depth / Temporal",BS_PUSHBUTTON,IDC_SETUP_DEPTH);
    Make(h,L"BUTTON",L"Runtime DLL...",BS_PUSHBUTTON,IDC_RUNTIME);
    Make(h,L"BUTTON",L"Save Parameters",BS_PUSHBUTTON,IDC_SAVE_PARAMETERS);
    Make(h,L"BUTTON",L"External Render Data...",BS_PUSHBUTTON,IDC_EXTERNAL_DATA);
    Make(h,L"STATIC",L"Depth: not selected   Motion: not selected",SS_LEFT,IDC_EXTERNAL_SUMMARY);

    // Create labels as ordinary static controls with IDs 1000 + control ID.
    auto field=[&](int id,const wchar_t* name,const wchar_t* cls,const wchar_t* text,DWORD style){ Label(h,name,1000+id); Make(h,cls,text,style,id); };
    field(IDC_DEPTH_MODE,L"Depth Mode",WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL); Combo(h,IDC_DEPTH_MODE,{L"Zero Depth",L"Auto Depth - Depth Anything V2",L"External EXR Sequence"},0);
    field(IDC_DEPTH_SIZE,L"DAV2 Size",L"EDIT",L"518",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_PRESET,L"NR Preset",WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL); Combo(h,IDC_PRESET,{L"Default",L"Preset #1",L"Preset #2",L"Preset #3"},2);
    field(IDC_STYLE,L"NR Style",WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL); Combo(h,IDC_STYLE,{L"Default",L"Natural",L"Cinematic"},1);
    field(IDC_INTENSITY,L"Intensity",L"EDIT",L"0.60",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_LOCAL_TONE,L"Local Tone",L"EDIT",L"0.80",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_LOCAL_STRUCTURE,L"Local Structure",L"EDIT",L"0.35",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_SKIN_STRUCTURE,L"Skin Structure",L"EDIT",L"0.0",WS_BORDER|ES_AUTOHSCROLL);
    Make(h,L"BUTTON",L"Auto Skin Mask",BS_AUTOCHECKBOX,IDC_AUTO_MASK); Button_SetCheck(GetDlgItem(h,IDC_AUTO_MASK),BST_UNCHECKED);
    Make(h,L"BUTTON",L"UI Correction",BS_AUTOCHECKBOX,IDC_UI_CORRECTION); Button_SetCheck(GetDlgItem(h,IDC_UI_CORRECTION),BST_UNCHECKED);
    field(IDC_TEMPORAL_MODE,L"Temporal",WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL); Combo(h,IDC_TEMPORAL_MODE,{L"Legacy - Reset Every Frame",L"Adaptive DIS - Stable Default",L"CPU Block Flow - Fallback",L"External EXR Motion - CG Ground Truth",L"NVIDIA Optical Flow - NVOF D3D12 Alpha"},1);
    field(IDC_NVOF_QUALITY,L"NVOF Quality",WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL); Combo(h,IDC_NVOF_QUALITY,{L"Quality / Slow",L"Balanced / Medium",L"Performance / Fast"},0);
    field(IDC_NVOF_GRID,L"NVOF Grid",WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL); Combo(h,IDC_NVOF_GRID,{L"4x4 - Validated Stable",L"2x2 - Finer",L"1x1 - Finest"},0);
    Make(h,L"BUTTON",L"NVOF Temporal Hints",BS_AUTOCHECKBOX,IDC_NVOF_TEMPORAL_HINTS); Button_SetCheck(GetDlgItem(h,IDC_NVOF_TEMPORAL_HINTS),BST_CHECKED);
    Make(h,L"BUTTON",L"NVOF Output Cost",BS_AUTOCHECKBOX,IDC_NVOF_OUTPUT_COST); Button_SetCheck(GetDlgItem(h,IDC_NVOF_OUTPUT_COST),BST_CHECKED);
    field(IDC_FLOW_WIDTH,L"Flow Width",L"EDIT",L"480",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_SCENE_CUT,L"Cut Threshold",L"EDIT",L"0.28",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_MV_SCALE_X,L"MV Scale X/Y",L"EDIT",L"1.0",WS_BORDER|ES_AUTOHSCROLL); Make(h,L"EDIT",L"1.0",WS_BORDER|ES_AUTOHSCROLL,IDC_MV_SCALE_Y);
    Make(h,L"BUTTON",L"Stable Depth",BS_AUTOCHECKBOX,IDC_DEPTH_STABILIZE); Button_SetCheck(GetDlgItem(h,IDC_DEPTH_STABILIZE),BST_CHECKED);
    Make(h,L"BUTTON",L"Stable Output",BS_AUTOCHECKBOX,IDC_OUTPUT_STABILIZE); Button_SetCheck(GetDlgItem(h,IDC_OUTPUT_STABILIZE),BST_UNCHECKED);
    field(IDC_DENOISE_MODE,L"Denoise",WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL); Combo(h,IDC_DENOISE_MODE,{L"Off",L"Temporal HQ",L"Full HQ - Recommended"},2);
    field(IDC_DENOISE_STRENGTH,L"DN Strength",L"EDIT",L"0.72",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_DENOISE_HISTORY,L"DN History",L"EDIT",L"0.76",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_DENOISE_SPATIAL,L"DN Spatial",L"EDIT",L"0.22",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_DENOISE_DETAIL,L"Detail Protect",L"EDIT",L"0.86",WS_BORDER|ES_AUTOHSCROLL);
    field(IDC_ITERATIONS,L"Iterations",WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL); Combo(h,IDC_ITERATIONS,{L"1",L"2",L"4",L"8"},0);
    field(IDC_CODEC,L"Encoder",WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_VSCROLL); Combo(h,IDC_CODEC,{L"H.264 NVENC",L"HEVC NVENC",L"H.264 CPU (libx264)"},0);
    field(IDC_QUALITY,L"CQ / CRF",L"EDIT",L"18",WS_BORDER|ES_AUTOHSCROLL);
    for (const auto& spec : kParameterSpecs) Make(h,L"BUTTON",L"Reset",BS_PUSHBUTTON,ResetButtonId(spec.id));
    Make(h,L"BUTTON",L"START CONVERSION",BS_DEFPUSHBUTTON,IDC_START); Make(h,L"BUTTON",L"CANCEL",BS_PUSHBUTTON,IDC_CANCEL); EnableWindow(GetDlgItem(h,IDC_CANCEL),FALSE);
    Make(h,PROGRESS_CLASSW,L"",PBS_SMOOTH,IDC_PROGRESS); SendMessageW(GetDlgItem(h,IDC_PROGRESS),PBM_SETRANGE32,0,1000);
    Make(h,L"STATIC",L"0.0%",SS_LEFT,IDC_PROGRESS_TEXT); Make(h,L"STATIC",L"Ready.",SS_LEFT,IDC_STATUS);
    Make(h,L"SCROLLBAR",L"",SBS_VERT,IDC_SIDEBAR_SCROLL);

    s->previewCtx[0]={s,0}; s->previewCtx[1]={s,1}; s->previewCtx[2]={s,2}; s->previewCtx[3]={s,3};
    HINSTANCE inst=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(h,GWLP_HINSTANCE));
    s->previewOriginal=CreateWindowExW(0,PREVIEW_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_BORDER,0,0,10,10,h,reinterpret_cast<HMENU>(IDC_PREVIEW_ORIGINAL),inst,&s->previewCtx[0]);
    s->previewOutput=CreateWindowExW(0,PREVIEW_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_BORDER,0,0,10,10,h,reinterpret_cast<HMENU>(IDC_PREVIEW_OUTPUT),inst,&s->previewCtx[1]);
    s->previewDepth=CreateWindowExW(0,PREVIEW_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_BORDER,0,0,10,10,h,reinterpret_cast<HMENU>(IDC_PREVIEW_DEPTH),inst,&s->previewCtx[2]);
    s->previewMotion=CreateWindowExW(0,PREVIEW_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_BORDER,0,0,10,10,h,reinterpret_cast<HMENU>(IDC_PREVIEW_MOTION),inst,&s->previewCtx[3]);
    HFONT font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for(HWND ch=GetWindow(h,GW_CHILD);ch;ch=GetWindow(ch,GW_HWNDNEXT)) {
        SendMessageW(ch,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        const int childId=GetDlgCtrlID(ch);
        if(childId!=IDC_PREVIEW_ORIGINAL && childId!=IDC_PREVIEW_OUTPUT && childId!=IDC_PREVIEW_DEPTH && childId!=IDC_PREVIEW_MOTION && childId!=IDC_SIDEBAR_SCROLL)
            SetWindowSubclass(ch,SidebarChildSubclassProc,1,reinterpret_cast<DWORD_PTR>(s));
    }
    for (const auto& spec : kParameterSpecs) ApplyParameterValue(h, spec, spec.factoryValue);
    const bool loadedSavedParameters = LoadSavedParameters(s);
    SyncTemporalUi(s);
    UpdateExternalSummary(s);
    if (loadedSavedParameters) SetText(h,IDC_STATUS,L"Saved parameters loaded. Per-parameter Reset restores factory defaults.");
}

void LayoutLabels(State* s) {
    // Synchronize label positions with the scrollable sidebar fields after Layout().
    const int x=12,label=112,scrollOffset=s->sidebarScrollY;
    int y=10+30+30+48+30+30+30+28+40;
    auto place=[&](int control,int advance){ MoveWindow(GetDlgItem(s->hwnd,1000+control),x,y-scrollOffset,label-6,24,TRUE); y+=advance; };
    place(IDC_DEPTH_MODE,30); place(IDC_DEPTH_SIZE,34); place(IDC_PRESET,30); place(IDC_STYLE,30);
    place(IDC_INTENSITY,28); place(IDC_LOCAL_TONE,28); place(IDC_LOCAL_STRUCTURE,28); place(IDC_SKIN_STRUCTURE,28);
    y+=30; place(IDC_TEMPORAL_MODE,30); place(IDC_NVOF_QUALITY,30); place(IDC_NVOF_GRID,30); y+=30;
    place(IDC_FLOW_WIDTH,28); place(IDC_SCENE_CUT,28); place(IDC_MV_SCALE_X,28);
    y+=30; place(IDC_DENOISE_MODE,30); place(IDC_DENOISE_STRENGTH,28); place(IDC_DENOISE_HISTORY,28);
    place(IDC_DENOISE_SPATIAL,28); place(IDC_DENOISE_DETAIL,30); place(IDC_ITERATIONS,30); place(IDC_CODEC,30); place(IDC_QUALITY,36);
}

void SetSidebarScrollPosition(State* s, int requestedPosition) {
    if(!s || !s->hwnd) return;
    RECT c{}; GetClientRect(s->hwnd,&c);
    const int viewportHeight=std::max(1,static_cast<int>(c.bottom-c.top));
    const int maxScroll=std::max(0,s->sidebarContentHeight-viewportHeight);
    const int next=std::clamp(requestedPosition,0,maxScroll);
    if(next==s->sidebarScrollY) return;

    // Do not call Layout()/LayoutLabels() for every wheel tick. Re-running the full
    // layout repeatedly resizes Win32 combo boxes and moves labels/controls in two
    // separate passes. On Windows that can leave stale child-window paint and can
    // make labels, Reset buttons and combo edit portions appear to drift or clip.
    // Instead, move every sidebar child by one identical delta in one DeferWindowPos
    // batch. Logical coordinates are still rebuilt from scratch by Layout() on WM_SIZE.
    const int old=s->sidebarScrollY;
    const int deltaY=old-next;
    s->sidebarScrollY=next;

    RECT client{}; GetClientRect(s->hwnd,&client);
    const int W=std::max(1,static_cast<int>(client.right-client.left));
    const int side=std::min(SIDEBAR,std::max(330,W/3));
    const int scrollBarW=17;
    const int scrollX=std::max(314,side-scrollBarW-5);

    std::vector<HWND> children;
    children.reserve(96);
    for(HWND ch=GetWindow(s->hwnd,GW_CHILD); ch; ch=GetWindow(ch,GW_HWNDNEXT)) {
        const int id=GetDlgCtrlID(ch);
        if(id==IDC_PREVIEW_ORIGINAL || id==IDC_PREVIEW_OUTPUT || id==IDC_PREVIEW_DEPTH || id==IDC_PREVIEW_MOTION || id==IDC_SIDEBAR_SCROLL)
            continue;
        RECT r{}; GetWindowRect(ch,&r);
        MapWindowPoints(HWND_DESKTOP,s->hwnd,reinterpret_cast<POINT*>(&r),2);
        // Every ordinary sidebar child is wholly to the left of the fixed scrollbar.
        // The x test also prevents future fixed right-hand children from being moved.
        if(r.left<scrollX) children.push_back(ch);
    }

    bool batchApplied=false;
    HDWP batch=BeginDeferWindowPos(static_cast<int>(children.size()));
    if(batch) {
        for(HWND ch:children) {
            RECT r{}; GetWindowRect(ch,&r);
            MapWindowPoints(HWND_DESKTOP,s->hwnd,reinterpret_cast<POINT*>(&r),2);
            batch=DeferWindowPos(batch,ch,nullptr,r.left,r.top+deltaY,0,0,
                SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
            if(!batch) break;
        }
        if(batch) batchApplied=EndDeferWindowPos(batch)!=FALSE;
    }

    // If the defer batch could not be allocated/completed, use a deterministic
    // fallback that still applies the exact same delta to all sidebar children.
    if(!batchApplied) {
        for(HWND ch:children) {
            RECT r{}; GetWindowRect(ch,&r);
            MapWindowPoints(HWND_DESKTOP,s->hwnd,reinterpret_cast<POINT*>(&r),2);
            SetWindowPos(ch,nullptr,r.left,r.top+deltaY,0,0,
                SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
        }
    }

    if(HWND bar=GetDlgItem(s->hwnd,IDC_SIDEBAR_SCROLL)) {
        SCROLLINFO si{}; si.cbSize=sizeof(si); si.fMask=SIF_POS; si.nPos=next;
        SetScrollInfo(bar,SB_CTL,&si,TRUE);
    }

    RECT sidebarRect{0,0,scrollX,viewportHeight};
    RedrawWindow(s->hwnd,&sidebarRect,nullptr,
        RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
}

void SyncTemporalUi(State* s) {
    const int depthMode = ComboSel(s->hwnd, IDC_DEPTH_MODE, 0);
    const int temporalMode = ComboSel(s->hwnd, IDC_TEMPORAL_MODE, 1);
    const bool temporal = temporalMode != 0;
    const bool nvof = temporalMode == 4;
    const bool flowWidthRelevant = temporal && temporalMode != 4;
    auto enableWithReset=[&](int id,bool enabled){ EnableWindow(GetDlgItem(s->hwnd,id),enabled); EnableWindow(GetDlgItem(s->hwnd,ResetButtonId(id)),enabled); };
    enableWithReset(IDC_DEPTH_SIZE, depthMode == 1 && !s->running);
    EnableWindow(GetDlgItem(s->hwnd, IDC_EXTERNAL_DATA), !s->running);
    enableWithReset(IDC_NVOF_QUALITY, nvof && !s->running);
    enableWithReset(IDC_NVOF_GRID, nvof && !s->running);
    enableWithReset(IDC_NVOF_TEMPORAL_HINTS, nvof && !s->running);
    enableWithReset(IDC_NVOF_OUTPUT_COST, nvof && !s->running);
    enableWithReset(IDC_FLOW_WIDTH, flowWidthRelevant && !s->running);
    enableWithReset(IDC_SCENE_CUT, temporal && !s->running);
    enableWithReset(IDC_MV_SCALE_X, temporal && !s->running);
    enableWithReset(IDC_MV_SCALE_Y, temporal && !s->running);
    enableWithReset(IDC_DEPTH_STABILIZE, temporal && !s->running);
    enableWithReset(IDC_OUTPUT_STABILIZE, temporal && !s->running);
    const bool denoise = ComboSel(s->hwnd, IDC_DENOISE_MODE, 2) != 0;
    enableWithReset(IDC_DENOISE_MODE, !s->running);
    enableWithReset(IDC_DENOISE_STRENGTH, denoise && !s->running);
    enableWithReset(IDC_DENOISE_HISTORY, denoise && !s->running);
    enableWithReset(IDC_DENOISE_SPATIAL, denoise && !s->running);
    enableWithReset(IDC_DENOISE_DETAIL, denoise && !s->running);
    enableWithReset(IDC_ITERATIONS, !temporal && !s->running);
}

void RunSetupVideo(State* s) {
    const auto script=app::ExecutableDir()/L"video"/L"setup_video.ps1";
    if(!std::filesystem::exists(script)){MessageBoxW(s->hwnd,L"setup_video.ps1 is missing.",L"Video Dependencies",MB_ICONERROR);return;}
    std::wstring args=L"-NoProfile -ExecutionPolicy Bypass -File \""+script.wstring()+L"\"";
    ShellExecuteW(s->hwnd,L"open",L"powershell.exe",args.c_str(),script.parent_path().c_str(),SW_SHOWNORMAL);
}

void RunSetupDepth(State* s) {
    const auto script=app::ExecutableDir()/L"auto_depth"/L"setup_auto_depth.ps1";
    if(!std::filesystem::exists(script)){MessageBoxW(s->hwnd,L"setup_auto_depth.ps1 is missing.",L"Auto Depth / Temporal",MB_ICONERROR);return;}
    std::wstring args=L"-NoProfile -ExecutionPolicy Bypass -File \""+script.wstring()+L"\"";
    ShellExecuteW(s->hwnd,L"open",L"powershell.exe",args.c_str(),script.parent_path().c_str(),SW_SHOWNORMAL);
}

void ImportRuntime(State* s) {
    wchar_t file[32768]{}; OPENFILENAMEW o{}; o.lStructSize=sizeof(o);o.hwndOwner=s->hwnd;o.lpstrFile=file;o.nMaxFile=ARRAYSIZE(file);
    o.lpstrFilter=L"DLSSNR Runtime\0nvngx_dlssnr.dll\0DLL Files\0*.dll\0";o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;
    if(!GetOpenFileNameW(&o))return;
    try { const auto dst=app::DefaultRuntimeDll();std::filesystem::create_directories(dst.parent_path());std::filesystem::copy_file(file,dst,std::filesystem::copy_options::overwrite_existing);MessageBoxW(s->hwnd,L"Runtime imported.",L"DLSS5",MB_OK|MB_ICONINFORMATION); }
    catch(const std::exception&e){MessageBoxW(s->hwnd,ToWide(e.what()).c_str(),L"Runtime import failed",MB_ICONERROR);}
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){auto*cs=reinterpret_cast<CREATESTRUCTW*>(lp);s=static_cast<State*>(cs->lpCreateParams);s->hwnd=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(s));}
    switch(msg){
    case WM_CREATE: CreateControls(s); Layout(s); LayoutLabels(s); return 0;
    case WM_SIZE: if(s){Layout(s);LayoutLabels(s);} return 0;
    case WM_GETMINMAXINFO: {auto*m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={1180,760};return 0;}
    case WM_VSCROLL: if(s && reinterpret_cast<HWND>(lp)==GetDlgItem(hwnd,IDC_SIDEBAR_SCROLL)) {
        int next=s->sidebarScrollY;
        SCROLLINFO si{}; si.cbSize=sizeof(si); si.fMask=SIF_TRACKPOS;
        GetScrollInfo(reinterpret_cast<HWND>(lp),SB_CTL,&si);
        switch(LOWORD(wp)){
        case SB_LINEUP: next-=28; break;
        case SB_LINEDOWN: next+=28; break;
        case SB_PAGEUP: {RECT c{};GetClientRect(hwnd,&c);next-=std::max(120,static_cast<int>(c.bottom-c.top)-80);break;}
        case SB_PAGEDOWN: {RECT c{};GetClientRect(hwnd,&c);next+=std::max(120,static_cast<int>(c.bottom-c.top)-80);break;}
        case SB_THUMBPOSITION:
        case SB_THUMBTRACK: next=si.nTrackPos; break;
        case SB_TOP: next=0; break;
        case SB_BOTTOM: next=s->sidebarContentHeight; break;
        default: return 0;
        }
        SetSidebarScrollPosition(s,next);
        return 0;
    }
    case WM_MOUSEWHEEL: if(s) {
        POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd,&pt);
        RECT c{};GetClientRect(hwnd,&c);
        const int side=std::min(SIDEBAR,std::max(330,static_cast<int>(c.right-c.left)/3));
        if(pt.x>=0 && pt.x<side){
            const int wheel=GET_WHEEL_DELTA_WPARAM(wp);
            if(wheel!=0){
                const int steps=std::max(1,std::abs(wheel)/WHEEL_DELTA);
                SetSidebarScrollPosition(s,s->sidebarScrollY+(wheel>0?-1:1)*steps*84);
                return 0;
            }
        }
        break;
    }
    case WM_COMMAND: if(!s)break; {
        const int commandId = LOWORD(wp);
        if (commandId >= RESET_BUTTON_BASE) {
            const int parameterId = commandId - RESET_BUTTON_BASE;
            if (FindParameterSpec(parameterId)) { ResetOneParameter(s, parameterId); return 0; }
        }
        switch(commandId){
        case IDC_INPUT_BROWSE: if(auto p=OpenVideo(hwnd)){s->inputPath=*p;SetText(hwnd,IDC_INPUT,p->wstring());if(s->outputPath.empty()){s->outputPath=p->parent_path()/(p->stem().wstring()+L"_DLSS5.mp4");SetText(hwnd,IDC_OUTPUT,s->outputPath.wstring());}StartProbe(s,*p);} return 0;
        case IDC_OUTPUT_BROWSE: if(auto p=SaveVideo(hwnd,s->inputPath)){s->outputPath=*p;SetText(hwnd,IDC_OUTPUT,p->wstring());} return 0;
        case IDC_SETUP_VIDEO: RunSetupVideo(s); return 0;
        case IDC_SETUP_DEPTH: RunSetupDepth(s); return 0;
        case IDC_RUNTIME: ImportRuntime(s); return 0;
        case IDC_SAVE_PARAMETERS: SaveParameters(s); return 0;
        case IDC_EXTERNAL_DATA: EditExternalData(s); return 0;
        case IDC_DEPTH_MODE: if(HIWORD(wp)==CBN_SELCHANGE) {
            if (ComboSel(s->hwnd, IDC_DEPTH_MODE, 0) == 2)
                Button_SetCheck(GetDlgItem(s->hwnd,IDC_DEPTH_STABILIZE), BST_UNCHECKED);
            SyncTemporalUi(s);
        } return 0;
        case IDC_TEMPORAL_MODE: if(HIWORD(wp)==CBN_SELCHANGE) SyncTemporalUi(s); return 0;
        case IDC_DENOISE_MODE: if(HIWORD(wp)==CBN_SELCHANGE) SyncTemporalUi(s); return 0;
        case IDC_START: StartJob(s); return 0;
        case IDC_CANCEL: s->cancel.store(true);SetText(hwnd,IDC_STATUS,L"Cancelling safely... current GPU/FFmpeg operation will finish before teardown.");return 0;
        }
    } break;
    case WM_APP_PROBE_FINISHED: if(s){
        if(s->probeWorker.joinable())s->probeWorker.join();
        s->probing=false;
        std::wstring err; video::VideoInfo i;
        {std::lock_guard lock(s->mutex);err=s->probeError;i=s->probeInfo;}
        if(!err.empty()){SetText(hwnd,IDC_INFO,L"Probe failed: "+err);}
        else {wchar_t b[256]{};std::wstring codec = i.codecName.empty() ? L"unknown" : ToWide(i.codecName);
swprintf_s(b,L"%ux%u  %.3f fps  %s  Duration %s  Frames ~%llu  Audio %s",i.width,i.height,i.fps,codec.c_str(),TimeText(i.durationSeconds).c_str(),static_cast<unsigned long long>(i.totalFrames),i.hasAudio?L"Yes":L"No");SetText(hwnd,IDC_INFO,b);}
        EnableWindow(GetDlgItem(hwnd,IDC_INPUT_BROWSE),!s->running);
        EnableWindow(GetDlgItem(hwnd,IDC_START),!s->running);
        return 0;
    }
    case WM_APP_PROGRESS: if(s)UpdateProgressUi(s);return 0;
    case WM_APP_PREVIEW: if(s){InvalidateRect(s->previewOriginal,nullptr,FALSE);InvalidateRect(s->previewOutput,nullptr,FALSE);InvalidateRect(s->previewDepth,nullptr,FALSE);InvalidateRect(s->previewMotion,nullptr,FALSE);}return 0;
    case WM_APP_FINISHED: if(s){if(s->worker.joinable())s->worker.join();s->running=false;EnableJobControls(s,false);UpdateProgressUi(s);std::wstring err;{std::lock_guard lock(s->mutex);err=s->error;}if(!err.empty()){SetText(hwnd,IDC_STATUS,L"Failed.");MessageBoxW(hwnd,err.c_str(),L"Video conversion failed",MB_ICONERROR);}else if(!s->cancel.load()){MessageBoxW(hwnd,L"Video conversion completed.",L"Crow-DLSS5-Video-Image-Converter",MB_OK|MB_ICONINFORMATION);} }return 0;
    case WM_CLOSE: if(s&&(s->running||s->probing)){if(MessageBoxW(hwnd,L"A video operation is running. Cancel it and close?",L"Crow-DLSS5-Video-Image-Converter",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;s->cancel.store(true);s->probeCancel.store(true);}DestroyWindow(hwnd);return 0;
    case WM_DESTROY: if(s){s->cancel.store(true);s->probeCancel.store(true);if(s->worker.joinable())s->worker.join();if(s->probeWorker.joinable())s->probeWorker.join();}PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

} // namespace

int RunVideoGuiApp(void* instance, int showCommand) {
    auto inst=static_cast<HINSTANCE>(instance);
    INITCOMMONCONTROLSEX ic{sizeof(ic),ICC_STANDARD_CLASSES|ICC_PROGRESS_CLASS};InitCommonControlsEx(&ic);
    WNDCLASSEXW pc{sizeof(pc)};pc.hInstance=inst;pc.lpfnWndProc=PreviewProc;pc.lpszClassName=PREVIEW_CLASS;pc.hCursor=LoadCursorW(nullptr,IDC_ARROW);pc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassExW(&pc);
    WNDCLASSEXW wc{sizeof(wc)};wc.hInstance=inst;wc.lpfnWndProc=MainProc;wc.lpszClassName=MAIN_CLASS;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);RegisterClassExW(&wc);
    State state;
    HWND h=CreateWindowExW(0,MAIN_CLASS,L"Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1580,1100,nullptr,nullptr,inst,&state);
    if(!h)return 1;ShowWindow(h,showCommand);UpdateWindow(h);
    MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return static_cast<int>(m.wParam);
}
