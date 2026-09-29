#include "FgVideoConverter.h"
#include "DlssFrameGenerationRunner.h"
#include "AppPaths.h"
#include "D3D12Context.h"
#include "video/FfmpegProcess.h"
#include "video/NvofFlowSession.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fg {
namespace {
using Clock = std::chrono::steady_clock;

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
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

std::wstring FpsString(double fps) {
    wchar_t b[64]{}; swprintf_s(b, L"%.8f", fps);
    std::wstring s = b;
    while (s.size() > 1 && s.back() == L'0') s.pop_back();
    if (!s.empty() && s.back() == L'.') s.pop_back();
    return s;
}

std::vector<std::wstring> DecoderArgs(const FgVideoSettings& settings) {
    return {
        L"-v", L"error", L"-nostdin",
        L"-i", settings.input.wstring(),
        L"-map", L"0:v:0", L"-an", L"-sn", L"-dn",
        L"-f", L"rawvideo", L"-pix_fmt", L"rgba", L"pipe:1"
    };
}

std::vector<std::wstring> EncoderArgs(const FgVideoSettings& settings, const FgVideoInfo& info) {
    const double outputFps = info.fps * 2.0;
    std::vector<std::wstring> a = {
        L"-y", L"-v", L"error", L"-nostats",
        L"-f", L"rawvideo", L"-pix_fmt", L"rgba",
        L"-s", std::to_wstring(info.width) + L"x" + std::to_wstring(info.height),
        L"-r", FpsString(outputFps), L"-i", L"pipe:0",
        L"-i", settings.input.wstring(),
        L"-map", L"0:v:0", L"-map", L"1:a?", L"-map_metadata", L"1"
    };
    switch (settings.codec) {
    case FgVideoCodec::H264Nvenc:
        a.insert(a.end(), {L"-c:v", L"h264_nvenc", L"-preset", L"p5", L"-tune", L"hq",
                           L"-rc", L"vbr", L"-cq", std::to_wstring(settings.quality), L"-b:v", L"0"});
        break;
    case FgVideoCodec::HevcNvenc:
        a.insert(a.end(), {L"-c:v", L"hevc_nvenc", L"-preset", L"p5", L"-tune", L"hq",
                           L"-rc", L"vbr", L"-cq", std::to_wstring(settings.quality), L"-b:v", L"0"});
        break;
    case FgVideoCodec::H264Cpu:
        a.insert(a.end(), {L"-c:v", L"libx264", L"-preset", L"medium", L"-crf", std::to_wstring(settings.quality)});
        break;
    }
    a.insert(a.end(), {L"-pix_fmt", L"yuv420p", L"-c:a", L"aac", L"-b:a", L"192k", L"-shortest"});
    const auto ext = settings.output.extension().wstring();
    if (_wcsicmp(ext.c_str(), L".mp4") == 0 || _wcsicmp(ext.c_str(), L".mov") == 0 ||
        _wcsicmp(ext.c_str(), L".m4v") == 0) {
        a.insert(a.end(), {L"-movflags", L"+faststart"});
    }
    a.push_back(settings.output.wstring());
    return a;
}

void Emit(const FgCallbacks& cb, const FgProgress& p) {
    if (cb.onProgress) cb.onProgress(p);
}

std::string ReadTail(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    auto end = in.tellg();
    const std::streamoff size = end > 0 ? static_cast<std::streamoff>(end) : 0;
    const std::streamoff take = std::min<std::streamoff>(size, 6000);
    in.seekg(-take, std::ios::end);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
} // namespace

FgVideoInfo ProbeFgVideo(const std::filesystem::path& input,
                         uint32_t timeoutMs,
                         const std::atomic_bool* cancel) {
    const auto ffprobe = video::FindFfprobe();
    if (ffprobe.empty()) throw std::runtime_error("ffprobe.exe not found. Run video dependency setup first.");
    const auto text = video::RunCapture(ffprobe, {
        L"-v", L"error", L"-select_streams", L"v:0",
        L"-show_entries", L"stream=width,height,avg_frame_rate,nb_frames,codec_name:format=duration",
        L"-of", L"default=noprint_wrappers=1:nokey=0", input.wstring()
    }, timeoutMs, cancel);

    FgVideoInfo info;
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
            else if (key == "codec_name" && value != "N/A") info.codecName = value;
        } catch (...) {}
    }
    if (!info.width || !info.height) throw std::runtime_error("ffprobe could not determine video resolution");
    if (!(info.fps > 0.0)) throw std::runtime_error("ffprobe could not determine input frame rate");
    if (!info.totalFrames && info.durationSeconds > 0.0)
        info.totalFrames = static_cast<uint64_t>(std::llround(info.durationSeconds * info.fps));
    try {
        const auto audio = video::RunCapture(ffprobe, {
            L"-v", L"error", L"-select_streams", L"a:0", L"-show_entries", L"stream=index",
            L"-of", L"csv=p=0", input.wstring()
        }, timeoutMs, cancel);
        info.hasAudio = !Trim(audio).empty();
    } catch (...) { info.hasAudio = false; }
    return info;
}

