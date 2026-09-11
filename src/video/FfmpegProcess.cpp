#include "FfmpegProcess.h"
#include "AppPaths.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace {

void CloseIfValid(HANDLE& h) noexcept {
    if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
}

HANDLE OpenLog(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot create FFmpeg log: " + path.string());
    }
    return h;
}

std::filesystem::path FindOnPath(const wchar_t* exeName) {
    wchar_t buffer[32768]{};
    DWORD n = SearchPathW(nullptr, exeName, nullptr, static_cast<DWORD>(32768), buffer, nullptr);
    if (n && n < 32768) return std::filesystem::path(buffer);
    return {};
}

std::filesystem::path FindTool(const wchar_t* exeName) {
    const auto root = app::ExecutableDir();
    const std::array<std::filesystem::path, 4> candidates = {
        root / L"video" / L"ffmpeg" / L"bin" / exeName,
        root / L"video" / exeName,
        root / L"ffmpeg" / L"bin" / exeName,
        root / exeName,
    };
    for (const auto& p : candidates) if (std::filesystem::exists(p)) return p;
    return FindOnPath(exeName);
}

void SpawnWithHandles(const std::filesystem::path& exe,
                      const std::vector<std::wstring>& args,
                      HANDLE stdIn, HANDLE stdOut, HANDLE stdErr,
                      HANDLE* process, HANDLE* thread) {
    std::wstring cmd = video::JoinCommand(exe, args);
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = stdIn;
    si.hStdOutput = stdOut;
    si.hStdError = stdErr;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreateProcessW");
    }
    *process = pi.hProcess;
    *thread = pi.hThread;
}

} // namespace

namespace video {

std::wstring QuoteArg(const std::wstring& value) {
    if (value.empty()) return L"\"\"";
    const bool needsQuotes = value.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) return value;
    std::wstring out = L"\"";
    size_t slashCount = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashCount;
        } else if (ch == L'\"') {
            out.append(slashCount * 2 + 1, L'\\');
            out.push_back(L'\"');
            slashCount = 0;
        } else {
            out.append(slashCount, L'\\');
            slashCount = 0;
            out.push_back(ch);
        }
    }
    out.append(slashCount * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

std::wstring JoinCommand(const std::filesystem::path& exe, const std::vector<std::wstring>& args) {
    std::wstring out = QuoteArg(exe.wstring());
    for (const auto& a : args) {
        out.push_back(L' ');
        out += QuoteArg(a);
    }
    return out;
}

std::filesystem::path FindFfmpeg() { return FindTool(L"ffmpeg.exe"); }
std::filesystem::path FindFfprobe() { return FindTool(L"ffprobe.exe"); }

std::string RunCapture(const std::filesystem::path& exe,
                       const std::vector<std::wstring>& args,
                       DWORD timeoutMs,
                       const std::atomic_bool* cancel) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = INVALID_HANDLE_VALUE, writePipe = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe");
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE process = nullptr, thread = nullptr;
    try {
        SpawnWithHandles(exe, args, nul, writePipe, writePipe, &process, &thread);
        CloseIfValid(writePipe);
        CloseIfValid(nul);

        std::string out;
        std::array<char, 4096> buf{};
        const ULONGLONG started = GetTickCount64();
        bool processExited = false;
        for (;;) {
            if (cancel && cancel->load(std::memory_order_relaxed)) {
                TerminateProcess(process, 1);
                WaitForSingleObject(process, 1000);
                throw std::runtime_error("Process cancelled");
            }
            if (timeoutMs != INFINITE && GetTickCount64() - started > timeoutMs) {
                TerminateProcess(process, 1);
                WaitForSingleObject(process, 1000);
                throw std::runtime_error("Process timed out after " + std::to_string(timeoutMs / 1000) + " seconds\n" + out);
            }

            DWORD available = 0;
            if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) {
                const DWORD err = GetLastError();
                if (err == ERROR_BROKEN_PIPE) break;
                throw std::system_error(static_cast<int>(err), std::system_category(), "PeekNamedPipe");
            }
            if (available) {
                DWORD got = 0;
                const DWORD want = std::min<DWORD>(available, static_cast<DWORD>(buf.size()));
                if (!ReadFile(readPipe, buf.data(), want, &got, nullptr)) {
                    const DWORD err = GetLastError();
                    if (err == ERROR_BROKEN_PIPE) break;
                    throw std::system_error(static_cast<int>(err), std::system_category(), "ReadFile");
                }
                if (got) out.append(buf.data(), got);
                continue;
            }

            if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
                if (processExited) break;
                processExited = true;
                // Give the pipe one short turn to expose any final buffered bytes.
                Sleep(1);
                continue;
            }
            Sleep(5);
        }

        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        CloseIfValid(readPipe);
        CloseIfValid(thread);
        CloseIfValid(process);
        if (code != 0) throw std::runtime_error("Process failed with exit code " + std::to_string(code) + "\n" + out);
        return out;
    } catch (...) {
        if (process) TerminateProcess(process, 1);
        CloseIfValid(readPipe); CloseIfValid(writePipe); CloseIfValid(nul);
        CloseIfValid(thread); CloseIfValid(process);
        throw;
    }
}

PipeReaderProcess::PipeReaderProcess(const std::filesystem::path& exe,
                                     const std::vector<std::wstring>& args,
                                     const std::filesystem::path& logPath) {
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE childWrite = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&_read, &childWrite, &sa, 1u << 20)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe decoder");
    }
    SetHandleInformation(_read, HANDLE_FLAG_INHERIT, 0);
    HANDLE log = OpenLog(logPath);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    try {
        SpawnWithHandles(exe, args, nul, childWrite, log, &_process, &_thread);
        CloseIfValid(childWrite); CloseIfValid(log); CloseIfValid(nul);
    } catch (...) {
        CloseIfValid(_read); CloseIfValid(childWrite); CloseIfValid(log); CloseIfValid(nul);
        throw;
    }
}

