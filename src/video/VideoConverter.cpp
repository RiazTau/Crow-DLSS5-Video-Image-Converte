#include "VideoConverter.h"
#include "AutoDepthVideo.h"
#include "FfmpegProcess.h"
#include "AppPaths.h"
#include "D3D12Context.h"
#include "ExternalRenderData.h"
#include "TemporalFlow.h"
#include "TemporalDenoiser.h"
#include "DisFlowVideo.h"
#include "NvofFlowSession.h"
#include "ParallelRows.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using Clock = std::chrono::steady_clock;

double Milliseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

struct PerfAccumulator {
    double decode = 0.0;
    double flow = 0.0;
    double denoise = 0.0;
    double depth = 0.0;
    double depthStabilize = 0.0;
    double dlssnr = 0.0;
    double outputStabilize = 0.0;
    double encode = 0.0;
    double total = 0.0;
    uint64_t frames = 0;
};

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

std::string LowerAscii(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    }
    return s;
}

std::string NormalizeCodecName(std::string codecName) {
    codecName = LowerAscii(Trim(std::move(codecName)));
    if (codecName.empty() || codecName == "n/a" || codecName == "unknown") return {};
    if (codecName.size() > 64) return {};
    for (const unsigned char ch : codecName) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
                        ch == '_' || ch == '-' || ch == '.';
        if (!ok) return {};
    }
    return codecName;
}

bool IsAv1CodecName(const std::string& codecName) {
    const std::string n = NormalizeCodecName(codecName);
    return n == "av1" || n == "av01" || n == "aom-av1" || n == "libaom-av1";
}

double ParseFraction(const std::string& s) {
    const auto slash = s.find('/');
    try {
        if (slash == std::string::npos) return std::stod(s);
        const double a = std::stod(s.substr(0, slash));
        const double b = std::stod(s.substr(slash + 1));
        return b == 0.0 ? 0.0 : a / b;
    } catch (...) { return 0.0; }
}

std::string ReadTail(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    const std::streamoff size = end > 0 ? static_cast<std::streamoff>(end) : 0;
    const std::streamoff take = std::min<std::streamoff>(size, 8000);
    in.seekg(-take, std::ios::end);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::wstring FpsString(double fps) {
    wchar_t b[64]{};
    swprintf_s(b, L"%.8f", fps);
    std::wstring s = b;
    while (s.size() > 1 && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}

bool DecoderAdvertised(const std::string& decoders, const char* name) {
    // Match the decoder name as a whitespace-delimited token. FFmpeg's decoder
    // list contains codec names in descriptions as well, so a plain substring
    // test can incorrectly report support for a decoder that is not registered.
    std::istringstream ss(decoders);
    std::string line;
    while (std::getline(ss, line)) {
        std::istringstream ls(line);
        std::string flags;
        std::string decoder;
        if (!(ls >> flags >> decoder)) continue;
        if (decoder == name) return true;
    }
    return false;
}

std::string NarrowAscii(const std::wstring& value) {
    std::string out;
    out.reserve(value.size());
    for (wchar_t ch : value) out.push_back(ch >= 0 && ch <= 0x7f ? static_cast<char>(ch) : '?');
    return out;
}

std::string CompactLogText(const std::string& raw, size_t maxLines = 12, size_t maxChars = 2800) {
    std::istringstream ss(raw);
    std::vector<std::string> kept;
    std::string line;
    size_t chars = 0;
    while (std::getline(ss, line)) {
        line = Trim(line);
        if (line.empty()) continue;
        // Decoder errors frequently repeat the same 2-4 lines for every packet.
        // Keep the first occurrence so the GUI error stays readable while the
        // complete stderr remains in decoder-last.log / decoder-preflight-last.log.
        if (std::find(kept.begin(), kept.end(), line) != kept.end()) continue;
        if (kept.size() >= maxLines || chars + line.size() > maxChars) break;
        chars += line.size();
        kept.push_back(std::move(line));
    }
    std::ostringstream out;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i) out << '\n';
        out << kept[i];
    }
    return out.str();
}

struct DecoderPreflightAttempt {
    std::wstring decoder;
    bool advertised = true;
    bool passed = false;
    std::string detail;
};

struct DecoderSelection {
    std::wstring decoder; // Empty means FFmpeg automatic selection.
    std::vector<DecoderPreflightAttempt> attempts;
};

DecoderPreflightAttempt PreflightAv1Decoder(const std::filesystem::path& ffmpeg,
                                             const std::filesystem::path& input,
                                             const std::wstring& decoder,
                                             bool advertised,
                                             uint32_t testFrames) {
    DecoderPreflightAttempt result;
    result.decoder = decoder;
    result.advertised = advertised;
    if (!advertised) {
        result.detail = "not advertised by this FFmpeg build";
        return result;
    }

    std::vector<std::wstring> args = {
        L"-y", L"-hide_banner", L"-v", L"error", L"-xerror", L"-nostdin"
    };
    if (!decoder.empty()) args.insert(args.end(), {L"-c:v", decoder});
    args.insert(args.end(), {
        L"-i", input.wstring(),
        L"-map", L"0:v:0",
        L"-an", L"-sn", L"-dn",
        L"-frames:v", std::to_wstring(testFrames),
        L"-f", L"rawvideo",
        L"-pix_fmt", L"rgba",
        L"NUL"
    });

    try {
        // V0.6.0 only checked one frame. Some AV1 streams let libaom output an
        // initial frame and then immediately start reporting "No sequence header"
        // / corrupt-frame errors. Test a short real run and make any FFmpeg decode
        // error fatal so a noisy exit-code-0 decode cannot be accepted.
        video::RunCapture(ffmpeg, args, 45000, nullptr);
        result.passed = true;
        result.detail = "decoded " + std::to_string(testFrames) + " frame preflight to RGBA with -xerror";
    } catch (const std::exception& e) {
        result.detail = CompactLogText(e.what(), 8, 1800);
        if (result.detail.empty()) result.detail = "decoder preflight failed";
    }
    return result;
}

