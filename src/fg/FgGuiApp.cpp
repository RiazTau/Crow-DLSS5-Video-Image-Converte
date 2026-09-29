#include "FgGuiApp.h"
#include "FgVideoConverter.h"
#include "DlssFrameGenerationRunner.h"
#include "AppPaths.h"
#include "D3D12Context.h"
#include "video/NvofFlowSession.h"

#include <Windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace fg {
namespace {

constexpr wchar_t kClassName[] = L"CrowDlssRenderingToolFgWindow";
constexpr wchar_t kTitle[] = L"Crow - DLSS Rendering Tool FG V0.7.0-alpha1";
constexpr UINT WM_FG_PROGRESS = WM_APP + 101;
constexpr UINT WM_FG_DONE = WM_APP + 102;

enum ControlId : int {
    IDC_INPUT = 1001,
    IDC_INPUT_BROWSE,
    IDC_OUTPUT,
    IDC_OUTPUT_BROWSE,
    IDC_CODEC,
    IDC_QUALITY,
    IDC_NVOF_QUALITY,
    IDC_GRID,
    IDC_TEMPORAL_HINTS,
    IDC_OUTPUT_COST,
    IDC_SCENE_CUT,
    IDC_RUNTIME_STATUS,
    IDC_CHECK_RUNTIME,
    IDC_START,
    IDC_CANCEL,
    IDC_PROGRESS,
    IDC_LOG
};

struct ProgressPayload { FgProgress value; };
struct DonePayload { bool ok = false; std::wstring message; };

struct AppState {
    HWND hwnd = nullptr;
    HWND input = nullptr;
    HWND output = nullptr;
    HWND codec = nullptr;
    HWND quality = nullptr;
    HWND nvofQuality = nullptr;
    HWND grid = nullptr;
    HWND temporalHints = nullptr;
    HWND outputCost = nullptr;
    HWND sceneCut = nullptr;
    HWND runtimeStatus = nullptr;
    HWND checkRuntime = nullptr;
    HWND start = nullptr;
    HWND cancel = nullptr;
    HWND progress = nullptr;
    HWND log = nullptr;
    HFONT font = nullptr;
    std::thread worker;
    std::atomic_bool cancelRequested{false};
    bool running = false;
    bool pendingClose = false;
};

std::wstring GetText(HWND control) {
    const int n = GetWindowTextLengthW(control);
    std::wstring s(static_cast<size_t>(n) + 1u, L'\0');
    if (n > 0) GetWindowTextW(control, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

void SetText(HWND control, const std::wstring& value) {
    SetWindowTextW(control, value.c_str());
}

void AppendLog(AppState& s, const std::wstring& line) {
    if (!s.log) return;
    const int len = GetWindowTextLengthW(s.log);
    SendMessageW(s.log, EM_SETSEL, len, len);
    std::wstring text = line;
    if (!text.ends_with(L"\r\n")) text += L"\r\n";
    SendMessageW(s.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    SendMessageW(s.log, EM_SCROLLCARET, 0, 0);
}

HWND AddControl(AppState& s, const wchar_t* cls, const wchar_t* text, DWORD style,
                int x, int y, int w, int h, int id, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, style | WS_CHILD | WS_VISIBLE,
                             x, y, w, h, s.hwnd,
                             id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (c && s.font) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(s.font), TRUE);
    return c;
}

void Label(AppState& s, const wchar_t* text, int x, int y, int w, int h = 20) {
    AddControl(s, L"STATIC", text, SS_LEFT, x, y, w, h, 0);
}

std::filesystem::path BrowseOpenVideo(HWND owner) {
    wchar_t buffer[32768]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(_countof(buffer));
    ofn.lpstrFilter = L"Video files\0*.mp4;*.mkv;*.mov;*.avi;*.webm;*.m4v;*.ts;*.mts\0All files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return {};
    return buffer;
}

std::filesystem::path BrowseSaveVideo(HWND owner, const std::filesystem::path& suggested) {
    wchar_t buffer[32768]{};
    const std::wstring initial = suggested.wstring();
    wcsncpy_s(buffer, _countof(buffer), initial.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = static_cast<DWORD>(_countof(buffer));
    ofn.lpstrFilter = L"MP4 video\0*.mp4\0Matroska video\0*.mkv\0All files\0*.*\0\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"mp4";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return {};
    return buffer;
}

std::filesystem::path DefaultOutput(const std::filesystem::path& input) {
    if (input.empty()) return {};
    const auto dir = input.parent_path();
    return dir / (input.stem().wstring() + L"_FG2X.mp4");
}

void SetRunning(AppState& s, bool running) {
    s.running = running;
    EnableWindow(s.input, !running);
    EnableWindow(GetDlgItem(s.hwnd, IDC_INPUT_BROWSE), !running);
    EnableWindow(s.output, !running);
    EnableWindow(GetDlgItem(s.hwnd, IDC_OUTPUT_BROWSE), !running);
    EnableWindow(s.codec, !running);
    EnableWindow(s.quality, !running);
    EnableWindow(s.nvofQuality, !running);
    EnableWindow(s.grid, !running);
    EnableWindow(s.temporalHints, !running);
    EnableWindow(s.outputCost, !running);
    EnableWindow(s.sceneCut, !running);
    EnableWindow(s.checkRuntime, !running);
    EnableWindow(s.start, !running);
    EnableWindow(s.cancel, running);
    if (!running) s.cancelRequested.store(false, std::memory_order_relaxed);
}

std::wstring StageName(FgProgress::Stage stage) {
    switch (stage) {
    case FgProgress::Stage::Preparing: return L"Preparing";
    case FgProgress::Stage::Processing: return L"Processing";
    case FgProgress::Stage::Finalizing: return L"Finalizing";
    case FgProgress::Stage::Completed: return L"Completed";
    case FgProgress::Stage::Cancelled: return L"Cancelled";
    case FgProgress::Stage::Failed: return L"Failed";
    }
    return L"Unknown";
}

void ApplyProgress(AppState& s, const FgProgress& p) {
    const int pos = static_cast<int>(std::clamp(p.fraction, 0.0, 1.0) * 1000.0);
    SendMessageW(s.progress, PBM_SETPOS, pos, 0);
    std::wostringstream line;
    line << L"[" << StageName(p.stage) << L"] " << p.message;
    if (p.stage == FgProgress::Stage::Processing) {
        line << L"  real=" << p.realFrameIndex;
        if (p.totalRealFrames) line << L"/" << p.totalRealFrames;
        line << L"  out=" << p.outputFrames << L"  speed=";
        line.setf(std::ios::fixed); line.precision(2);
        line << p.processingFps << L" real-fps";
    }
    AppendLog(s, line.str());
}

FgVideoSettings ReadSettings(AppState& s) {
    FgVideoSettings cfg;
    cfg.input = GetText(s.input);
    cfg.output = GetText(s.output);
    const int codec = static_cast<int>(SendMessageW(s.codec, CB_GETCURSEL, 0, 0));
    cfg.codec = codec == 1 ? FgVideoCodec::HevcNvenc : (codec == 2 ? FgVideoCodec::H264Cpu : FgVideoCodec::H264Nvenc);
    try { cfg.quality = std::clamp(std::stoi(GetText(s.quality)), 0, 51); } catch (...) { cfg.quality = 18; }
    try { cfg.sceneCutThreshold = std::clamp(std::stof(GetText(s.sceneCut)), 0.01f, 1.0f); } catch (...) { cfg.sceneCutThreshold = 0.28f; }

    const int q = static_cast<int>(SendMessageW(s.nvofQuality, CB_GETCURSEL, 0, 0));
    cfg.nvof.quality = q == 0 ? video::NvofQuality::Slow : (q == 1 ? video::NvofQuality::Medium : video::NvofQuality::Fast);
    const int g = static_cast<int>(SendMessageW(s.grid, CB_GETCURSEL, 0, 0));
    cfg.nvof.outputGridSize = g == 1 ? 2u : (g == 2 ? 1u : 4u);
    cfg.nvof.temporalHints = SendMessageW(s.temporalHints, BM_GETCHECK, 0, 0) == BST_CHECKED;
    cfg.nvof.outputCost = SendMessageW(s.outputCost, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return cfg;
}

void CheckRuntime(AppState& s) {
    try {
        SetText(s.runtimeStatus, L"Checking D3D12 / NGX DLSS-G...");
        D3D12Context d3d;
        DXGI_ADAPTER_DESC1 adapterDesc{};
        std::wstring adapterName = L"NVIDIA D3D12 adapter";
        if (d3d.Adapter() && SUCCEEDED(d3d.Adapter()->GetDesc1(&adapterDesc))) adapterName = adapterDesc.Description;
        DlssFrameGenerationRunner runner(d3d, app::DistributionRoot() / L"runtime");
        const auto& cap = runner.Capability();
        std::wostringstream text;
        text << (cap.available ? L"READY" : L"UNAVAILABLE")
             << L" | " << adapterName
             << L" | DLSS-G 2X | max generated frames=" << cap.maxGeneratedFrames
             << L" | NVOF=" << video::NvofFlowSession::BuildStatusText();
        SetText(s.runtimeStatus, text.str());
        AppendLog(s, L"Runtime check: " + text.str());
    } catch (const std::exception& e) {
        const std::string n = e.what();
        const std::wstring w(n.begin(), n.end());
        SetText(s.runtimeStatus, L"NOT READY: " + w);
        AppendLog(s, L"Runtime check failed: " + w);
    }
}

void StartConversion(AppState& s) {
    if (s.running) return;
    const auto cfg = ReadSettings(s);
    if (cfg.input.empty() || !std::filesystem::exists(cfg.input)) {
        MessageBoxW(s.hwnd, L"Please select a valid input video.", kTitle, MB_ICONWARNING);
        return;
    }
    if (cfg.output.empty()) {
        MessageBoxW(s.hwnd, L"Please select an output video.", kTitle, MB_ICONWARNING);
        return;
    }
    if (!video::NvofFlowSession::NativeBackendCompiled()) {
        MessageBoxW(s.hwnd,
            L"This build does not contain the native NVOF D3D12 execute bridge.\n\n"
            L"Run BUILD.bat and choose a build option [1]-[6]; required NVIDIA Optical Flow SDK 5.x setup runs before compilation.",
            kTitle, MB_ICONERROR);
        return;
    }

    s.cancelRequested.store(false, std::memory_order_relaxed);
    SendMessageW(s.progress, PBM_SETPOS, 0, 0);
    AppendLog(s, L"Starting standalone 2X FG conversion...");
    AppendLog(s, L"Guidance: NVIDIA Optical Flow motion + zero-depth alpha guidance.");
    SetRunning(s, true);
    HWND hwnd = s.hwnd;
    AppState* state = &s;
    s.worker = std::thread([hwnd, state, cfg]() {
        auto done = std::make_unique<DonePayload>();
        try {
            FgCallbacks cb;
            cb.onProgress = [hwnd](const FgProgress& p) {
                auto payload = std::make_unique<ProgressPayload>();
                payload->value = p;
                if (PostMessageW(hwnd, WM_FG_PROGRESS, 0, reinterpret_cast<LPARAM>(payload.get())))
                    payload.release();
            };
            ConvertFgVideo2X(cfg, cb, state->cancelRequested);
            done->ok = !state->cancelRequested.load(std::memory_order_relaxed);
            done->message = done->ok ? L"2X Frame Generation conversion finished." : L"Conversion cancelled.";
        } catch (const std::exception& e) {
            const std::string n = e.what();
            done->message.assign(n.begin(), n.end());
            done->ok = false;
        } catch (...) {
            done->message = L"Unknown error in FG worker.";
            done->ok = false;
        }
        if (PostMessageW(hwnd, WM_FG_DONE, 0, reinterpret_cast<LPARAM>(done.get()))) done.release();
    });
}

void CreateUi(AppState& s) {
    s.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    int y = 18;
    Label(s, L"Standalone DLSS Frame Generation 2X", 18, y, 360, 22); y += 32;

    Label(s, L"Input video", 18, y + 4, 95);
    s.input = AddControl(s, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 118, y, 570, 25, IDC_INPUT);
    AddControl(s, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 696, y, 92, 25, IDC_INPUT_BROWSE); y += 36;

    Label(s, L"Output video", 18, y + 4, 95);
    s.output = AddControl(s, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 118, y, 570, 25, IDC_OUTPUT);
    AddControl(s, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 696, y, 92, 25, IDC_OUTPUT_BROWSE); y += 42;

    Label(s, L"FG mode", 18, y + 4, 95);
    AddControl(s, L"STATIC", L"DLSS Frame Generation 2X (fixed in alpha1)", SS_LEFT, 118, y + 4, 330, 20, 0);
    Label(s, L"Depth guide", 470, y + 4, 92);
    AddControl(s, L"STATIC", L"Zero Depth (alpha1)", SS_LEFT, 563, y + 4, 220, 20, 0); y += 32;

    Label(s, L"Encoder", 18, y + 4, 95);
    s.codec = AddControl(s, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 118, y, 180, 200, IDC_CODEC);
    SendMessageW(s.codec, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"H.264 NVENC"));
    SendMessageW(s.codec, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"HEVC NVENC"));
    SendMessageW(s.codec, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"H.264 CPU (x264)"));
    SendMessageW(s.codec, CB_SETCURSEL, 0, 0);
    Label(s, L"CQ/CRF", 318, y + 4, 62);
    s.quality = AddControl(s, L"EDIT", L"18", WS_BORDER | ES_NUMBER | WS_TABSTOP, 382, y, 54, 25, IDC_QUALITY); y += 36;

    Label(s, L"NVOF quality", 18, y + 4, 95);
    s.nvofQuality = AddControl(s, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 118, y, 130, 160, IDC_NVOF_QUALITY);
    SendMessageW(s.nvofQuality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Slow / Quality"));
    SendMessageW(s.nvofQuality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Medium"));
    SendMessageW(s.nvofQuality, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fast"));
    SendMessageW(s.nvofQuality, CB_SETCURSEL, 0, 0);
    Label(s, L"Output grid", 268, y + 4, 78);
    s.grid = AddControl(s, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 350, y, 92, 140, IDC_GRID);
    SendMessageW(s.grid, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"4x4"));
    SendMessageW(s.grid, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2x2"));
    SendMessageW(s.grid, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"1x1"));
    SendMessageW(s.grid, CB_SETCURSEL, 0, 0);
    s.temporalHints = AddControl(s, L"BUTTON", L"Temporal hints", BS_AUTOCHECKBOX | WS_TABSTOP, 466, y + 2, 125, 24, IDC_TEMPORAL_HINTS);
    SendMessageW(s.temporalHints, BM_SETCHECK, BST_CHECKED, 0);
    s.outputCost = AddControl(s, L"BUTTON", L"Output cost", BS_AUTOCHECKBOX | WS_TABSTOP, 600, y + 2, 108, 24, IDC_OUTPUT_COST);
    SendMessageW(s.outputCost, BM_SETCHECK, BST_CHECKED, 0); y += 36;

    Label(s, L"Scene-cut threshold", 18, y + 4, 130);
    s.sceneCut = AddControl(s, L"EDIT", L"0.28", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 153, y, 72, 25, IDC_SCENE_CUT); y += 38;

    Label(s, L"Runtime", 18, y + 4, 95);
    s.runtimeStatus = AddControl(s, L"STATIC", L"Not checked", SS_LEFT | SS_PATHELLIPSIS, 118, y + 4, 560, 20, IDC_RUNTIME_STATUS);
    s.checkRuntime = AddControl(s, L"BUTTON", L"Check Runtime", BS_PUSHBUTTON | WS_TABSTOP, 682, y, 106, 26, IDC_CHECK_RUNTIME); y += 38;

    s.start = AddControl(s, L"BUTTON", L"Start 2X FG", BS_DEFPUSHBUTTON | WS_TABSTOP, 18, y, 142, 31, IDC_START);
    s.cancel = AddControl(s, L"BUTTON", L"Cancel", BS_PUSHBUTTON | WS_TABSTOP, 170, y, 100, 31, IDC_CANCEL);
    EnableWindow(s.cancel, FALSE);
    s.progress = AddControl(s, PROGRESS_CLASSW, L"", 0, 286, y + 4, 502, 23, IDC_PROGRESS);
    SendMessageW(s.progress, PBM_SETRANGE32, 0, 1000); y += 43;

    Label(s, L"Log", 18, y, 95); y += 22;
    s.log = AddControl(s, L"EDIT", L"FG alpha1 ready. Run Check Runtime before the first conversion.\r\n",
                       WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                       18, y, 770, 170, IDC_LOG, WS_EX_CLIENTEDGE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* s = reinterpret_cast<AppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        s = static_cast<AppState*>(cs->lpCreateParams);
        s->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    if (!s) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
    case WM_CREATE:
        CreateUi(*s);
        return 0;
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_INPUT_BROWSE: {
            auto p = BrowseOpenVideo(hwnd);
            if (!p.empty()) {
                SetText(s->input, p.wstring());
                if (GetText(s->output).empty()) SetText(s->output, DefaultOutput(p).wstring());
            }
            return 0;
        }
        case IDC_OUTPUT_BROWSE: {
            auto suggested = std::filesystem::path(GetText(s->output));
            if (suggested.empty()) suggested = DefaultOutput(GetText(s->input));
            auto p = BrowseSaveVideo(hwnd, suggested);
            if (!p.empty()) SetText(s->output, p.wstring());
            return 0;
        }
        case IDC_CHECK_RUNTIME:
            CheckRuntime(*s); return 0;
        case IDC_START:
            StartConversion(*s); return 0;
        case IDC_CANCEL:
            if (s->running) {
                s->cancelRequested.store(true, std::memory_order_relaxed);
                EnableWindow(s->cancel, FALSE);
                AppendLog(*s, L"Cancellation requested. Draining GPU/process lifecycle safely...");
            }
            return 0;
        }
        break;
    }
    case WM_FG_PROGRESS: {
        std::unique_ptr<ProgressPayload> p(reinterpret_cast<ProgressPayload*>(lParam));
        if (p) ApplyProgress(*s, p->value);
        return 0;
    }
    case WM_FG_DONE: {
        std::unique_ptr<DonePayload> p(reinterpret_cast<DonePayload*>(lParam));
        if (s->worker.joinable()) s->worker.join();
        SetRunning(*s, false);
        if (p) AppendLog(*s, (p->ok ? L"Done: " : L"Stopped: ") + p->message);
        if (p && !p->ok && !s->pendingClose && !s->cancelRequested.load(std::memory_order_relaxed) && p->message != L"Conversion cancelled.")
            MessageBoxW(hwnd, p->message.c_str(), kTitle, MB_ICONERROR);
        if (s->pendingClose) DestroyWindow(hwnd);
        return 0;
    }
    case WM_CLOSE:
        if (s->running) {
            s->pendingClose = true;
            s->cancelRequested.store(true, std::memory_order_relaxed);
            EnableWindow(s->cancel, FALSE);
            AppendLog(*s, L"Window close requested: cancelling safely before exit...");
            return 0;
        }
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        if (s->worker.joinable()) {
            s->cancelRequested.store(true, std::memory_order_relaxed);
            s->worker.join();
        }
        PostQuitMessage(0); return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int RunFgGui(HINSTANCE instance, int showCommand) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return 1;

    AppState state;
    RECT desired{0, 0, 824, 650};
    AdjustWindowRectEx(&desired, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, FALSE, 0);
    HWND hwnd = CreateWindowExW(0, kClassName, kTitle,
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        desired.right - desired.left, desired.bottom - desired.top,
        nullptr, nullptr, instance, &state);
    if (!hwnd) return 2;
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

} // namespace fg