void ConvertFgVideo2X(const FgVideoSettings& settings,
                      const FgCallbacks& callbacks,
                      std::atomic_bool& cancelRequested) {
    if (settings.input.empty() || settings.output.empty()) throw std::runtime_error("Input and output paths are required");
    const auto inAbs = std::filesystem::absolute(settings.input).lexically_normal();
    const auto outAbs = std::filesystem::absolute(settings.output).lexically_normal();
    if (_wcsicmp(inAbs.wstring().c_str(), outAbs.wstring().c_str()) == 0)
        throw std::runtime_error("Output must not overwrite the input video");

    const auto ffmpeg = video::FindFfmpeg();
    if (ffmpeg.empty()) throw std::runtime_error("ffmpeg.exe not found. Run dist\\video\\setup_video.ps1 first.");
    if (!settings.output.parent_path().empty()) std::filesystem::create_directories(settings.output.parent_path());

    FgProgress progress;
    progress.stage = FgProgress::Stage::Preparing;
    progress.message = L"Probing source video...";
    Emit(callbacks, progress);
    const FgVideoInfo info = ProbeFgVideo(settings.input, 15000, &cancelRequested);
    progress.totalRealFrames = info.totalFrames;

    const auto logRoot = app::DistributionRoot() / L"logs" / L"fg";
    std::filesystem::create_directories(logRoot);
    const auto decoderLog = logRoot / L"decoder-last.log";
    const auto encoderLog = logRoot / L"encoder-last.log";
    const auto fgLogPath = logRoot / L"fg-last.csv";

    progress.message = L"Initializing D3D12, NVOF and DLSS Frame Generation...";
    Emit(callbacks, progress);
    D3D12Context d3d;
    video::NvofFlowSession nvof(d3d, info.width, info.height,
                                 settings.sceneCutThreshold, settings.nvof, &cancelRequested);
    DlssFrameGenerationRunner fgRunner(d3d, app::DistributionRoot() / L"runtime");

    video::PipeReaderProcess decoder(ffmpeg, DecoderArgs(settings), decoderLog);
    video::PipeWriterProcess encoder(ffmpeg, EncoderArgs(settings, info), encoderLog);

    Rgba8Image current;
    current.width = info.width; current.height = info.height;
    current.pixels.resize(static_cast<size_t>(info.width) * info.height * 4u);
    Rgba8Image previous;
    previous.width = info.width; previous.height = info.height;

    std::ofstream fgLog(fgLogPath, std::ios::binary | std::ios::trunc);
    if (fgLog) fgLog << "real_frame,scene_score,scene_cut,dlss_generated,fallback,output_frames\n";

    const auto started = Clock::now();
    uint64_t realFrames = 0;
    uint64_t outputFrames = 0;
    uint64_t generatedFrames = 0;
    uint64_t fallbackFrames = 0;

    try {
        // Prime temporal history with the first real frame.
        if (!decoder.ReadExact(current.pixels.data(), current.pixels.size(), &cancelRequested, 30000))
            throw std::runtime_error("Input video produced no decodable frames");
        ++realFrames;
        auto firstFlow = nvof.Process(current);
        (void)fgRunner.GenerateFrame(current, firstFlow.motionXY, nullptr, false, true, 0, 1, 1);
        if (!encoder.WriteExact(current.pixels.data(), current.pixels.size(), &cancelRequested))
            throw std::runtime_error("Encoder stopped while writing the first frame");
        ++outputFrames;
        previous = current;

        progress.stage = FgProgress::Stage::Processing;
        progress.message = L"2X FG processing: NVOF motion + DLSS-G + zero-depth alpha guidance.";
        Emit(callbacks, progress);

        while (!cancelRequested.load(std::memory_order_relaxed)) {
            if (!decoder.ReadExact(current.pixels.data(), current.pixels.size(), &cancelRequested, 30000)) break;
            const uint64_t frameId = realFrames;
            ++realFrames;
            auto flow = nvof.Process(current);
            auto generated = fgRunner.GenerateFrame(current, flow.motionXY, nullptr, false, flow.sceneCut, frameId, 1, 1);

            bool fallback = false;
            if (generated) {
                if (!encoder.WriteExact(generated->pixels.data(), generated->pixels.size(), &cancelRequested)) break;
                ++generatedFrames;
            } else {
                // Preserve exact CFR 2X timing across scene cuts / SDK interpolation-disable frames.
                if (!encoder.WriteExact(previous.pixels.data(), previous.pixels.size(), &cancelRequested)) break;
                ++fallbackFrames;
                fallback = true;
            }
            ++outputFrames;
            if (!encoder.WriteExact(current.pixels.data(), current.pixels.size(), &cancelRequested)) break;
            ++outputFrames;

            if (fgLog) fgLog << frameId << ',' << flow.sceneCutScore << ',' << (flow.sceneCut ? 1 : 0) << ','
                             << (generated ? 1 : 0) << ',' << (fallback ? 1 : 0) << ',' << outputFrames << '\n';
            previous = current;

            const double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
            progress.realFrameIndex = realFrames;
            progress.outputFrames = outputFrames;
            progress.elapsedSeconds = elapsed;
            progress.processingFps = elapsed > 0.001 ? static_cast<double>(realFrames) / elapsed : 0.0;
            if (info.totalFrames)
                progress.fraction = std::clamp(static_cast<double>(realFrames) / static_cast<double>(info.totalFrames), 0.0, 1.0);
            Emit(callbacks, progress);
        }

        if (cancelRequested.load(std::memory_order_relaxed)) {
            fgRunner.Drain();
            decoder.Terminate(); encoder.Terminate();
            std::error_code ec; std::filesystem::remove(settings.output, ec);
            progress.stage = FgProgress::Stage::Cancelled;
            progress.message = L"Cancelled.";
            Emit(callbacks, progress);
            return;
        }

        // 2*N output frames at 2*FPS gives the same nominal CFR duration as N frames at FPS.
        if (!encoder.WriteExact(previous.pixels.data(), previous.pixels.size(), &cancelRequested))
            throw std::runtime_error("Encoder stopped while writing final duration-preserving frame");
        ++outputFrames;

        progress.stage = FgProgress::Stage::Finalizing;
        progress.message = L"Finalizing 2X video and audio...";
        progress.fraction = 1.0;
        Emit(callbacks, progress);
        fgRunner.Drain();
        encoder.CloseInput();
        const DWORD decoderCode = decoder.Wait(15000);
        const DWORD encoderCode = encoder.Wait(60000);
        if (decoderCode != 0) throw std::runtime_error("FFmpeg decoder failed.\n" + ReadTail(decoderLog));
        if (encoderCode != 0) throw std::runtime_error("FFmpeg encoder failed.\n" + ReadTail(encoderLog));
        if (!std::filesystem::exists(settings.output) || std::filesystem::file_size(settings.output) == 0)
            throw std::runtime_error("Encoder completed without a valid output file");

        progress.stage = FgProgress::Stage::Completed;
        progress.realFrameIndex = realFrames;
        progress.outputFrames = outputFrames;
        progress.fraction = 1.0;
        progress.elapsedSeconds = std::chrono::duration<double>(Clock::now() - started).count();
        progress.message = L"Completed: 2X output. DLSS generated=" + std::to_wstring(generatedFrames) +
                           L", timing fallbacks=" + std::to_wstring(fallbackFrames) + L".";
        Emit(callbacks, progress);
    } catch (...) {
        try { fgRunner.Drain(); } catch (...) {}
        decoder.Terminate(); encoder.Terminate();
        std::error_code ec; std::filesystem::remove(settings.output, ec);
        if (cancelRequested.load(std::memory_order_relaxed)) {
            progress.stage = FgProgress::Stage::Cancelled;
            progress.message = L"Cancelled.";
            Emit(callbacks, progress);
            return;
        }
        throw;
    }
}

} // namespace fg