void WriteDecoderPreflightLog(const std::filesystem::path& logPath,
                              const std::filesystem::path& ffmpeg,
                              const std::filesystem::path& input,
                              const video::VideoInfo& info,
                              uint32_t testFrames,
                              const DecoderSelection& selection) {
    std::ofstream out(logPath, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out << "Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Native NVOF D3D12 Execute - AV1 decoder preflight\n";
    out << "FFmpeg: " << ffmpeg.string() << "\n";
    out << "Input: " << input.string() << "\n";
    out << "Codec: " << info.codecName << "\n";
    out << "Profile: " << info.profile << "\n";
    out << "Pixel format: " << info.pixelFormat << "\n";
    out << "Test frames per candidate: " << testFrames << "\n\n";
    for (const auto& a : selection.attempts) {
        const std::string name = a.decoder.empty() ? "FFmpeg automatic" : NarrowAscii(a.decoder);
        out << (a.passed ? "[PASS] " : (a.advertised ? "[FAIL] " : "[SKIP] ")) << name << "\n";
        if (!a.detail.empty()) out << a.detail << "\n";
        out << "\n";
    }
    if (!selection.decoder.empty()) out << "Selected: " << NarrowAscii(selection.decoder) << "\n";
    else if (!selection.attempts.empty() && selection.attempts.back().passed) out << "Selected: FFmpeg automatic\n";
}

DecoderSelection SelectInputDecoder(const std::filesystem::path& ffmpeg,
                                    const std::filesystem::path& input,
                                    const video::VideoInfo& info,
                                    bool av1Input,
                                    const std::filesystem::path& preflightLog) {
    DecoderSelection selection;
    if (!av1Input) return selection;

    constexpr uint32_t kPreflightFrames = 24;
    std::string decoders;
    try {
        decoders = video::RunCapture(ffmpeg, {L"-hide_banner", L"-decoders"}, 5000, nullptr);
    } catch (...) {
        // If capability enumeration itself fails, try every candidate directly.
    }

    // Prefer robust software decode, then hardware decoders, then FFmpeg native,
    // with libaom last. The V0.6.0 incident was specifically a libaom AV1 stream
    // repeatedly reporting missing sequence headers, so it should no longer win
    // simply because it can emit a single first frame.
    const struct Candidate { const char* advertised; const wchar_t* decoder; } candidates[] = {
        {"libdav1d",   L"libdav1d"},
        {"av1_cuvid",  L"av1_cuvid"},
        {"av1_qsv",    L"av1_qsv"},
        {"av1",        L"av1"},
        {"libaom-av1", L"libaom-av1"}
    };

    for (const auto& c : candidates) {
        const bool advertised = decoders.empty() || DecoderAdvertised(decoders, c.advertised);
        auto attempt = PreflightAv1Decoder(ffmpeg, input, c.decoder, advertised, kPreflightFrames);
        const bool passed = attempt.passed;
        selection.attempts.push_back(std::move(attempt));
        if (passed) {
            selection.decoder = c.decoder;
            WriteDecoderPreflightLog(preflightLog, ffmpeg, input, info, kPreflightFrames, selection);
            return selection;
        }
    }

    // Last chance: automatic selection is also subjected to the exact same real
    // multi-frame RGBA + -xerror test. This avoids silently selecting libaom again
    // when FFmpeg auto-decoding can only partially decode the stream.
    auto automatic = PreflightAv1Decoder(ffmpeg, input, {}, true, kPreflightFrames);
    const bool automaticPassed = automatic.passed;
    selection.attempts.push_back(std::move(automatic));
    WriteDecoderPreflightLog(preflightLog, ffmpeg, input, info, kPreflightFrames, selection);
    if (automaticPassed) return selection;

    std::ostringstream error;
    error << "AV1 decoder compatibility check failed before DLSSNR initialization.\n"
          << "No decoder could cleanly decode " << kPreflightFrames << " real frames to RGBA with FFmpeg -xerror.\n"
          << "Tried: libdav1d -> av1_cuvid -> av1_qsv -> native av1 -> libaom-av1 -> FFmpeg automatic.\n\n";
    for (const auto& a : selection.attempts) {
        const std::string name = a.decoder.empty() ? "FFmpeg automatic" : NarrowAscii(a.decoder);
        error << name << ": ";
        if (!a.advertised) error << "not available";
        else error << CompactLogText(a.detail, 2, 520);
        error << '\n';
    }
    error << "\nFull decoder preflight log: " << preflightLog.string();
    throw std::runtime_error(error.str());
}

std::vector<std::wstring> DecoderArgs(const video::VideoSettings& settings,
                                      const std::filesystem::path& ffmpeg,
                                      const video::VideoInfo& info,
                                      bool av1Input,
                                      const std::wstring& forcedDecoder) {
    std::vector<std::wstring> args = {
        L"-v", L"error",
        L"-nostdin"
    };
    if (av1Input) {
        // Stop on the first AV1 decode error. Continuing after corrupt packets can
        // break frame count / temporal history and floods the GUI with repeated
        // libaom diagnostics. The full stderr remains in decoder-last.log.
        args.push_back(L"-xerror");
    }

    if (!forcedDecoder.empty()) {
        args.insert(args.end(), {L"-c:v", forcedDecoder});
    }
    args.insert(args.end(), {
        L"-i", settings.input.wstring(),
        L"-map", L"0:v:0"
    });

    // FFmpeg sync-option compatibility:
    // New builds use -fps_mode passthrough, while older builds may only expose
    // the deprecated -vsync option.  Detect the installed executable instead
    // of assuming one particular FFmpeg generation.  If neither option is
    // advertised, rawvideo decoding is still allowed to proceed without an
    // explicit sync override.
    try {
        const std::string help = video::RunCapture(
            ffmpeg, {L"-hide_banner", L"-h", L"full"}, 5000, nullptr);
        if (help.find("-fps_mode") != std::string::npos) {
            args.insert(args.end(), {L"-fps_mode", L"passthrough"});
        } else if (help.find("-vsync") != std::string::npos) {
            args.insert(args.end(), {L"-vsync", L"0"});
        }
    } catch (...) {
        // Capability probing is best-effort. The decoder itself still has the
        // normal bounded waits and will report a useful FFmpeg log on failure.
    }

    args.insert(args.end(), {
        L"-f", L"rawvideo",
        L"-pix_fmt", L"rgba",
        L"pipe:1"
    });
    return args;
}

std::vector<std::wstring> EncoderArgs(const video::VideoSettings& settings, const video::VideoInfo& info) {
    std::vector<std::wstring> a = {
        L"-y", L"-v", L"error", L"-nostats",
        L"-f", L"rawvideo", L"-pix_fmt", L"rgba",
        L"-s", std::to_wstring(info.width) + L"x" + std::to_wstring(info.height),
        L"-r", FpsString(info.fps),
        L"-i", L"pipe:0",
        L"-i", settings.input.wstring(),
        L"-map", L"0:v:0",
        L"-map", L"1:a?",
        L"-map_metadata", L"1"
    };

    switch (settings.codec) {
    case video::VideoCodec::H264Nvenc:
        a.insert(a.end(), {L"-c:v", L"h264_nvenc", L"-preset", L"p5", L"-tune", L"hq", L"-rc", L"vbr", L"-cq", std::to_wstring(settings.quality), L"-b:v", L"0"});
        break;
    case video::VideoCodec::HevcNvenc:
        a.insert(a.end(), {L"-c:v", L"hevc_nvenc", L"-preset", L"p5", L"-tune", L"hq", L"-rc", L"vbr", L"-cq", std::to_wstring(settings.quality), L"-b:v", L"0"});
        break;
    case video::VideoCodec::H264Cpu:
        a.insert(a.end(), {L"-c:v", L"libx264", L"-preset", L"medium", L"-crf", std::to_wstring(settings.quality)});
        break;
    }
    a.insert(a.end(), {L"-pix_fmt", L"yuv420p", L"-c:a", L"aac", L"-b:a", L"192k", L"-shortest"});
    const auto ext = settings.output.extension().wstring();
    if (_wcsicmp(ext.c_str(), L".mp4") == 0 || _wcsicmp(ext.c_str(), L".m4v") == 0 || _wcsicmp(ext.c_str(), L".mov") == 0) {
        a.insert(a.end(), {L"-movflags", L"+faststart"});
    }
    a.push_back(settings.output.wstring());
    return a;
}

bool FfmpegStreamLooksAv1(const std::filesystem::path& ffmpeg,
                          const std::filesystem::path& input,
                          const std::atomic_bool* cancel) {
    std::string text;
    try {
        text = video::RunCapture(ffmpeg, {
            L"-hide_banner", L"-loglevel", L"info",
            L"-i", input.wstring(),
            L"-map", L"0:v:0",
            L"-frames:v", L"0",
            L"-f", L"null", L"NUL"
        }, 10000, cancel);
    } catch (const std::exception& e) {
        // Even if the zero-frame probe returns a nonzero code, FFmpeg generally
        // prints the stream description before failure. Inspect that diagnostic.
        text = e.what();
    }
    const std::string lower = LowerAscii(text);
    return lower.find("video: av1") != std::string::npos ||
           lower.find("video: aom-av1") != std::string::npos ||
           lower.find("(av01 / 0x") != std::string::npos;
}

void Emit(const video::VideoCallbacks& cb, const video::VideoProgress& p) {
    if (cb.onProgress) cb.onProgress(p);
}

} // namespace

