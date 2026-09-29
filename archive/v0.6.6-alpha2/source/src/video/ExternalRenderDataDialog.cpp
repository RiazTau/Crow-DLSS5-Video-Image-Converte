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
    IDC_ANALYZE_BOTH, IDC_NOTE, IDC_OK_BUTTON, IDC_CANCEL_BUTTON
};

struct DialogState {
    HWND hwnd = nullptr;
    HWND parent = nullptr;
    std::filesystem::path sourceVideo;
    video::ExternalRenderDataSettings working;
    bool accepted = false;
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
    Make(h, L"BUTTON", L"External Depth EXR Sequence", BS_GROUPBOX, -1, 12, 10, 770, 220);
    Label(h, L"First frame", 26, 36, 84); Make(h, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, IDC_D_PATH, 112, 34, 540, 24);
    Make(h, L"BUTTON", L"Browse...", BS_PUSHBUTTON, IDC_D_BROWSE, 662, 34, 102, 24);
    Label(h, L"Sequence", 26, 66, 84); Make(h, L"STATIC", L"Not selected", SS_LEFT, IDC_D_SEQUENCE, 112, 66, 652, 22);
    Label(h, L"Channel", 26, 94, 84); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_D_CHANNEL, 112, 90, 370, 180);
    Make(h, L"BUTTON", L"Quick Auto", BS_PUSHBUTTON, IDC_D_AUTO_CHANNEL, 490, 90, 82, 24);
    Make(h, L"BUTTON", L"Auto Calibrate", BS_PUSHBUTTON, IDC_D_ANALYZE, 580, 90, 104, 24);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_CHANNEL_RESET, 692, 90, 72, 24);
    Label(h, L"Mapping", 26, 124, 84); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_D_MAPPING, 112, 120, 175, 90);
    SendMessageW(GetDlgItem(h, IDC_D_MAPPING), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Fixed Near/Far"));
    SendMessageW(GetDlgItem(h, IDC_D_MAPPING), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Raw 0..1"));
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_MAPPING_RESET, 294, 120, 60, 24);
    Label(h, L"Near", 366, 124, 44); Make(h, L"EDIT", L"0.1", WS_BORDER | ES_AUTOHSCROLL, IDC_D_NEAR, 412, 120, 90, 24);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_NEAR_RESET, 508, 120, 60, 24);
    Label(h, L"Far", 580, 124, 34); Make(h, L"EDIT", L"100", WS_BORDER | ES_AUTOHSCROLL, IDC_D_FAR, 616, 120, 76, 24);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_FAR_RESET, 700, 120, 64, 24);
    Make(h, L"BUTTON", L"Near = white / DLSSNR DepthInverted", BS_AUTOCHECKBOX, IDC_D_INVERTED, 112, 154, 300, 24);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_D_INVERTED_RESET, 420, 154, 60, 24);
    Make(h, L"STATIC", L"Not calibrated", SS_LEFT, IDC_D_RESULT, 26, 184, 738, 34);

    Make(h, L"BUTTON", L"External Motion EXR Sequence", BS_GROUPBOX, -1, 12, 238, 770, 278);
    Label(h, L"First frame", 26, 264, 84); Make(h, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, IDC_M_PATH, 112, 262, 540, 24);
    Make(h, L"BUTTON", L"Browse...", BS_PUSHBUTTON, IDC_M_BROWSE, 662, 262, 102, 24);
    Label(h, L"Sequence", 26, 294, 84); Make(h, L"STATIC", L"Not selected", SS_LEFT, IDC_M_SEQUENCE, 112, 294, 652, 22);
    Label(h, L"Motion X", 26, 322, 84); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_M_X, 112, 318, 380, 180);
    Make(h, L"BUTTON", L"Quick Auto", BS_PUSHBUTTON, IDC_M_X_AUTO, 500, 318, 82, 24);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_X_RESET, 590, 318, 70, 24);
    Make(h, L"BUTTON", L"Auto Calibrate", BS_PUSHBUTTON, IDC_M_ANALYZE, 668, 318, 96, 54);
    Label(h, L"Motion Y", 26, 352, 84); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_M_Y, 112, 348, 380, 180);
    Make(h, L"BUTTON", L"Quick Auto", BS_PUSHBUTTON, IDC_M_Y_AUTO, 500, 348, 82, 24);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_Y_RESET, 590, 348, 70, 24);
    Label(h, L"Scale X", 26, 384, 62); Make(h, L"EDIT", L"1.0", WS_BORDER | ES_AUTOHSCROLL, IDC_M_SCALE_X, 90, 380, 86, 24);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_SCALE_X_RESET, 182, 380, 56, 24);
    Label(h, L"Scale Y", 250, 384, 62); Make(h, L"EDIT", L"1.0", WS_BORDER | ES_AUTOHSCROLL, IDC_M_SCALE_Y, 314, 380, 86, 24);
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_SCALE_Y_RESET, 406, 380, 56, 24);
    Make(h, L"BUTTON", L"Flip X", BS_AUTOCHECKBOX, IDC_M_FLIP_X, 480, 380, 70, 24); Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_FLIP_X_RESET, 552, 380, 54, 24);
    Make(h, L"BUTTON", L"Flip Y", BS_AUTOCHECKBOX, IDC_M_FLIP_Y, 614, 380, 70, 24); Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_FLIP_Y_RESET, 690, 380, 54, 24);
    Label(h, L"Direction", 26, 418, 70); Make(h, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST, IDC_M_DIRECTION, 98, 414, 228, 90);
    SendMessageW(GetDlgItem(h, IDC_M_DIRECTION), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Current -> Previous (native)"));
    SendMessageW(GetDlgItem(h, IDC_M_DIRECTION), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Previous -> Current (invert)"));
    Make(h, L"BUTTON", L"Reset", BS_PUSHBUTTON, IDC_M_DIRECTION_RESET, 334, 414, 60, 24);
    Make(h, L"STATIC", L"Not calibrated", SS_LEFT, IDC_M_RESULT, 26, 450, 738, 52);

    Make(h, L"BUTTON", L"Auto Calibrate Both", BS_PUSHBUTTON, IDC_ANALYZE_BOTH, 18, 528, 150, 30);
    Make(h, L"STATIC", L"V0.6.5.1 Auto Calibration: Depth uses multi-frame global robust statistics. Motion tests channel pair, XY/sign, pixel/UV/NDC scale and current/previous direction against real adjacent video frames. LOW confidence results remain editable.", SS_LEFT, IDC_NOTE, 180, 526, 584, 54);
    Make(h, L"BUTTON", L"OK", BS_DEFPUSHBUTTON, IDC_OK_BUTTON, 580, 590, 86, 30);
    Make(h, L"BUTTON", L"Cancel", BS_PUSHBUTTON, IDC_CANCEL_BUTTON, 678, 590, 86, 30);

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

    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, L"V0.6.6-alpha2 External Render Data Auto Calibration",
        WS_CAPTION | WS_SYSMENU | WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 810, 670,
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
