#include "ExternalRenderDataDialog.h"
#include "ExternalDataCalibration.h"
#include "FfmpegProcess.h"
#include "ImageImport.h"
#include "VideoConverter.h"
#include "AppPaths.h"
#include <CommCtrl.h>
#include <commdlg.h>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr wchar_t kClassName[] = L"DLSS5ExternalRenderDataV0652";

enum : int {
    IDC_D_PATH = 8100, IDC_D_BROWSE, IDC_D_SEQUENCE, IDC_D_CHANNEL, IDC_D_AUTO_CHANNEL, IDC_D_ANALYZE, IDC_D_CHANNEL_RESET,
    IDC_D_MAPPING, IDC_D_MAPPING_RESET, IDC_D_NEAR, IDC_D_NEAR_RESET, IDC_D_FAR, IDC_D_FAR_RESET,
    IDC_D_INVERTED, IDC_D_INVERTED_RESET, IDC_D_RESULT,
    IDC_M_PATH, IDC_M_BROWSE, IDC_M_SEQUENCE, IDC_M_X, IDC_M_X_AUTO, IDC_M_X_RESET, IDC_M_Y, IDC_M_Y_AUTO, IDC_M_Y_RESET,
    IDC_M_SCALE_X, IDC_M_SCALE_X_RESET, IDC_M_SCALE_Y, IDC_M_SCALE_Y_RESET,
    IDC_M_FLIP_X, IDC_M_FLIP_X_RESET, IDC_M_FLIP_Y, IDC_M_FLIP_Y_RESET,
    IDC_M_DIRECTION, IDC_M_DIRECTION_RESET, IDC_M_ANALYZE, IDC_M_RESULT,
    IDC_ANALYZE_BOTH, IDC_NOTE, IDC_OK_BUTTON, IDC_CANCEL_BUTTON,
    IDC_D_GROUP = 8300, IDC_D_LABEL_PATH, IDC_D_LABEL_SEQUENCE, IDC_D_LABEL_CHANNEL, IDC_D_LABEL_MAPPING,
    IDC_D_LABEL_NEAR, IDC_D_LABEL_FAR,
    IDC_M_GROUP, IDC_M_LABEL_PATH, IDC_M_LABEL_SEQUENCE, IDC_M_LABEL_X, IDC_M_LABEL_Y,
    IDC_M_LABEL_SCALE_X, IDC_M_LABEL_SCALE_Y, IDC_M_LABEL_DIRECTION
};

struct DialogState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    std::filesystem::path sourceVideo;
    video::ExternalRenderDataSettings working;
    bool accepted = false;
    UINT dpi = 96;
    HFONT uiFont = nullptr;
    std::vector<std::string> depthChannels;
    std::vector<std::string> motionChannels;
};

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

HWND Make(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id,
          int x, int y, int w, int h) {
    return CreateWindowExW(0, cls, text, style | WS_CHILD | WS_VISIBLE, x, y, w, h, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), nullptr);
}

void Label(HWND h, const wchar_t* text, int x, int y, int w, int hh = 22) {
    Make(h, L"STATIC", text, SS_LEFT, -1, x, y, w, hh);
}


UINT WindowDpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (auto fn = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow"))) {
            const UINT dpi = fn(hwnd);
            if (dpi) return dpi;
        }
    }
    return 96u;
}

int DpiScale(const DialogState* s, int logicalPx) {
    return MulDiv(logicalPx, static_cast<int>((s && s->dpi) ? s->dpi : 96u), 96);
}

int MeasureTextWidth(HWND hwnd, HFONT font, const wchar_t* text) {
    if (!hwnd || !text || !*text) return 0;
    HDC dc = GetDC(hwnd);
    if (!dc) return 0;
    HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return size.cx;
}

int ButtonWidth(DialogState* s, const wchar_t* text, int logicalMinimum) {
    const int measured = MeasureTextWidth(s->hwnd, s->uiFont, text) + DpiScale(s, 24);
    return std::max(DpiScale(s, logicalMinimum), measured);
}

void FitComboDropWidth(DialogState* s, int id) {
    HWND combo = GetDlgItem(s->hwnd, id);
    if (!combo) return;
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    int widest = 0;
    for (int i = 0; i < count; ++i) {
        const int len = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, i, 0));
        if (len <= 0) continue;
        std::wstring text(static_cast<size_t>(len) + 1u, L'\0');
        SendMessageW(combo, CB_GETLBTEXT, i, reinterpret_cast<LPARAM>(text.data()));
        text.resize(static_cast<size_t>(len));
        widest = std::max(widest, MeasureTextWidth(combo, s->uiFont, text.c_str()));
    }
    RECT rc{}; GetWindowRect(combo, &rc);
    const int currentWidth = static_cast<int>(rc.right - rc.left);
    const int desiredWidth = widest + DpiScale(s, 42);
    const int dropWidth = (std::max)(currentWidth, desiredWidth);
    ::SendMessageW(combo, CB_SETDROPPEDWIDTH, static_cast<WPARAM>(dropWidth), 0);
}

