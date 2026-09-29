#include "ModelBootstrap.h"
#include "AppPaths.h"
#include <Windows.h>
#include <urlmon.h>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
constexpr wchar_t kUrl[] = L"https://hf-mirror.com/onnx-community/depth-anything-v2-small/resolve/c70d1ddbcd93c9bda8098268cc3554adf5e8dd4f/onnx/model_fp16.onnx?download=true";
constexpr char kSha256[] = "2df6223f206b5164e21f664ace61dabeb9bb6a49b8b5a3e00510b4807d0f5b04";

std::string Sha256File(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0, resultLength = 0, hashLength = 0;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (st < 0) throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA256) failed");
    auto cleanupAlg = [&](){ if (alg) BCryptCloseAlgorithmProvider(alg, 0); };
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) < 0 ||
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0) < 0) {
        cleanupAlg(); throw std::runtime_error("BCryptGetProperty failed");
    }
    std::vector<UCHAR> object(objectLength), digest(hashLength);
    if (BCryptCreateHash(alg, &hash, object.data(), objectLength, nullptr, 0, 0) < 0) {
        cleanupAlg(); throw std::runtime_error("BCryptCreateHash failed");
    }
    auto cleanup = [&](){ if (hash) BCryptDestroyHash(hash); cleanupAlg(); };

    std::ifstream in(path, std::ios::binary);
    if (!in) { cleanup(); throw std::runtime_error("Cannot open model for SHA256: " + path.string()); }
    std::vector<char> buffer(4 * 1024 * 1024);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = in.gcount();
        if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) < 0) {
            cleanup(); throw std::runtime_error("BCryptHashData failed");
        }
    }
    if (BCryptFinishHash(hash, digest.data(), hashLength, 0) < 0) {
        cleanup(); throw std::runtime_error("BCryptFinishHash failed");
    }
    cleanup();
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : digest) oss << std::setw(2) << static_cast<unsigned>(b);
    return oss.str();
}
}

namespace models {
const char* DepthAnythingV2Sha256() { return kSha256; }
const wchar_t* DepthAnythingV2Url() { return kUrl; }

ModelStatus CheckDepthAnythingV2() {
    ModelStatus s;
    s.path = app::DefaultDepthModel();
    s.present = std::filesystem::exists(s.path);
    if (s.present) {
        try { s.hashOk = Sha256File(s.path) == kSha256; }
        catch (...) { s.hashOk = false; }
    }
    return s;
}

std::filesystem::path EnsureDepthAnythingV2() {
    auto status = CheckDepthAnythingV2();
    if (status.present && status.hashOk) return status.path;

    std::filesystem::create_directories(status.path.parent_path());
    auto temp = status.path;
    temp += L".download";
    std::error_code ec;
    std::filesystem::remove(temp, ec);

    const HRESULT hr = URLDownloadToFileW(nullptr, kUrl, temp.c_str(), 0, nullptr);
    if (FAILED(hr)) {
        throw std::runtime_error("Depth Anything V2 download failed, HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
    }
    const auto hash = Sha256File(temp);
    if (hash != kSha256) {
        std::filesystem::remove(temp, ec);
        throw std::runtime_error("Depth Anything V2 SHA256 mismatch. Expected " + std::string(kSha256) + ", got " + hash);
    }
    std::filesystem::remove(status.path, ec);
    std::filesystem::rename(temp, status.path);
    return status.path;
}
}