PipeReaderProcess::~PipeReaderProcess() {
    CloseIfValid(_read);
    if (_process && WaitForSingleObject(_process, 0) == WAIT_TIMEOUT) TerminateProcess(_process, 1);
    CloseIfValid(_thread); CloseIfValid(_process);
}

bool PipeReaderProcess::ReadExact(void* dst, size_t bytes,
                                      const std::atomic_bool* cancel,
                                      DWORD stallTimeoutMs) {
    auto* p = static_cast<uint8_t*>(dst);
    size_t done = 0;
    ULONGLONG lastProgress = GetTickCount64();
    while (done < bytes) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;

        DWORD available = 0;
        if (!PeekNamedPipe(_read, nullptr, 0, nullptr, &available, nullptr)) {
            const DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE) {
                if (done == 0) return false;
                throw std::runtime_error("FFmpeg decoder ended in the middle of a frame");
            }
            throw std::system_error(static_cast<int>(err), std::system_category(), "PeekNamedPipe decoder");
        }

        if (available) {
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(std::min<size_t>(bytes - done, available), 1u << 20));
            DWORD got = 0;
            if (!ReadFile(_read, p + done, chunk, &got, nullptr)) {
                const DWORD err = GetLastError();
                if (err == ERROR_OPERATION_ABORTED && cancel && cancel->load(std::memory_order_relaxed)) return false;
                if (err == ERROR_BROKEN_PIPE && done == 0) return false;
                throw std::system_error(static_cast<int>(err), std::system_category(), "ReadFile decoder");
            }
            if (got) {
                done += got;
                lastProgress = GetTickCount64();
                continue;
            }
        }

        if (_process && WaitForSingleObject(_process, 0) == WAIT_OBJECT_0) {
            if (done == 0) return false;
            throw std::runtime_error("FFmpeg decoder exited in the middle of a frame");
        }
        if (stallTimeoutMs != INFINITE && GetTickCount64() - lastProgress > stallTimeoutMs) {
            throw std::runtime_error("FFmpeg decoder produced no data for " + std::to_string(stallTimeoutMs / 1000) + " seconds");
        }
        Sleep(5);
    }
    return true;
}

DWORD PipeReaderProcess::Wait(DWORD timeoutMs) {
    if (!_process) return 0;
    const DWORD wait = WaitForSingleObject(_process, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(_process, 1);
        WaitForSingleObject(_process, 1000);
        throw std::runtime_error("FFmpeg decoder did not exit within the finalization timeout");
    }
    if (wait != WAIT_OBJECT_0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "WaitForSingleObject decoder");
    }
    DWORD code = 0; GetExitCodeProcess(_process, &code); return code;
}

void PipeReaderProcess::Terminate() noexcept {
    if (_read != INVALID_HANDLE_VALUE) CancelIoEx(_read, nullptr);
    if (_process) TerminateProcess(_process, 1);
    CloseIfValid(_read);
}

PipeWriterProcess::PipeWriterProcess(const std::filesystem::path& exe,
                                     const std::vector<std::wstring>& args,
                                     const std::filesystem::path& logPath) {
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE childRead = INVALID_HANDLE_VALUE;
    if (!CreatePipe(&childRead, &_write, &sa, 1u << 20)) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "CreatePipe encoder");
    }
    SetHandleInformation(_write, HANDLE_FLAG_INHERIT, 0);
    HANDLE log = OpenLog(logPath);
    try {
        SpawnWithHandles(exe, args, childRead, log, log, &_process, &_thread);
        CloseIfValid(childRead); CloseIfValid(log);
    } catch (...) {
        CloseIfValid(childRead); CloseIfValid(_write); CloseIfValid(log);
        throw;
    }
}

PipeWriterProcess::~PipeWriterProcess() {
    CloseInput();
    if (_process && WaitForSingleObject(_process, 0) == WAIT_TIMEOUT) TerminateProcess(_process, 1);
    CloseIfValid(_thread); CloseIfValid(_process);
}

bool PipeWriterProcess::WriteExact(const void* src, size_t bytes,
                                      const std::atomic_bool* cancel) {
    const auto* p = static_cast<const uint8_t*>(src);
    size_t done = 0;
    while (done < bytes) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes - done, 1u << 20));
        DWORD wrote = 0;
        if (!WriteFile(_write, p + done, chunk, &wrote, nullptr)) {
            const DWORD err = GetLastError();
            if (err == ERROR_OPERATION_ABORTED && cancel && cancel->load(std::memory_order_relaxed)) return false;
            throw std::system_error(static_cast<int>(err), std::system_category(), "WriteFile encoder");
        }
        if (!wrote) throw std::runtime_error("FFmpeg encoder pipe accepted zero bytes");
        done += wrote;
    }
    return true;
}

void PipeWriterProcess::CloseInput() noexcept { CloseIfValid(_write); }

DWORD PipeWriterProcess::Wait(DWORD timeoutMs) {
    if (!_process) return 0;
    const DWORD wait = WaitForSingleObject(_process, timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(_process, 1);
        WaitForSingleObject(_process, 1000);
        throw std::runtime_error("FFmpeg encoder did not exit within the finalization timeout");
    }
    if (wait != WAIT_OBJECT_0) {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "WaitForSingleObject encoder");
    }
    DWORD code = 0; GetExitCodeProcess(_process, &code); return code;
}

void PipeWriterProcess::Terminate() noexcept {
    if (_write != INVALID_HANDLE_VALUE) CancelIoEx(_write, nullptr);
    CloseInput();
    if (_process) TerminateProcess(_process, 1);
}

} // namespace video