namespace video {

VideoInfo ProbeVideo(const std::filesystem::path& input,
                     uint32_t timeoutMs,
                     const std::atomic_bool* cancel) {
    if (!std::filesystem::exists(input)) throw std::runtime_error("Input video does not exist: " + input.string());
    const auto ffprobe = FindFfprobe();
    if (ffprobe.empty()) {
        throw std::runtime_error("ffprobe.exe not found. Put FFmpeg in dist\\video\\ffmpeg\\bin or run Setup Video Dependencies.");
    }

    const auto text = RunCapture(ffprobe, {
        L"-v", L"error",
        L"-select_streams", L"v:0",
        // Keep stream + format fields in one -show_entries expression. Some
        // ffprobe generations treat repeated -show_entries options differently,
        // which can leave codec_name blank even though width/fps were reported.
        L"-show_entries", L"stream=width,height,avg_frame_rate,nb_frames,codec_name,codec_tag_string,profile,pix_fmt:format=duration",
        L"-of", L"default=noprint_wrappers=1:nokey=0",
        input.wstring()
    }, timeoutMs, cancel);

    VideoInfo info;
    std::string codecTagString;
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = Trim(line);
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        try {
            if (key == "width") info.width = static_cast<uint32_t>(std::stoul(value));
            else if (key == "height") info.height = static_cast<uint32_t>(std::stoul(value));
            else if (key == "avg_frame_rate") info.fps = ParseFraction(value);
            else if (key == "nb_frames" && value != "N/A") info.totalFrames = std::stoull(value);
            else if (key == "duration" && value != "N/A") info.durationSeconds = std::stod(value);
            else if (key == "codec_name" && value != "N/A") {
                const auto codec = NormalizeCodecName(value);
                if (!codec.empty()) info.codecName = codec;
            }
            else if (key == "codec_tag_string" && value != "N/A") codecTagString = LowerAscii(Trim(value));
            else if (key == "profile" && value != "N/A") info.profile = value;
            else if (key == "pix_fmt" && value != "N/A") info.pixelFormat = value;
        } catch (...) {}
    }
    // V0.6.1.2: keep machine-readable ffprobe metadata separate from decoder
    // diagnostics conceptually. RunCapture merges stdout/stderr, so never trust
    // the first non-empty line from a nokey query: libaom errors such as
    // "[libaom-av1 @ ...] Failed to decode frame" can otherwise be mistaken for
    // codec_name and suppress AV1 preflight. Only key=value fields and strict
    // codec tokens are accepted here. The MP4 av01 codec tag is also a reliable
    // decoder-independent AV1 signal when codec_name is unavailable.
    if (info.codecName.empty()) {
        try {
            const auto codecText = RunCapture(ffprobe, {
                L"-v", L"error",
                L"-select_streams", L"v:0",
                L"-show_entries", L"stream=codec_name,codec_tag_string",
                L"-of", L"default=noprint_wrappers=1:nokey=0",
                input.wstring()
            }, timeoutMs, cancel);
            std::istringstream codecStream(codecText);
            std::string codecLine;
            while (std::getline(codecStream, codecLine)) {
                codecLine = Trim(codecLine);
                const auto eq = codecLine.find('=');
                if (eq == std::string::npos) continue;
                const auto key = codecLine.substr(0, eq);
                const auto value = codecLine.substr(eq + 1);
                if (key == "codec_name") {
                    const auto codec = NormalizeCodecName(value);
                    if (!codec.empty()) info.codecName = codec;
                } else if (key == "codec_tag_string") {
                    codecTagString = LowerAscii(Trim(value));
                }
            }
        } catch (...) {
            // Keep the primary structured probe result. ConvertVideo performs one
            // additional FFmpeg stream-description fallback before decoder routing.
        }
    }