void ApplyDialogFont(DialogState* s) {
    if (!s || !s->hwnd) return;
    if (s->uiFont) { DeleteObject(s->uiFont); s->uiFont = nullptr; }
    const int height = -MulDiv(9, static_cast<int>(s->dpi ? s->dpi : 96u), 72);
    s->uiFont = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT font = s->uiFont ? s->uiFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (HWND c = GetWindow(s->hwnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
        SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (int id : {IDC_D_CHANNEL, IDC_D_MAPPING, IDC_M_X, IDC_M_Y, IDC_M_DIRECTION}) FitComboDropWidth(s, id);
}

void MoveCtl(DialogState* s, int id, int x, int y, int w, int h) {
    if (HWND ctl = GetDlgItem(s->hwnd, id))
        MoveWindow(ctl, x, y, std::max(1, w), std::max(1, h), TRUE);
}

void LayoutDialog(DialogState* s) {
    if (!s || !s->hwnd) return;
    RECT rc{}; GetClientRect(s->hwnd, &rc);
    const int W = std::max(1, static_cast<int>(rc.right - rc.left));
    const int H = std::max(1, static_cast<int>(rc.bottom - rc.top));
    const int m = DpiScale(s, 12), inner = DpiScale(s, 14), gap = DpiScale(s, 8);
    const int row = DpiScale(s, 26), labelW = DpiScale(s, 104);
    const int groupW = std::max(DpiScale(s, 760), W - 2 * m);
    const int left = m + inner, right = m + groupW - inner;
    const int valueX = left + labelW;

    const int depthY = DpiScale(s, 10), depthH = DpiScale(s, 228);
    MoveCtl(s, IDC_D_GROUP, m, depthY, groupW, depthH);
    int y = depthY + DpiScale(s, 26);
    const int browseW = ButtonWidth(s, L"Browse...", 100);
    MoveCtl(s, IDC_D_LABEL_PATH, left, y + DpiScale(s, 3), labelW - gap, row);
    MoveCtl(s, IDC_D_BROWSE, right - browseW, y, browseW, row);
    MoveCtl(s, IDC_D_PATH, valueX, y, std::max(DpiScale(s, 160), right - browseW - gap - valueX), row);
    y += DpiScale(s, 32);
    MoveCtl(s, IDC_D_LABEL_SEQUENCE, left, y + DpiScale(s, 3), labelW - gap, row);
    MoveCtl(s, IDC_D_SEQUENCE, valueX, y + DpiScale(s, 2), std::max(DpiScale(s, 200), right - valueX), row);
    y += DpiScale(s, 30);

    const int resetW = ButtonWidth(s, L"Reset", 64);
    const int analyzeW = ButtonWidth(s, L"Auto Calibrate", 112);
    const int quickW = ButtonWidth(s, L"Quick Auto", 88);
    int bx = right;
    bx -= resetW; MoveCtl(s, IDC_D_CHANNEL_RESET, bx, y, resetW, row); bx -= gap;
    bx -= analyzeW; MoveCtl(s, IDC_D_ANALYZE, bx, y, analyzeW, row); bx -= gap;
    bx -= quickW; MoveCtl(s, IDC_D_AUTO_CHANNEL, bx, y, quickW, row); bx -= gap;
    MoveCtl(s, IDC_D_LABEL_CHANNEL, left, y + DpiScale(s, 3), labelW - gap, row);
    MoveCtl(s, IDC_D_CHANNEL, valueX, y, std::max(DpiScale(s, 180), bx - valueX), DpiScale(s, 210));
    y += DpiScale(s, 32);

    MoveCtl(s, IDC_D_LABEL_MAPPING, left, y + DpiScale(s, 3), DpiScale(s, 72), row);
    const int mappingX = left + DpiScale(s, 76);
    const int mappingW = DpiScale(s, 190);
    MoveCtl(s, IDC_D_MAPPING, mappingX, y, mappingW, DpiScale(s, 120));
    MoveCtl(s, IDC_D_MAPPING_RESET, mappingX + mappingW + gap, y, resetW, row);
    int nx = mappingX + mappingW + gap + resetW + DpiScale(s, 20);
    MoveCtl(s, IDC_D_LABEL_NEAR, nx, y + DpiScale(s, 3), DpiScale(s, 42), row); nx += DpiScale(s, 44);
    const int numberW = DpiScale(s, 88);
    MoveCtl(s, IDC_D_NEAR, nx, y, numberW, row); nx += numberW + gap;
    MoveCtl(s, IDC_D_NEAR_RESET, nx, y, resetW, row); nx += resetW + DpiScale(s, 18);
    MoveCtl(s, IDC_D_LABEL_FAR, nx, y + DpiScale(s, 3), DpiScale(s, 34), row); nx += DpiScale(s, 36);
    MoveCtl(s, IDC_D_FAR, nx, y, std::max(DpiScale(s, 72), right - nx - resetW - gap), row);
    MoveCtl(s, IDC_D_FAR_RESET, right - resetW, y, resetW, row);
    y += DpiScale(s, 32);

    const int invertedW = std::min(right - valueX - resetW - gap,
        MeasureTextWidth(s->hwnd, s->uiFont, L"Near = white / DLSSNR DepthInverted") + DpiScale(s, 30));
    MoveCtl(s, IDC_D_INVERTED, valueX, y, std::max(DpiScale(s, 260), invertedW), row);
    MoveCtl(s, IDC_D_INVERTED_RESET, valueX + std::max(DpiScale(s, 260), invertedW) + gap, y, resetW, row);
    y += DpiScale(s, 31);
    MoveCtl(s, IDC_D_RESULT, left, y, right - left, DpiScale(s, 38));

    const int motionY = depthY + depthH + DpiScale(s, 10), motionH = DpiScale(s, 336);
    MoveCtl(s, IDC_M_GROUP, m, motionY, groupW, motionH);
    y = motionY + DpiScale(s, 26);
    MoveCtl(s, IDC_M_LABEL_PATH, left, y + DpiScale(s, 3), labelW - gap, row);
    MoveCtl(s, IDC_M_BROWSE, right - browseW, y, browseW, row);
    MoveCtl(s, IDC_M_PATH, valueX, y, std::max(DpiScale(s, 160), right - browseW - gap - valueX), row);
    y += DpiScale(s, 32);
    MoveCtl(s, IDC_M_LABEL_SEQUENCE, left, y + DpiScale(s, 3), labelW - gap, row);
    MoveCtl(s, IDC_M_SEQUENCE, valueX, y + DpiScale(s, 2), std::max(DpiScale(s, 200), right - valueX), row);
    y += DpiScale(s, 30);

    const int motionAnalyzeW = ButtonWidth(s, L"Auto Calibrate", 112);
    const int motionResetW = resetW, motionQuickW = quickW;
    const int motionRight = right - motionAnalyzeW - gap;
    auto motionRow = [&](int labelId, int comboId, int quickId, int resetId, int yy) {
        MoveCtl(s, labelId, left, yy + DpiScale(s, 3), labelW - gap, row);
        int r = motionRight;
        r -= motionResetW; MoveCtl(s, resetId, r, yy, motionResetW, row); r -= gap;
        r -= motionQuickW; MoveCtl(s, quickId, r, yy, motionQuickW, row); r -= gap;
        MoveCtl(s, comboId, valueX, yy, std::max(DpiScale(s, 210), r - valueX), DpiScale(s, 210));
    };
    motionRow(IDC_M_LABEL_X, IDC_M_X, IDC_M_X_AUTO, IDC_M_X_RESET, y);
    MoveCtl(s, IDC_M_ANALYZE, right - motionAnalyzeW, y, motionAnalyzeW, row * 2 + DpiScale(s, 6));
    y += DpiScale(s, 32);
    motionRow(IDC_M_LABEL_Y, IDC_M_Y, IDC_M_Y_AUTO, IDC_M_Y_RESET, y);
    y += DpiScale(s, 34);

    const int half = (right - left - gap) / 2;
    const int editW = DpiScale(s, 92), smallLabel = DpiScale(s, 66), smallReset = ButtonWidth(s, L"Reset", 58);
    int x1 = left;
    MoveCtl(s, IDC_M_LABEL_SCALE_X, x1, y + DpiScale(s, 3), smallLabel, row); x1 += smallLabel;
    MoveCtl(s, IDC_M_SCALE_X, x1, y, editW, row); x1 += editW + gap;
    MoveCtl(s, IDC_M_SCALE_X_RESET, x1, y, smallReset, row);
    int x2 = left + half + gap;
    MoveCtl(s, IDC_M_LABEL_SCALE_Y, x2, y + DpiScale(s, 3), smallLabel, row); x2 += smallLabel;
    MoveCtl(s, IDC_M_SCALE_Y, x2, y, editW, row); x2 += editW + gap;
    MoveCtl(s, IDC_M_SCALE_Y_RESET, x2, y, smallReset, row);
    y += DpiScale(s, 34);

    const int flipYW = ButtonWidth(s, L"Flip Y", 90), flipXW = ButtonWidth(s, L"Flip X", 90);
    const int flipBlockW = flipXW + gap + smallReset;
    MoveCtl(s, IDC_M_FLIP_X, left, y, flipXW, row);
    MoveCtl(s, IDC_M_FLIP_X_RESET, left + flipXW + gap, y, smallReset, row);
    const int flipYx = left + std::max(DpiScale(s, 240), flipBlockW + DpiScale(s, 40));
    MoveCtl(s, IDC_M_FLIP_Y, flipYx, y, flipYW, row);
    MoveCtl(s, IDC_M_FLIP_Y_RESET, flipYx + flipYW + gap, y, smallReset, row);
    y += DpiScale(s, 34);

    MoveCtl(s, IDC_M_LABEL_DIRECTION, left, y + DpiScale(s, 3), labelW - gap, row);
    MoveCtl(s, IDC_M_DIRECTION_RESET, right - resetW, y, resetW, row);
    MoveCtl(s, IDC_M_DIRECTION, valueX, y, std::max(DpiScale(s, 280), right - resetW - gap - valueX), DpiScale(s, 120));
    y += DpiScale(s, 34);
    MoveCtl(s, IDC_M_RESULT, left, y, right - left, DpiScale(s, 50));

    const int footerTop = motionY + motionH + DpiScale(s, 10);
    const int buttonH = DpiScale(s, 32);
    const int bothW = ButtonWidth(s, L"Auto Calibrate Both", 160);
    MoveCtl(s, IDC_ANALYZE_BOTH, m + DpiScale(s, 6), footerTop, bothW, buttonH);
    const int okW = ButtonWidth(s, L"OK", 88), cancelW = ButtonWidth(s, L"Cancel", 92);
    const int footerRight = W - m - DpiScale(s, 6);
    MoveCtl(s, IDC_CANCEL_BUTTON, footerRight - cancelW, footerTop + DpiScale(s, 62), cancelW, buttonH);
    MoveCtl(s, IDC_OK_BUTTON, footerRight - cancelW - gap - okW, footerTop + DpiScale(s, 62), okW, buttonH);
    const int noteX = m + DpiScale(s, 6) + bothW + gap;
    MoveCtl(s, IDC_NOTE, noteX, footerTop, std::max(DpiScale(s, 300), footerRight - noteX), DpiScale(s, 58));

    for (int id : {IDC_D_CHANNEL, IDC_D_MAPPING, IDC_M_X, IDC_M_Y, IDC_M_DIRECTION}) FitComboDropWidth(s, id);
    (void)H;
}

std::wstring Text(HWND h, int id) {
    HWND c = GetDlgItem(h, id);
    const int n = GetWindowTextLengthW(c);
    std::wstring out(static_cast<size_t>(n) + 1u, L'\0');
    GetWindowTextW(c, out.data(), n + 1); out.resize(static_cast<size_t>(n));
    return out;
}

void SetText(HWND h, int id, const std::wstring& s) { SetWindowTextW(GetDlgItem(h, id), s.c_str()); }
float ReadFloat(HWND h, int id, float fallback) { try { return std::stof(Text(h, id)); } catch (...) { return fallback; } }

void FillCombo(HWND h, int id, const std::vector<std::string>& values, const std::string& selected) {
    HWND c = GetDlgItem(h, id); SendMessageW(c, CB_RESETCONTENT, 0, 0);
    int sel = -1;
    for (size_t i = 0; i < values.size(); ++i) {
        const auto w = ToWide(values[i]);
        SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
        if (values[i] == selected) sel = static_cast<int>(i);
    }
    if (sel < 0 && !values.empty()) sel = 0;
    SendMessageW(c, CB_SETCURSEL, sel, 0);
    if (auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(h, GWLP_USERDATA)))
        FitComboDropWidth(state, id);
}

std::string ComboValue(HWND h, int id) {
    HWND c = GetDlgItem(h, id);
    const int sel = static_cast<int>(SendMessageW(c, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return {};
    const int n = static_cast<int>(SendMessageW(c, CB_GETLBTEXTLEN, sel, 0));
    if (n < 0) return {};
    std::wstring w(static_cast<size_t>(n) + 1u, L'\0');
    SendMessageW(c, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(w.data()));
    w.resize(static_cast<size_t>(n));
    return ToUtf8(w);
}

void SelectComboString(HWND h, int id, const std::string& value) {
    if (value.empty()) return;
    const auto w = ToWide(value);
    const auto idx = SendMessageW(GetDlgItem(h, id), CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(w.c_str()));
    if (idx != CB_ERR) SendMessageW(GetDlgItem(h, id), CB_SETCURSEL, idx, 0);
}

std::optional<std::filesystem::path> PickExr(HWND owner) {
    wchar_t file[32768]{};
    OPENFILENAMEW o{}; o.lStructSize = sizeof(o); o.hwndOwner = owner; o.lpstrFile = file; o.nMaxFile = ARRAYSIZE(file);
    o.lpstrFilter = L"OpenEXR frame (*.exr)\0*.exr\0All files\0*.*\0";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&o) ? std::optional<std::filesystem::path>{file} : std::nullopt;
}

void SetStatus(HWND h, const std::wstring& text) { SetText(h, IDC_NOTE, text); }

std::wstring ConfidenceText(float confidence) {
    const int pct = static_cast<int>(std::lround(std::clamp(confidence, 0.0f, 1.0f) * 100.0f));
    std::wstring out;
    if (pct >= 80) out = L"HIGH ";
    else if (pct >= 50) out = L"MEDIUM ";
    else out = L"LOW ";
    out += std::to_wstring(pct);
    out += L"%";
    if (pct < 50) out += L" - verify manually";
    return out;
}

void LoadDepthProbe(DialogState* s, bool autoSelect) {
    if (s->working.depth.firstFrame.empty()) return;
    try {
        auto probe = video::ProbeExternalExr(s->working.depth.firstFrame);
        s->depthChannels = std::move(probe.channels);
        std::string selected = s->working.depth.channel;
        if (autoSelect || selected.empty()) selected = video::AutoSelectDepthChannel(s->depthChannels);
        FillCombo(s->hwnd, IDC_D_CHANNEL, s->depthChannels, selected);
        SetText(s->hwnd, IDC_D_SEQUENCE, probe.sequenceDescription);
        SetStatus(s->hwnd, L"Depth EXR inspected. Auto Calibrate can now detect channel + a global robust range.");
    } catch (const std::exception& e) {
        MessageBoxA(s->hwnd, e.what(), "External depth EXR", MB_ICONERROR);
    }
}

void LoadMotionProbe(DialogState* s, bool autoSelect) {
    if (s->working.motion.firstFrame.empty()) return;
    try {
        auto probe = video::ProbeExternalExr(s->working.motion.firstFrame);
        s->motionChannels = std::move(probe.channels);
        std::string x = s->working.motion.xChannel, y = s->working.motion.yChannel;
        if (autoSelect || x.empty()) x = video::AutoSelectMotionXChannel(s->motionChannels);
        if (autoSelect || y.empty()) y = video::AutoSelectMotionYChannel(s->motionChannels);
        FillCombo(s->hwnd, IDC_M_X, s->motionChannels, x);
        FillCombo(s->hwnd, IDC_M_Y, s->motionChannels, y);
        SetText(s->hwnd, IDC_M_SEQUENCE, probe.sequenceDescription);
        SetStatus(s->hwnd, L"Motion EXR inspected. Auto Calibrate uses the source video to solve channel/sign/scale/direction.");
    } catch (const std::exception& e) {
        MessageBoxA(s->hwnd, e.what(), "External motion EXR", MB_ICONERROR);
    }
}

void SyncDepthMapping(DialogState* s) {
    const bool fixed = SendMessageW(GetDlgItem(s->hwnd, IDC_D_MAPPING), CB_GETCURSEL, 0, 0) != 1;
    EnableWindow(GetDlgItem(s->hwnd, IDC_D_NEAR), fixed);
    EnableWindow(GetDlgItem(s->hwnd, IDC_D_NEAR_RESET), fixed);
    EnableWindow(GetDlgItem(s->hwnd, IDC_D_FAR), fixed);
    EnableWindow(GetDlgItem(s->hwnd, IDC_D_FAR_RESET), fixed);
}

float PairMad(const Rgba8Image& a, const Rgba8Image& b) {
    if (a.width != b.width || a.height != b.height || a.pixels.size() != b.pixels.size() || a.pixels.empty()) return 1.0f;
    double sum = 0.0;
    const size_t pixels = static_cast<size_t>(a.width) * a.height;
    for (size_t i = 0; i < pixels; i += 3u) {
        const auto* pa = &a.pixels[i * 4u];
        const auto* pb = &b.pixels[i * 4u];
        const float la = 0.2126f * pa[0] + 0.7152f * pa[1] + 0.0722f * pa[2];
        const float lb = 0.2126f * pb[0] + 0.7152f * pb[1] + 0.0722f * pb[2];
        sum += std::abs(la - lb) / 255.0;
    }
    return static_cast<float>(sum / static_cast<double>((pixels + 2u) / 3u));
}

std::vector<video::MotionCalibrationFramePair> DecodeCalibrationPairs(const std::filesystem::path& sourceVideo,
                                                                      uint32_t& sourceWidth,
                                                                      uint32_t& sourceHeight) {
    if (sourceVideo.empty() || !std::filesystem::exists(sourceVideo)) {
        throw std::runtime_error("Select the source video in the main window before motion auto-calibration.");
    }
    const auto ffmpeg = video::FindFfmpeg();
    if (ffmpeg.empty() || !std::filesystem::exists(ffmpeg)) {
        throw std::runtime_error("ffmpeg.exe was not found. Run Setup Video Dependencies first.");
    }
    const auto info = video::ProbeVideo(sourceVideo);
    sourceWidth = info.width; sourceHeight = info.height;
    if (!sourceWidth || !sourceHeight) throw std::runtime_error("Could not determine source-video resolution.");

    const uint32_t aw = std::min<uint32_t>(320u, sourceWidth);
    const uint32_t ah = std::max<uint32_t>(1u, static_cast<uint32_t>(std::lround(
        static_cast<double>(sourceHeight) * aw / sourceWidth)));
    const uint64_t maxFrames = info.totalFrames ? std::min<uint64_t>(info.totalFrames, 360u) : 360u;
    std::wstring scaleFilter = L"scale=";
    scaleFilter += std::to_wstring(aw);
    scaleFilter += L":";
    scaleFilter += std::to_wstring(ah);
    scaleFilter += L":flags=bilinear";
    std::vector<std::wstring> args = {
        L"-v", L"error", L"-nostdin", L"-i", sourceVideo.wstring(),
        L"-map", L"0:v:0", L"-an", L"-sn", L"-dn",
        L"-vf", scaleFilter,
        L"-frames:v", std::to_wstring(maxFrames), L"-f", L"rawvideo", L"-pix_fmt", L"rgba", L"pipe:1"
    };
    const auto logPath = app::ExecutableDir() / L"video" / L"logs" / L"external-calibration-decoder-last.log";
    video::PipeReaderProcess decoder(ffmpeg, args, logPath);

    struct Ranked { float mad = 0.0f; video::MotionCalibrationFramePair pair; };
    std::vector<Ranked> ranked;
    Rgba8Image previous, current;
    previous.width = current.width = aw; previous.height = current.height = ah;
    previous.pixels.resize(static_cast<size_t>(aw) * ah * 4u);
    current.pixels.resize(previous.pixels.size());
    bool havePrevious = false;
    uint64_t oneBased = 0;
    while (oneBased < maxFrames && decoder.ReadExact(current.pixels.data(), current.pixels.size(), nullptr, 30000)) {
        ++oneBased;
        if (havePrevious) {
            const float mad = PairMad(previous, current);
            // Very large differences are likely cuts; near-zero pairs have too little
            // evidence to distinguish a motion convention. Keep the best six useful pairs.
            if (mad >= 0.003f && mad <= 0.55f) {
                Ranked r; r.mad = mad; r.pair.currentOneBasedFrame = oneBased;
                r.pair.previous = previous; r.pair.current = current;
                ranked.push_back(std::move(r));
                std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) { return a.mad > b.mad; });
                if (ranked.size() > 6u) ranked.resize(6u);
            }
        }
        previous.pixels.swap(current.pixels);
        havePrevious = true;
    }
    decoder.Terminate();

    if (ranked.empty() && havePrevious) {
        throw std::runtime_error("The sampled source-video region contains too little usable adjacent-frame motion for automatic calibration.");
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return a.pair.currentOneBasedFrame < b.pair.currentOneBasedFrame;
    });
    std::vector<video::MotionCalibrationFramePair> out;
    out.reserve(ranked.size());
    for (auto& r : ranked) out.push_back(std::move(r.pair));
    return out;
}