    info.codecName = NormalizeCodecName(info.codecName);
    if (info.codecName.empty() && codecTagString == "av01") info.codecName = "av1";

    if (!info.width || !info.height) throw std::runtime_error("ffprobe could not determine the video resolution");
    if (!(info.fps > 0.0)) throw std::runtime_error("ffprobe could not determine the video frame rate");
    if (!info.totalFrames && info.durationSeconds > 0.0) {
        info.totalFrames = static_cast<uint64_t>(std::llround(info.durationSeconds * info.fps));
    }

    try {
        const auto audio = RunCapture(ffprobe, {
            L"-v", L"error", L"-select_streams", L"a:0",
            L"-show_entries", L"stream=index", L"-of", L"csv=p=0", input.wstring()
        }, timeoutMs, cancel);
        info.hasAudio = !Trim(audio).empty();
    } catch (...) { info.hasAudio = false; }
    return info;
}

void ConvertVideo(const VideoSettings& settings,
                  const VideoCallbacks& callbacks,
                  std::atomic_bool& cancelRequested) {
    if (settings.input.empty() || settings.output.empty()) throw std::runtime_error("Input and output video paths are required");
    const auto inputAbs = std::filesystem::absolute(settings.input).lexically_normal();
    const auto outputAbs = std::filesystem::absolute(settings.output).lexically_normal();
    if (_wcsicmp(inputAbs.wstring().c_str(), outputAbs.wstring().c_str()) == 0)
        throw std::runtime_error("Output video must not overwrite the input video");

    const auto ffmpeg = FindFfmpeg();
    if (ffmpeg.empty()) {
        throw std::runtime_error("ffmpeg.exe not found. Put it in dist\\video\\ffmpeg\\bin or run Setup Video Dependencies.");
    }
    const auto runtime = app::DefaultRuntimeDll();
    if (!std::filesystem::exists(runtime)) throw std::runtime_error("nvngx_dlssnr.dll not found: " + runtime.string());

    VideoProgress progress;
    progress.stage = VideoProgress::Stage::Preparing;
    progress.message = L"Probing video...";
    Emit(callbacks, progress);
    VideoInfo info = ProbeVideo(settings.input, 15000, &cancelRequested);
    info.codecName = NormalizeCodecName(info.codecName);
    bool av1Input = IsAv1CodecName(info.codecName);
    if (!av1Input && info.codecName.empty()) {
        av1Input = FfmpegStreamLooksAv1(ffmpeg, settings.input, &cancelRequested);
        if (av1Input) info.codecName = "av1";
    }
    progress.totalFrames = info.totalFrames;

    if (!settings.output.parent_path().empty()) std::filesystem::create_directories(settings.output.parent_path());
    const auto logRoot = app::ExecutableDir() / L"video" / L"logs";
    std::filesystem::create_directories(logRoot);
    const auto decoderLog = logRoot / L"decoder-last.log";
    const auto decoderPreflightLog = logRoot / L"decoder-preflight-last.log";
    const auto encoderLog = logRoot / L"encoder-last.log";
    const auto temporalLogPath = logRoot / L"temporal-last.log";
    const auto performanceLogPath = logRoot / L"performance-last.csv";
    const auto performanceSummaryPath = logRoot / L"performance-summary-last.txt";

    progress.message = L"Checking input decoder compatibility...";
    Emit(callbacks, progress);
    const DecoderSelection decoderSelection = SelectInputDecoder(ffmpeg, settings.input, info, av1Input, decoderPreflightLog);
    const std::wstring selectedDecoder = decoderSelection.decoder;
    if (av1Input) {
        progress.message = selectedDecoder.empty()
            ? L"AV1 preflight passed: FFmpeg automatic decoder."
            : (L"AV1 preflight passed: " + selectedDecoder + L".");
        Emit(callbacks, progress);
    }

    const bool externalDepthEnabled = settings.depthMode == DepthMode::ExternalExr;
    const bool externalMotionEnabled = settings.temporalMode == TemporalMode::ExternalExr;
    std::unique_ptr<ExternalRenderDataReader> externalData;
    if (externalDepthEnabled || externalMotionEnabled) {
        progress.message = L"Validating External Render Data EXR sequences...";
        Emit(callbacks, progress);
        externalData = std::make_unique<ExternalRenderDataReader>(settings.externalData, info.width, info.height,
                                                                  info.totalFrames, externalDepthEnabled, externalMotionEnabled);
        externalData->Validate();
    }

    progress.message = L"Initializing D3D12 / DLSSNR Feature 18...";
    Emit(callbacks, progress);
    D3D12Context d3d;
    DlssNrSettings dlssSettings = settings.dlss;
    if (externalDepthEnabled) dlssSettings.depthInverted = settings.externalData.depth.depthInverted;
    DlssNrRunner runner(d3d, runtime, dlssSettings);
    std::unique_ptr<AutoDepthVideoSession> autoDepth;
    if (settings.depthMode == DepthMode::AutoDepth) {
        progress.message = L"Loading Depth Anything V2 once for the video...";
        Emit(callbacks, progress);
        autoDepth = std::make_unique<AutoDepthVideoSession>(settings.autoDepthSize, &cancelRequested);
    }

    const bool temporalEnabled = settings.temporalMode != TemporalMode::LegacyResetEveryFrame;
    std::unique_ptr<TemporalFlowEstimator> temporal;
    std::unique_ptr<DisFlowVideoSession> disFlow;
    std::unique_ptr<NvofFlowSession> nvofFlow;
    if (temporalEnabled) {
        TemporalFlowSettings t;
        t.analysisWidth = settings.flowAnalysisWidth;
        t.sceneCutThreshold = settings.sceneCutThreshold;
        t.depthHistoryWeight = settings.temporalDepthStabilization ? settings.depthHistoryWeight : 0.0f;
        t.outputHistoryWeight = settings.temporalOutputStabilization ? settings.outputHistoryWeight : 0.0f;
        temporal = std::make_unique<TemporalFlowEstimator>(t);
        if (settings.temporalMode == TemporalMode::DisOpticalFlow) {
            progress.message = L"Temporal mode: Adaptive Stable DIS optical flow (current -> previous).";
            Emit(callbacks, progress);
            disFlow = std::make_unique<DisFlowVideoSession>(info.width, info.height, settings.flowAnalysisWidth,
                                                            settings.sceneCutThreshold, &cancelRequested);
        } else if (settings.temporalMode == TemporalMode::NvidiaOpticalFlow) {
            const wchar_t* qualityName = settings.nvof.quality == NvofQuality::Fast ? L"Fast" :
                                         settings.nvof.quality == NvofQuality::Medium ? L"Medium" : L"Slow";
            progress.message = L"Temporal mode: NVIDIA Optical Flow (NVOFA) DirectX 12 - " +
                               std::to_wstring(settings.nvof.outputGridSize) + L"x" +
                               std::to_wstring(settings.nvof.outputGridSize) + L" grid, " + qualityName +
                               (settings.nvof.temporalHints ? L", temporal hints ON" : L", temporal hints OFF") +
                               (settings.nvof.outputCost ? L", cost ON." : L", cost OFF.");
            Emit(callbacks, progress);
            nvofFlow = std::make_unique<NvofFlowSession>(d3d, info.width, info.height,
                                                         settings.sceneCutThreshold, settings.nvof,
                                                         &cancelRequested);
        } else if (settings.temporalMode == TemporalMode::ExternalExr) {
            progress.message = settings.externalData.motion.direction == ExternalMotionDirection::PreviousToCurrent
                ? L"Temporal mode: External EXR forward motion auto-inverted to current -> previous pixels."
                : L"Temporal mode: External EXR motion vectors (current -> previous pixels).";
            Emit(callbacks, progress);
        } else {
            progress.message = L"Temporal mode: built-in CPU block-flow fallback.";
            Emit(callbacks, progress);
        }
    }

    std::unique_ptr<TemporalDenoiser> denoiser;
    if (settings.denoiseMode != DenoiseMode::Off) {
        TemporalDenoiseSettings d;
        d.mode = settings.denoiseMode;
        d.strength = settings.denoiseStrength;
        d.historyWeight = settings.denoiseHistoryWeight;
        d.spatialStrength = settings.denoiseSpatialStrength;
        d.detailProtection = settings.denoiseDetailProtection;
        d.maxHistory = settings.denoiseMaxHistory;
        denoiser = std::make_unique<TemporalDenoiser>(d);
        progress.message = settings.denoiseMode == DenoiseMode::FullHq
            ? L"V0.6 Full HQ denoise enabled: motion-compensated history + edge-aware spatial fallback."
            : L"V0.6 Temporal HQ denoise enabled: motion-compensated history only.";
        Emit(callbacks, progress);
    }

    PipeReaderProcess decoder(ffmpeg, DecoderArgs(settings, ffmpeg, info, av1Input, selectedDecoder), decoderLog);
    PipeWriterProcess encoder(ffmpeg, EncoderArgs(settings, info), encoderLog);

    Rgba8Image input;
    input.width = info.width; input.height = info.height;
    input.pixels.resize(static_cast<size_t>(info.width) * info.height * 4u);
    Rgba8Image previousInput;
    previousInput.width = info.width; previousInput.height = info.height;
    previousInput.pixels.resize(input.pixels.size());
    bool havePreviousInput = false;
    Rgba8Image previousStableOutput;
    std::vector<float> previousStableDepth;
    std::ofstream temporalLog(temporalLogPath, std::ios::binary | std::ios::trunc);
    if (temporalLog) temporalLog << "frame,scene_score,scene_cut,avg_mv_px,avg_confidence,reset,denoise_hist_w,denoise_spatial_w,denoise_reject,denoise_age\n";
    std::ofstream performanceLog(performanceLogPath, std::ios::binary | std::ios::trunc);
    if (performanceLog) {
        performanceLog << "frame,decode_ms,flow_ms,denoise_ms,depth_ms,depth_stabilize_ms,dlssnr_ms,output_stabilize_ms,encode_ms,total_ms\n";
    }
    PerfAccumulator perfTotal{};

    const auto started = Clock::now();
    auto lastPreview = started - std::chrono::seconds(1);
    uint64_t frame = 0;
    progress.stage = VideoProgress::Stage::Processing;
    progress.message = L"Processing frames...";
    Emit(callbacks, progress);

    try {
        while (!cancelRequested.load(std::memory_order_relaxed)) {
            const auto frameStarted = Clock::now();
            const auto decodeStarted = frameStarted;
            if (!decoder.ReadExact(input.pixels.data(), input.pixels.size(), &cancelRequested, 30000)) break;
            const auto decodeFinished = Clock::now();
            ++frame;

            // 1) Estimate motion from the untouched decoded source. Optical flow should see
            // the real source sequence, not a recursively denoised sequence.
            const auto flowStarted = Clock::now();
            TemporalFlowResult flow;
            bool temporalReset = true;
            const std::vector<float>* motionPtr = nullptr;
            if (temporalEnabled) {
                if (externalMotionEnabled) {
                    flow = externalData->LoadMotion(frame, havePreviousInput ? &previousInput : nullptr, input,
                                                    settings.flowAnalysisWidth, settings.sceneCutThreshold);
                    temporalReset = flow.sceneCut;
                } else if (nvofFlow) {
                    flow = nvofFlow->Process(input);
                    temporalReset = flow.sceneCut;
                } else if (disFlow) {
                    flow = disFlow->Process(input);
                    temporalReset = flow.sceneCut; // first DIS frame deliberately reports a cut/reset
                } else if (havePreviousInput) {
                    flow = temporal->Estimate(previousInput, input);
                    temporalReset = flow.sceneCut;
                }
                if (!temporalReset && !flow.motionXY.empty()) motionPtr = &flow.motionXY;
            }
            const auto flowFinished = Clock::now();

            // 2) V0.6 pre-NR denoise. The history is motion compensated and variance-clipped,
            // so static noise is accumulated away while scene cuts/disocclusions reject history.
            const auto denoiseStarted = Clock::now();
            TemporalDenoiseStats denoiseStats{};
            Rgba8Image nrInput = input;
            if (denoiser) {
                nrInput = denoiser->Process(input,
                                            temporalEnabled ? &flow : nullptr,
                                            !temporalEnabled || temporalReset,
                                            &denoiseStats);
            }
            const auto denoiseFinished = Clock::now();

            // 3) Depth is inferred from the denoised source. This reduces frame-random DAV2
            // depth noise before the existing motion-compensated depth stabilizer.
            const auto depthStarted = Clock::now();
            std::vector<float> depth;
            const std::vector<float>* depthPtr = nullptr;
            if (externalDepthEnabled) {
                depth = externalData->LoadDepth(frame);
                depthPtr = &depth;
            } else if (autoDepth) {
                depth = autoDepth->Process(nrInput);
                depthPtr = &depth;
            }
            const auto depthFinished = Clock::now();
            const auto depthStabilizeStarted = depthFinished;
            if (temporalEnabled && depthPtr && settings.temporalDepthStabilization &&
                !temporalReset && !previousStableDepth.empty()) {
                depth = temporal->StabilizeDepth(depth, previousStableDepth, info.width, info.height, flow);
                depthPtr = &depth;
            }
            const auto depthStabilizeFinished = Clock::now();

            // 4) DLSSNR receives the temporally coherent color/depth/MV set. Post-NR output
            // stabilization remains deliberately light; the heavy denoising happens before NR.
            const auto dlssStarted = Clock::now();
            Rgba8Image output;
            if (temporalEnabled) {
                output = runner.ProcessTemporal(nrInput, depthPtr, motionPtr, temporalReset,
                                                settings.mvecScaleX, settings.mvecScaleY);
            } else {
                output = runner.Process(nrInput, depthPtr);
            }
            const auto dlssFinished = Clock::now();
            const auto outputStabilizeStarted = dlssFinished;
            if (temporalEnabled && settings.temporalOutputStabilization &&
                !temporalReset && !previousStableOutput.pixels.empty()) {
                output = temporal->StabilizeOutput(output, previousStableOutput, flow);
            }
            const auto outputStabilizeFinished = Clock::now();

            if (temporalLog && temporalEnabled) {
                double avgMv = 0.0, avgConfidence = 0.0;
                const size_t n = flow.confidence.size();
                if (n) {
                    const unsigned workers = perf::WorkerCountForRows(info.height, 64u);
                    std::vector<double> mvSums(workers, 0.0);
                    std::vector<double> confidenceSums(workers, 0.0);
                    perf::ParallelForRows(info.height, 64u, [&](uint32_t rowBegin, uint32_t rowEnd, unsigned worker) {
                        double localMv = 0.0, localConfidence = 0.0;
                        for (uint32_t y = rowBegin; y < rowEnd; ++y) {
                            const size_t first = static_cast<size_t>(y) * info.width;
                            const size_t last = std::min(n, first + static_cast<size_t>(info.width));
                            for (size_t i = first; i < last; ++i) {
                                const double x = flow.motionXY.empty() ? 0.0 : flow.motionXY[i * 2u + 0u];
                                const double yy = flow.motionXY.empty() ? 0.0 : flow.motionXY[i * 2u + 1u];
                                localMv += std::sqrt(x * x + yy * yy);
                                localConfidence += flow.confidence[i];
                            }
                        }
                        mvSums[worker] = localMv;
                        confidenceSums[worker] = localConfidence;
                    });
                    for (unsigned i = 0; i < workers; ++i) {
                        avgMv += mvSums[i];
                        avgConfidence += confidenceSums[i];
                    }
                    avgMv /= static_cast<double>(n);
                    avgConfidence /= static_cast<double>(n);
                }
                temporalLog << frame << ',' << flow.sceneCutScore << ',' << (flow.sceneCut ? 1 : 0) << ','
                            << avgMv << ',' << avgConfidence << ',' << (temporalReset ? 1 : 0) << ','
                            << denoiseStats.averageHistoryWeight << ',' << denoiseStats.averageSpatialWeight << ','
                            << denoiseStats.rejectedFraction << ',' << denoiseStats.averageHistoryAge << "\n";
            }

            const auto encodeStarted = Clock::now();
            if (!encoder.WriteExact(output.pixels.data(), output.pixels.size(), &cancelRequested)) break;
            const auto encodeFinished = Clock::now();

            const double decodeMs = Milliseconds(decodeStarted, decodeFinished);
            const double flowMs = Milliseconds(flowStarted, flowFinished);
            const double denoiseMs = Milliseconds(denoiseStarted, denoiseFinished);
            const double depthMs = Milliseconds(depthStarted, depthFinished);
            const double depthStabilizeMs = Milliseconds(depthStabilizeStarted, depthStabilizeFinished);
            const double dlssMs = Milliseconds(dlssStarted, dlssFinished);
            const double outputStabilizeMs = Milliseconds(outputStabilizeStarted, outputStabilizeFinished);
            const double encodeMs = Milliseconds(encodeStarted, encodeFinished);
            const double totalMs = Milliseconds(frameStarted, encodeFinished);
            perfTotal.decode += decodeMs;
            perfTotal.flow += flowMs;
            perfTotal.denoise += denoiseMs;
            perfTotal.depth += depthMs;
            perfTotal.depthStabilize += depthStabilizeMs;
            perfTotal.dlssnr += dlssMs;
            perfTotal.outputStabilize += outputStabilizeMs;
            perfTotal.encode += encodeMs;
            perfTotal.total += totalMs;
            ++perfTotal.frames;
            if (performanceLog) {
                performanceLog << frame << ',' << decodeMs << ',' << flowMs << ',' << denoiseMs << ','
                               << depthMs << ',' << depthStabilizeMs << ',' << dlssMs << ','
                               << outputStabilizeMs << ',' << encodeMs << ',' << totalMs << '\n';
            }

            const auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - started).count();
            const double fps = elapsed > 0.001 ? static_cast<double>(frame) / elapsed : 0.0;
            progress.frameIndex = frame;
            progress.processingFps = fps;
            progress.elapsedSeconds = elapsed;
            if (info.totalFrames) {
                progress.fraction = std::clamp(static_cast<double>(frame) / static_cast<double>(info.totalFrames), 0.0, 1.0);
                progress.etaSeconds = fps > 0.01 ? static_cast<double>(info.totalFrames > frame ? info.totalFrames - frame : 0) / fps : 0.0;
            } else if (info.durationSeconds > 0.0) {
                progress.fraction = std::clamp((static_cast<double>(frame) / info.fps) / info.durationSeconds, 0.0, 1.0);
            }
            Emit(callbacks, progress);

            // Limit expensive UI image copies/painting to roughly 10 Hz. The processing stream itself stays full frame rate.
            if (callbacks.onPreview && std::chrono::duration<double>(now - lastPreview).count() >= 0.10) {
                callbacks.onPreview(input, output, depthPtr, motionPtr, settings.mvecScaleX, settings.mvecScaleY);
                lastPreview = now;
            }

            if (temporalEnabled) {
                // Rotate full-resolution history buffers instead of copying ~66 MB of RGBA data
                // per 4K frame (source history + stable-output history).
                std::swap(previousInput.pixels, input.pixels);
                havePreviousInput = true;
                previousStableOutput.width = info.width;
                previousStableOutput.height = info.height;
                std::swap(previousStableOutput.pixels, output.pixels);
                if (depthPtr) previousStableDepth = std::move(depth);
                else previousStableDepth.clear();
            }
        }

        if (cancelRequested.load(std::memory_order_relaxed)) {
            // Tear down NVOF explicitly while D3D12Context is unquestionably alive. The
            // GUI no longer uses thread-wide CancelSynchronousIo because it can cancel
            // driver-internal synchronous I/O while nvofapi64.dll is executing.
            nvofFlow.reset();
            decoder.Terminate(); encoder.Terminate();
            std::error_code ec; std::filesystem::remove(settings.output, ec);
            progress.stage = VideoProgress::Stage::Cancelled;
            progress.message = L"Cancelled.";
            Emit(callbacks, progress);
            return;
        }

        encoder.CloseInput();
        progress.stage = VideoProgress::Stage::Finalizing;
        progress.message = L"Finalizing video and audio...";
        progress.fraction = 1.0;
        Emit(callbacks, progress);

        const DWORD decoderCode = decoder.Wait(15000);
        const DWORD encoderCode = encoder.Wait(60000);
        if (decoderCode != 0) {
            std::string detail = "FFmpeg decoder failed.";
            if (!info.codecName.empty()) detail += "\nInput codec: " + info.codecName;
            else detail += "\nInput codec: unavailable from ffprobe";
            if (!selectedDecoder.empty()) detail += "\nSelected decoder: " + NarrowAscii(selectedDecoder);
            else if (av1Input) detail += "\nSelected decoder: FFmpeg automatic";
            detail += "\nDecoded frames before failure: " + std::to_string(frame);
            if (av1Input) {
                detail += "\nAV1 decoding is configured with -xerror so corrupt/unsupported packets stop immediately instead of contaminating temporal history.";
                if (frame == 0) {
                    detail += "\nThe input failed before the first frame reached DLSSNR; this is an FFmpeg/input decode failure, not a DLSS5 processing error.";
                }
                detail += "\nPreflight log: " + decoderPreflightLog.string();
            }
            const std::string compact = CompactLogText(ReadTail(decoderLog));
            if (!compact.empty()) detail += "\n\nDecoder diagnostics (deduplicated):\n" + compact;
            detail += "\n\nFull decoder log: " + decoderLog.string();
            throw std::runtime_error(detail);
        }
        if (encoderCode != 0) throw std::runtime_error("FFmpeg encoder failed.\n" + ReadTail(encoderLog));
        if (!std::filesystem::exists(settings.output) || std::filesystem::file_size(settings.output) == 0) {
            throw std::runtime_error("FFmpeg completed without producing a valid output file");
        }

        // Explicitly release the hardware optical-flow session before reporting completion.
        // This turns NVOF teardown into part of the worker's controlled lifetime instead of
        // leaving it to reverse-order stack unwinding after the GUI receives Completed.
        nvofFlow.reset();

        if (perfTotal.frames) {
            std::ofstream summary(performanceSummaryPath, std::ios::binary | std::ios::trunc);
            if (summary) {
                const double inv = 1.0 / static_cast<double>(perfTotal.frames);
                summary << "Crow-DLSS5-Video-Image-Converter V0.6.6-alpha2 - Native NVOF D3D12 Execute / Adaptive Stable Motion / Motion Preview / Parameter Persistence\n";
                summary << "Frames: " << perfTotal.frames << "\n";
                summary << "CPU worker limit (DLSS5_PERF_THREADS): auto unless overridden\n";
                summary << "D3D12 batching: enabled unless DLSS5_DISABLE_D3D12_BATCH is set\n\n";
                summary << "Average decode_ms: " << perfTotal.decode * inv << "\n";
                summary << "Average flow_ms: " << perfTotal.flow * inv << "\n";
                summary << "Average denoise_ms: " << perfTotal.denoise * inv << "\n";
                summary << "Average depth_ms: " << perfTotal.depth * inv << "\n";
                summary << "Average depth_stabilize_ms: " << perfTotal.depthStabilize * inv << "\n";
                summary << "Average dlssnr_ms: " << perfTotal.dlssnr * inv << "\n";
                summary << "Average output_stabilize_ms: " << perfTotal.outputStabilize * inv << "\n";
                summary << "Average encode_ms: " << perfTotal.encode * inv << "\n";
                summary << "Average total_ms: " << perfTotal.total * inv << "\n";
                summary << "Average pipeline_fps: " << (perfTotal.total > 0.0 ? 1000.0 * perfTotal.frames / perfTotal.total : 0.0) << "\n";
            }
        }

        progress.stage = VideoProgress::Stage::Completed;
        progress.message = L"Completed. Performance report: video\\logs\\performance-summary-last.txt";
        progress.frameIndex = frame;
        progress.fraction = 1.0;
        progress.elapsedSeconds = std::chrono::duration<double>(Clock::now() - started).count();
        Emit(callbacks, progress);
    } catch (...) {
        nvofFlow.reset();
        decoder.Terminate(); encoder.Terminate();
        std::error_code ec; std::filesystem::remove(settings.output, ec);
        if (cancelRequested.load(std::memory_order_relaxed)) {
            progress.stage = VideoProgress::Stage::Cancelled;
            progress.message = L"Cancelled.";
            Emit(callbacks, progress);
            return;
        }
        throw;
    }
}

} // namespace video