void ApplyDepthCalibration(DialogState* s) {
    if (s->working.depth.firstFrame.empty()) {
        MessageBoxW(s->hwnd, L"Select a depth EXR sequence first.", L"Depth Auto Calibration", MB_ICONWARNING);
        return;
    }
    SetStatus(s->hwnd, L"Analyzing depth channels and global sequence statistics...");
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    try {
        const auto r = video::CalibrateExternalDepthSequence(s->working.depth.firstFrame);
        if (!r.success) throw std::runtime_error(r.summary.empty() ? "Depth auto-calibration failed" : r.summary);
        s->working.depth.channel = r.channel;
        s->working.depth.mapping = r.mapping;
        s->working.depth.nearValue = r.nearValue;
        s->working.depth.farValue = r.farValue;
        s->working.depth.depthInverted = r.depthInverted;
        SelectComboString(s->hwnd, IDC_D_CHANNEL, r.channel);
        SendMessageW(GetDlgItem(s->hwnd, IDC_D_MAPPING), CB_SETCURSEL,
                     r.mapping == video::ExternalDepthMapping::Raw01 ? 1 : 0, 0);
        SetText(s->hwnd, IDC_D_NEAR, std::to_wstring(r.nearValue));
        SetText(s->hwnd, IDC_D_FAR, std::to_wstring(r.farValue));
        Button_SetCheck(GetDlgItem(s->hwnd, IDC_D_INVERTED), r.depthInverted ? BST_CHECKED : BST_UNCHECKED);
        SyncDepthMapping(s);
        SetText(s->hwnd, IDC_D_RESULT, std::wstring(L"AUTO ") + ConfidenceText(r.confidence) + L" | " + ToWide(r.summary));
        SetStatus(s->hwnd, L"Depth auto-calibration applied. Range is global across sampled EXR frames, not per-frame normalization.");
    } catch (const std::exception& e) {
        MessageBoxA(s->hwnd, e.what(), "Depth Auto Calibration", MB_ICONERROR);
        SetText(s->hwnd, IDC_D_RESULT, L"AUTO FAILED");
    }
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

void ApplyMotionCalibration(DialogState* s) {
    if (s->working.motion.firstFrame.empty()) {
        MessageBoxW(s->hwnd, L"Select a motion EXR sequence first.", L"Motion Auto Calibration", MB_ICONWARNING);
        return;
    }
    SetStatus(s->hwnd, L"Decoding low-resolution adjacent frames and testing EXR motion conventions...");
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    try {
        uint32_t sourceW = 0, sourceH = 0;
        const auto pairs = DecodeCalibrationPairs(s->sourceVideo, sourceW, sourceH);
        const auto r = video::CalibrateExternalMotionSequence(s->working.motion.firstFrame, sourceW, sourceH, pairs);
        if (!r.success) throw std::runtime_error(r.summary.empty() ? "Motion auto-calibration failed" : r.summary);
        s->working.motion.xChannel = r.xChannel;
        s->working.motion.yChannel = r.yChannel;
        s->working.motion.importScaleX = r.importScaleX;
        s->working.motion.importScaleY = r.importScaleY;
        s->working.motion.flipX = r.flipX;
        s->working.motion.flipY = r.flipY;
        s->working.motion.direction = r.direction;
        SelectComboString(s->hwnd, IDC_M_X, r.xChannel);
        SelectComboString(s->hwnd, IDC_M_Y, r.yChannel);
        SetText(s->hwnd, IDC_M_SCALE_X, std::to_wstring(r.importScaleX));
        SetText(s->hwnd, IDC_M_SCALE_Y, std::to_wstring(r.importScaleY));
        Button_SetCheck(GetDlgItem(s->hwnd, IDC_M_FLIP_X), r.flipX ? BST_CHECKED : BST_UNCHECKED);
        Button_SetCheck(GetDlgItem(s->hwnd, IDC_M_FLIP_Y), r.flipY ? BST_CHECKED : BST_UNCHECKED);
        SendMessageW(GetDlgItem(s->hwnd, IDC_M_DIRECTION), CB_SETCURSEL,
                     r.direction == video::ExternalMotionDirection::PreviousToCurrent ? 1 : 0, 0);
        SetText(s->hwnd, IDC_M_RESULT, std::wstring(L"AUTO ") + ConfidenceText(r.confidence) + L" | " + ToWide(r.summary));
        SetStatus(s->hwnd, r.confidence < 0.50f
            ? L"Motion auto-calibration applied with LOW confidence. Verify using a short conversion before a long render."
            : L"Motion auto-calibration applied from real video reprojection error.");
    } catch (const std::exception& e) {
        MessageBoxA(s->hwnd, e.what(), "Motion Auto Calibration", MB_ICONERROR);
        SetText(s->hwnd, IDC_M_RESULT, L"AUTO FAILED");
    }
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

void CreateControls(DialogState* s) {
    HWND h = s->hwnd;
    Make(h, L"BUTTON", L"External Depth EXR Sequence", BS_GROUPBOX, IDC_D_GROUP, 0, 0, 10, 10);
    Make(h, L"STATIC", L"First frame", SS_LEFT, IDC_D_LABEL_PATH, 0, 0, 10, 10);
    Make(h, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, IDC_D_PATH, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Browse...", BS_PUSHBUTTON, IDC_D_BROWSE, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Sequence", SS_LEFT, IDC_D_LABEL_SEQUENCE, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Not selected", SS_LEFT | SS_PATHELLIPSIS, IDC_D_SEQUENCE, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Channel", SS_LEFT, IDC_D_LABEL_CHANNEL, 0, 0, 10, 10);
    Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_D_CHANNEL, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Quick Auto", BS_PUSHBUTTON, IDC_D_AUTO_CHANNEL, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Auto Calibrate", BS_PUSHBUTTON, IDC_D_ANALYZE, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_CHANNEL_RESET, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Mapping", SS_LEFT, IDC_D_LABEL_MAPPING, 0, 0, 10, 10);
    Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_D_MAPPING, 0, 0, 10, 10);
    SendMessageW(GetDlgItem(h, IDC_D_MAPPING), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fixed Near/Far"));
    SendMessageW(GetDlgItem(h, IDC_D_MAPPING), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Raw 0..1"));
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_MAPPING_RESET, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Near", SS_LEFT, IDC_D_LABEL_NEAR, 0, 0, 10, 10);
    Make(h, L"EDIT", L"0.1", WS_BORDER | ES_AUTOHSCROLL, IDC_D_NEAR, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_NEAR_RESET, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Far", SS_LEFT, IDC_D_LABEL_FAR, 0, 0, 10, 10);
    Make(h, L"EDIT", L"100", WS_BORDER | ES_AUTOHSCROLL, IDC_D_FAR, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_FAR_RESET, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Near = white / DLSSNR DepthInverted", BS_AUTOCHECKBOX, IDC_D_INVERTED, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_INVERTED_RESET, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Not calibrated", SS_LEFT | SS_EDITCONTROL, IDC_D_RESULT, 0, 0, 10, 10);

    Make(h, L"BUTTON", L"External Motion EXR Sequence", BS_GROUPBOX, IDC_M_GROUP, 0, 0, 10, 10);
    Make(h, L"STATIC", L"First frame", SS_LEFT, IDC_M_LABEL_PATH, 0, 0, 10, 10);
    Make(h, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, IDC_M_PATH, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Browse...", BS_PUSHBUTTON, IDC_M_BROWSE, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Sequence", SS_LEFT, IDC_M_LABEL_SEQUENCE, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Not selected", SS_LEFT | SS_PATHELLIPSIS, IDC_M_SEQUENCE, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Motion X", SS_LEFT, IDC_M_LABEL_X, 0, 0, 10, 10);
    Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_M_X, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Quick Auto", BS_PUSHBUTTON, IDC_M_X_AUTO, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_X_RESET, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Auto Calibrate", BS_PUSHBUTTON, IDC_M_ANALYZE, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Motion Y", SS_LEFT, IDC_M_LABEL_Y, 0, 0, 10, 10);
    Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_M_Y, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Quick Auto", BS_PUSHBUTTON, IDC_M_Y_AUTO, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_Y_RESET, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Scale X", SS_LEFT, IDC_M_LABEL_SCALE_X, 0, 0, 10, 10);
    Make(h, L"EDIT", L"1.0", WS_BORDER | ES_AUTOHSCROLL, IDC_M_SCALE_X, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_SCALE_X_RESET, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Scale Y", SS_LEFT, IDC_M_LABEL_SCALE_Y, 0, 0, 10, 10);
    Make(h, L"EDIT", L"1.0", WS_BORDER | ES_AUTOHSCROLL, IDC_M_SCALE_Y, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_SCALE_Y_RESET, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Flip X", BS_AUTOCHECKBOX, IDC_M_FLIP_X, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_FLIP_X_RESET, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Flip Y", BS_AUTOCHECKBOX, IDC_M_FLIP_Y, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_FLIP_Y_RESET, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Direction", SS_LEFT, IDC_M_LABEL_DIRECTION, 0, 0, 10, 10);
    Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_M_DIRECTION, 0, 0, 10, 10);
    SendMessageW(GetDlgItem(h, IDC_M_DIRECTION), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current -> Previous (native)"));
    SendMessageW(GetDlgItem(h, IDC_M_DIRECTION), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Previous -> Current (invert)"));
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_DIRECTION_RESET, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Not calibrated", SS_LEFT | SS_EDITCONTROL, IDC_M_RESULT, 0, 0, 10, 10);

    Make(h, L"BUTTON", L"Auto Calibrate Both", BS_PUSHBUTTON, IDC_ANALYZE_BOTH, 0, 0, 10, 10);
    Make(h, L"STATIC", L"Auto Calibration: Depth uses multi-frame global robust statistics. Motion tests channel pair, XY/sign, pixel/UV/NDC scale and current/previous direction against real adjacent video frames. LOW confidence results remain editable.", SS_LEFT | SS_EDITCONTROL, IDC_NOTE, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"OK", BS_DEFPUSHBUTTON, IDC_OK_BUTTON, 0, 0, 10, 10);
    Make(h, L"BUTTON", L"Cancel", BS_PUSHBUTTON, IDC_CANCEL_BUTTON, 0, 0, 10, 10);

    s->dpi = WindowDpi(h);
    ApplyDialogFont(s);
    LayoutDialog(s);

    SetText(h, IDC_D_PATH, s->working.depth.firstFrame.wstring());
    SetText(h, IDC_M_PATH, s->working.motion.firstFrame.wstring());
    SendMessageW(GetDlgItem(h, IDC_D_MAPPING), CB_SETCURSEL,
                 s->working.depth.mapping == video::ExternalDepthMapping::Raw01 ? 1 : 0, 0);
    SetText(h, IDC_D_NEAR, std::to_wstring(s->working.depth.nearValue));
    SetText(h, IDC_D_FAR, std::to_wstring(s->working.depth.farValue));
    Button_SetCheck(GetDlgItem(h, IDC_D_INVERTED), s->working.depth.depthInverted ? BST_CHECKED : BST_UNCHECKED);
    SetText(h, IDC_M_SCALE_X, std::to_wstring(s->working.motion.importScaleX));
    SetText(h, IDC_M_SCALE_Y, std::to_wstring(s->working.motion.importScaleY));
    Button_SetCheck(GetDlgItem(h, IDC_M_FLIP_X), s->working.motion.flipX ? BST_CHECKED : BST_UNCHECKED);
    Button_SetCheck(GetDlgItem(h, IDC_M_FLIP_Y), s->working.motion.flipY ? BST_CHECKED : BST_UNCHECKED);
    SendMessageW(GetDlgItem(h, IDC_M_DIRECTION), CB_SETCURSEL,
                 s->working.motion.direction == video::ExternalMotionDirection::PreviousToCurrent ? 1 : 0, 0);
    if (!s->working.depth.firstFrame.empty() && std::filesystem::exists(s->working.depth.firstFrame)) LoadDepthProbe(s, false);
    if (!s->working.motion.firstFrame.empty() && std::filesystem::exists(s->working.motion.firstFrame)) LoadMotionProbe(s, false);
    SyncDepthMapping(s);
    LayoutDialog(s);
}

bool Commit(DialogState* s) {
    s->working.depth.channel = ComboValue(s->hwnd, IDC_D_CHANNEL);
    s->working.depth.mapping = SendMessageW(GetDlgItem(s->hwnd, IDC_D_MAPPING), CB_GETCURSEL, 0, 0) == 1
        ? video::ExternalDepthMapping::Raw01 : video::ExternalDepthMapping::FixedRange;
    s->working.depth.nearValue = ReadFloat(s->hwnd, IDC_D_NEAR, 0.1f);
    s->working.depth.farValue = ReadFloat(s->hwnd, IDC_D_FAR, 100.0f);
    s->working.depth.depthInverted = Button_GetCheck(GetDlgItem(s->hwnd, IDC_D_INVERTED)) == BST_CHECKED;
    s->working.motion.xChannel = ComboValue(s->hwnd, IDC_M_X);
    s->working.motion.yChannel = ComboValue(s->hwnd, IDC_M_Y);
    s->working.motion.importScaleX = std::clamp(ReadFloat(s->hwnd, IDC_M_SCALE_X, 1.0f), -1000000.0f, 1000000.0f);
    s->working.motion.importScaleY = std::clamp(ReadFloat(s->hwnd, IDC_M_SCALE_Y, 1.0f), -1000000.0f, 1000000.0f);
    s->working.motion.flipX = Button_GetCheck(GetDlgItem(s->hwnd, IDC_M_FLIP_X)) == BST_CHECKED;
    s->working.motion.flipY = Button_GetCheck(GetDlgItem(s->hwnd, IDC_M_FLIP_Y)) == BST_CHECKED;
    s->working.motion.direction = SendMessageW(GetDlgItem(s->hwnd, IDC_M_DIRECTION), CB_GETCURSEL, 0, 0) == 1
        ? video::ExternalMotionDirection::PreviousToCurrent : video::ExternalMotionDirection::CurrentToPrevious;
    if (s->working.depth.mapping == video::ExternalDepthMapping::FixedRange &&
        (!std::isfinite(s->working.depth.nearValue) || !std::isfinite(s->working.depth.farValue) ||
         s->working.depth.farValue <= s->working.depth.nearValue)) {
        MessageBoxW(s->hwnd, L"Fixed depth mapping requires Far > Near.", L"External Render Data", MB_ICONWARNING);
        return false;
    }
    s->accepted = true;
    DestroyWindow(s->hwnd);
    return true;
}

LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp); s = static_cast<DialogState*>(cs->lpCreateParams);
        s->hwnd = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(s));
    }
    switch (msg) {
    case WM_CREATE: CreateControls(s); return 0;
    case WM_SIZE: if (s) { LayoutDialog(s); return 0; } break;
    case WM_DPICHANGED: if (s) {
        s->dpi = HIWORD(wp);
        ApplyDialogFont(s);
        const auto* suggested = reinterpret_cast<const RECT*>(lp);
        if (suggested) SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutDialog(s);
        return 0;
    } break;
    case WM_GETMINMAXINFO: if (s) {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize.x = DpiScale(s, 980);
        mm->ptMinTrackSize.y = DpiScale(s, 760);
        return 0;
    } break;
    case WM_COMMAND: if (s) {
        switch (LOWORD(wp)) {
        case IDC_D_BROWSE: if (auto p = PickExr(hwnd)) { s->working.depth.firstFrame = *p; SetText(hwnd, IDC_D_PATH, p->wstring()); SetText(hwnd, IDC_D_RESULT, L"Not calibrated"); LoadDepthProbe(s, true); } return 0;
        case IDC_M_BROWSE: if (auto p = PickExr(hwnd)) { s->working.motion.firstFrame = *p; SetText(hwnd, IDC_M_PATH, p->wstring()); SetText(hwnd, IDC_M_RESULT, L"Not calibrated"); LoadMotionProbe(s, true); } return 0;
        case IDC_D_AUTO_CHANNEL: if (!s->depthChannels.empty()) SelectComboString(hwnd, IDC_D_CHANNEL, video::AutoSelectDepthChannel(s->depthChannels)); return 0;
        case IDC_D_ANALYZE: ApplyDepthCalibration(s); return 0;
        case IDC_D_CHANNEL_RESET: SendMessageW(GetDlgItem(hwnd, IDC_D_CHANNEL), CB_SETCURSEL, static_cast<WPARAM>(-1), 0); SetText(hwnd, IDC_D_RESULT, L"Manual / reset"); return 0;
        case IDC_M_X_AUTO: if (!s->motionChannels.empty()) SelectComboString(hwnd, IDC_M_X, video::AutoSelectMotionXChannel(s->motionChannels)); return 0;
        case IDC_M_X_RESET: SendMessageW(GetDlgItem(hwnd, IDC_M_X), CB_SETCURSEL, static_cast<WPARAM>(-1), 0); SetText(hwnd, IDC_M_RESULT, L"Manual / reset"); return 0;
        case IDC_M_Y_AUTO: if (!s->motionChannels.empty()) SelectComboString(hwnd, IDC_M_Y, video::AutoSelectMotionYChannel(s->motionChannels)); return 0;
        case IDC_M_Y_RESET: SendMessageW(GetDlgItem(hwnd, IDC_M_Y), CB_SETCURSEL, static_cast<WPARAM>(-1), 0); SetText(hwnd, IDC_M_RESULT, L"Manual / reset"); return 0;
        case IDC_M_ANALYZE: ApplyMotionCalibration(s); return 0;
        case IDC_ANALYZE_BOTH: ApplyDepthCalibration(s); ApplyMotionCalibration(s); return 0;
        case IDC_D_MAPPING: if (HIWORD(wp) == CBN_SELCHANGE) SyncDepthMapping(s); return 0;
        case IDC_D_MAPPING_RESET: SendMessageW(GetDlgItem(hwnd, IDC_D_MAPPING), CB_SETCURSEL, 0, 0); SyncDepthMapping(s); SetText(hwnd, IDC_D_RESULT, L"Manual / reset"); return 0;
        case IDC_D_NEAR_RESET: SetText(hwnd, IDC_D_NEAR, L"0.1"); SetText(hwnd, IDC_D_RESULT, L"Manual / reset"); return 0;
        case IDC_D_FAR_RESET: SetText(hwnd, IDC_D_FAR, L"100.0"); SetText(hwnd, IDC_D_RESULT, L"Manual / reset"); return 0;
        case IDC_D_INVERTED_RESET: Button_SetCheck(GetDlgItem(hwnd, IDC_D_INVERTED), BST_CHECKED); SetText(hwnd, IDC_D_RESULT, L"Manual / reset"); return 0;
        case IDC_M_SCALE_X_RESET: SetText(hwnd, IDC_M_SCALE_X, L"1.0"); SetText(hwnd, IDC_M_RESULT, L"Manual / reset"); return 0;
        case IDC_M_SCALE_Y_RESET: SetText(hwnd, IDC_M_SCALE_Y, L"1.0"); SetText(hwnd, IDC_M_RESULT, L"Manual / reset"); return 0;
        case IDC_M_FLIP_X_RESET: Button_SetCheck(GetDlgItem(hwnd, IDC_M_FLIP_X), BST_UNCHECKED); SetText(hwnd, IDC_M_RESULT, L"Manual / reset"); return 0;
        case IDC_M_FLIP_Y_RESET: Button_SetCheck(GetDlgItem(hwnd, IDC_M_FLIP_Y), BST_UNCHECKED); SetText(hwnd, IDC_M_RESULT, L"Manual / reset"); return 0;
        case IDC_M_DIRECTION_RESET: SendMessageW(GetDlgItem(hwnd, IDC_M_DIRECTION), CB_SETCURSEL, 0, 0); SetText(hwnd, IDC_M_RESULT, L"Manual / reset"); return 0;
        case IDC_OK_BUTTON: Commit(s); return 0;
        case IDC_CANCEL_BUTTON: DestroyWindow(hwnd); return 0;
        }
    } break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_NCDESTROY: if (s && s->uiFont) { DeleteObject(s->uiFont); s->uiFont = nullptr; } break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

namespace video {

bool EditExternalRenderDataSettings(HWND parent,
                                    ExternalRenderDataSettings& settings,
                                    const std::filesystem::path& sourceVideo) {
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE));
    WNDCLASSEXW wc{sizeof(wc)};
    if (!GetClassInfoExW(inst, kClassName, &wc)) {
        wc = WNDCLASSEXW{sizeof(wc)}; wc.hInstance = inst; wc.lpfnWndProc = Proc; wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        RegisterClassExW(&wc);
    }
    DialogState state; state.parent = parent; state.working = settings; state.sourceVideo = sourceVideo;
    EnableWindow(parent, FALSE);
    const UINT parentDpi = WindowDpi(parent);
    const int initialW = MulDiv(1040, static_cast<int>(parentDpi), 96);
    const int initialH = MulDiv(800, static_cast<int>(parentDpi), 96);
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, L"Crow - DLSS Rendering Tool External Render Data Auto Calibration",
        WS_CAPTION | WS_SYSMENU | WS_POPUP | WS_THICKFRAME | WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, initialW, initialH,
        parent, nullptr, inst, &state);
    if (!h) { EnableWindow(parent, TRUE); return false; }
    ShowWindow(h, SW_SHOW); UpdateWindow(h);
    MSG msg{};
    while (IsWindow(h) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent);
    if (state.accepted) settings = std::move(state.working);
    return state.accepted;
}

} // namespace video
