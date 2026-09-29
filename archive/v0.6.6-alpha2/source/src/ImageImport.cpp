#include "ImageImport.h"
#include <tinyexr.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <limits>
#include <stdexcept>

namespace {
std::string NarrowPath(const std::filesystem::path& p) {
    return p.u8string().empty() ? std::string{} : std::string(reinterpret_cast<const char*>(p.u8string().c_str()));
}

float SrgbEncode1(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

float ToneMap1(float x, ExrToneMap mode) {
    x = std::max(0.0f, x);
    switch (mode) {
    case ExrToneMap::Clamp:
        return std::clamp(x, 0.0f, 1.0f);
    case ExrToneMap::Reinhard:
        return x / (1.0f + x);
    case ExrToneMap::AcesFitted:
    default: {
        const float y = (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
        return std::clamp(y, 0.0f, 1.0f);
    }
    }
}

std::vector<float> LoadExrRgba(const std::filesystem::path& path, const std::string& layer,
                               int& width, int& height) {
    float* rgba = nullptr;
    const char* err = nullptr;
    const std::string filename = NarrowPath(path);
    const char* layerPtr = layer.empty() ? nullptr : layer.c_str();
    const int ret = LoadEXRWithLayer(&rgba, &width, &height, filename.c_str(), layerPtr, &err);
    if (ret != TINYEXR_SUCCESS || !rgba) {
        std::string message = "TinyEXR LoadEXRWithLayer failed";
        if (err) {
            message += ": ";
            message += err;
            FreeEXRErrorMessage(err);
        }
        throw std::runtime_error(message);
    }
    std::vector<float> out(rgba, rgba + static_cast<size_t>(width) * height * 4u);
    free(rgba);
    return out;
}


FloatChannelMap LoadExrFloatChannelInternal(const std::filesystem::path& path, const std::string& channelName) {
    uint32_t width = 0, height = 0;
    if (channelName.empty()) throw std::runtime_error("EXR channel name is empty");
    const std::string filename = NarrowPath(path);
    EXRVersion version{};
    int ret = ParseEXRVersionFromFile(&version, filename.c_str());
    if (ret != TINYEXR_SUCCESS) throw std::runtime_error("TinyEXR ParseEXRVersionFromFile failed");
    if (version.multipart) throw std::runtime_error("Exact EXR channel import currently supports single-part EXR only");

    EXRHeader header{};
    InitEXRHeader(&header);
    const char* err = nullptr;
    ret = ParseEXRHeaderFromFile(&header, &version, filename.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::string message = "TinyEXR ParseEXRHeaderFromFile failed";
        if (err) { message += ": "; message += err; FreeEXRErrorMessage(err); }
        FreeEXRHeader(&header);
        throw std::runtime_error(message);
    }

    int channelIndex = -1;
    for (int i = 0; i < header.num_channels; ++i) {
        if (channelName == header.channels[i].name) { channelIndex = i; break; }
    }
    if (channelIndex < 0) {
        FreeEXRHeader(&header);
        throw std::runtime_error("EXR channel not found: " + channelName);
    }

    // Blender Z/position/vector passes are normally HALF/FLOAT. Ask TinyEXR to expose
    // all HALF channels as float so callers do not need to decode half values themselves.
    for (int i = 0; i < header.num_channels; ++i) {
        if (header.pixel_types[i] == TINYEXR_PIXELTYPE_HALF) {
            header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
        }
    }

    EXRImage image{};
    InitEXRImage(&image);
    err = nullptr;
    ret = LoadEXRImageFromFile(&image, &header, filename.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::string message = "TinyEXR LoadEXRImageFromFile failed";
        if (err) { message += ": "; message += err; FreeEXRErrorMessage(err); }
        FreeEXRImage(&image);
        FreeEXRHeader(&header);
        throw std::runtime_error(message);
    }
    if (!image.images) {
        FreeEXRImage(&image);
        FreeEXRHeader(&header);
        throw std::runtime_error("Exact EXR channel import does not yet support tiled EXR");
    }

    width = static_cast<uint32_t>(image.width);
    height = static_cast<uint32_t>(image.height);
    const size_t count = static_cast<size_t>(width) * height;
    std::vector<float> out(count);
    const int loadedType = header.requested_pixel_types[channelIndex];
    if (loadedType == TINYEXR_PIXELTYPE_FLOAT) {
        const float* src = reinterpret_cast<const float*>(image.images[channelIndex]);
        std::copy(src, src + count, out.begin());
    } else if (loadedType == TINYEXR_PIXELTYPE_UINT) {
        const unsigned int* src = reinterpret_cast<const unsigned int*>(image.images[channelIndex]);
        for (size_t i = 0; i < count; ++i) out[i] = static_cast<float>(src[i]);
    } else {
        FreeEXRImage(&image);
        FreeEXRHeader(&header);
        throw std::runtime_error("Unsupported EXR channel pixel type for exact-channel import");
    }

    FreeEXRImage(&image);
    FreeEXRHeader(&header);
    FloatChannelMap result;
    result.width = width;
    result.height = height;
    result.values = std::move(out);
    return result;
}

std::vector<float> ResizeFloatBilinear(const std::vector<float>& src, uint32_t sw, uint32_t sh,
                                       uint32_t dw, uint32_t dh) {
    if (sw == dw && sh == dh) return src;
    if (!sw || !sh || !dw || !dh) throw std::runtime_error("Invalid depth resize dimensions");
    std::vector<float> dst(static_cast<size_t>(dw) * dh);
    const float sx = static_cast<float>(sw) / static_cast<float>(dw);
    const float sy = static_cast<float>(sh) / static_cast<float>(dh);
    for (uint32_t y = 0; y < dh; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, static_cast<int>(sh) - 1);
        const int y1 = std::min(y0 + 1, static_cast<int>(sh) - 1);
        const float ty = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
        for (uint32_t x = 0; x < dw; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * sx - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, static_cast<int>(sw) - 1);
            const int x1 = std::min(x0 + 1, static_cast<int>(sw) - 1);
            const float tx = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f);
            const float a = src[static_cast<size_t>(y0) * sw + x0];
            const float b = src[static_cast<size_t>(y0) * sw + x1];
            const float c = src[static_cast<size_t>(y1) * sw + x0];
            const float d = src[static_cast<size_t>(y1) * sw + x1];
            const float top = a + (b - a) * tx;
            const float bottom = c + (d - c) * tx;
            dst[static_cast<size_t>(y) * dw + x] = top + (bottom - top) * ty;
        }
    }
    return dst;
}

void NormalizeDepth(std::vector<float>& v) {
    float lo = std::numeric_limits<float>::infinity();
    float hi = -std::numeric_limits<float>::infinity();
    for (float x : v) {
        if (!std::isfinite(x)) continue;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    if (!std::isfinite(lo) || !std::isfinite(hi) || hi - lo < 1e-12f) {
        std::fill(v.begin(), v.end(), 0.0f);
        return;
    }
    const float inv = 1.0f / (hi - lo);
    for (float& x : v) x = std::clamp((std::isfinite(x) ? x : lo) - lo, 0.0f, hi - lo) * inv;
}
}

bool IsExrPath(const std::filesystem::path& path) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".exr";
}

std::vector<std::string> ListExrLayers(const std::filesystem::path& path) {
    std::vector<std::string> out;
    if (!IsExrPath(path)) return out;
    const std::string filename = NarrowPath(path);
    const char** names = nullptr;
    int count = 0;
    const char* err = nullptr;
    const int ret = EXRLayers(filename.c_str(), &names, &count, &err);
    if (ret != TINYEXR_SUCCESS) {
        std::string message = "TinyEXR EXRLayers failed";
        if (err) {
            message += ": ";
            message += err;
            FreeEXRErrorMessage(err);
        }
        throw std::runtime_error(message);
    }
    for (int i = 0; i < count; ++i) if (names[i]) out.emplace_back(names[i]);
    if (names) {
        for (int i = 0; i < count; ++i) free(const_cast<char*>(names[i]));
        free(names);
    }
    return out;
}


std::vector<std::string> ListExrChannels(const std::filesystem::path& path) {
    std::vector<std::string> out;
    if (!IsExrPath(path)) return out;
    const std::string filename = NarrowPath(path);
    EXRVersion version{};
    if (ParseEXRVersionFromFile(&version, filename.c_str()) != TINYEXR_SUCCESS) {
        throw std::runtime_error("TinyEXR ParseEXRVersionFromFile failed");
    }
    if (version.multipart) {
        throw std::runtime_error("V0.3 channel listing supports single-part EXR only");
    }
    EXRHeader header{};
    InitEXRHeader(&header);
    const char* err = nullptr;
    const int ret = ParseEXRHeaderFromFile(&header, &version, filename.c_str(), &err);
    if (ret != TINYEXR_SUCCESS) {
        std::string message = "TinyEXR ParseEXRHeaderFromFile failed";
        if (err) { message += ": "; message += err; FreeEXRErrorMessage(err); }
        FreeEXRHeader(&header);
        throw std::runtime_error(message);
    }
    for (int i = 0; i < header.num_channels; ++i) out.emplace_back(header.channels[i].name);
    FreeEXRHeader(&header);
    return out;
}

FloatChannelMap LoadExrFloatChannel(const std::filesystem::path& path, const std::string& channelName) {
    if (!IsExrPath(path)) throw std::runtime_error("Float EXR channel import requires an .exr file");
    return LoadExrFloatChannelInternal(path, channelName);
}

ImportedImage LoadImageForDlss(const std::filesystem::path& path, const ImageImportSettings& settings) {
    ImportedImage out;
    if (!IsExrPath(path)) {
        out.display = LoadImageRgba8(path);
        return out;
    }

    out.fromExr = true;
    out.exrLayers = ListExrLayers(path);
    std::string layer = settings.exrLayer;
    if (layer.empty() && !out.exrLayers.empty()) {
        auto it = std::find_if(out.exrLayers.begin(), out.exrLayers.end(), [](const std::string& s) {
            return s.find("Combined") != std::string::npos || s.find("combined") != std::string::npos;
        });
        layer = it != out.exrLayers.end() ? *it : out.exrLayers.front();
    }
    out.selectedLayer = layer;

    int w = 0, h = 0;
    const auto rgba = LoadExrRgba(path, layer, w, h);
    out.display.width = static_cast<uint32_t>(w);
    out.display.height = static_cast<uint32_t>(h);
    out.display.pixels.resize(static_cast<size_t>(w) * h * 4u);
    const float exposure = std::exp2(settings.exposureEv);
    for (size_t i = 0, p = 0; i < rgba.size(); i += 4, p += 4) {
        for (int c = 0; c < 3; ++c) {
            float v = ToneMap1(rgba[i + c] * exposure, settings.toneMap);
            if (settings.srgbEncode) v = SrgbEncode1(v);
            out.display.pixels[p + c] = static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
        }
        const float a = std::clamp(rgba[i + 3], 0.0f, 1.0f);
        out.display.pixels[p + 3] = static_cast<uint8_t>(std::lround(a * 255.0f));
    }
    return out;
}

DepthMap LoadDepthMap(const std::filesystem::path& path, const DepthImportSettings& settings,
                      uint32_t targetWidth, uint32_t targetHeight) {
    DepthMap out;
    uint32_t sw = 0, sh = 0;
    std::vector<float> values;

    if (IsExrPath(path)) {
        if (!settings.exrChannel.empty()) {
            auto channel = LoadExrFloatChannelInternal(path, settings.exrChannel);
            sw = channel.width;
            sh = channel.height;
            values = std::move(channel.values);
        } else {
            int w = 0, h = 0;
            const auto rgba = LoadExrRgba(path, settings.exrLayer, w, h);
            sw = static_cast<uint32_t>(w);
            sh = static_cast<uint32_t>(h);
            values.resize(static_cast<size_t>(sw) * sh);
            for (size_t i = 0; i < values.size(); ++i) {
                const float r = rgba[i * 4 + 0], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
                switch (settings.channel) {
                case 1: values[i] = g; break;
                case 2: values[i] = b; break;
                case 3: values[i] = a; break;
                case 4: values[i] = r * 0.2126f + g * 0.7152f + b * 0.0722f; break;
                case 0:
                default: values[i] = r; break;
                }
            }
        }
    } else {
        const auto img = LoadImageRgba8(path);
        sw = img.width;
        sh = img.height;
        values.resize(static_cast<size_t>(sw) * sh);
        for (size_t i = 0; i < values.size(); ++i) {
            const float r = img.pixels[i * 4 + 0] / 255.0f;
            const float g = img.pixels[i * 4 + 1] / 255.0f;
            const float b = img.pixels[i * 4 + 2] / 255.0f;
            const float a = img.pixels[i * 4 + 3] / 255.0f;
            switch (settings.channel) {
            case 1: values[i] = g; break;
            case 2: values[i] = b; break;
            case 3: values[i] = a; break;
            case 4: values[i] = r * 0.2126f + g * 0.7152f + b * 0.0722f; break;
            case 0:
            default: values[i] = r; break;
            }
        }
    }

    if (settings.autoNormalize) NormalizeDepth(values);
    for (float& v : values) v = std::clamp(v * settings.scale + settings.offset, 0.0f, 1.0f);
    values = ResizeFloatBilinear(values, sw, sh, targetWidth, targetHeight);
    out.width = targetWidth;
    out.height = targetHeight;
    out.values = std::move(values);
    out.inverseDepth = settings.inverseDepth;
    return out;
}

void SaveOutputImage(const std::filesystem::path& path, const Rgba8Image& image) {
    if (IsExrPath(path)) {
        std::vector<float> rgba(image.pixels.size());
        for (size_t i = 0; i < image.pixels.size(); ++i) rgba[i] = image.pixels[i] / 255.0f;
        const std::string filename = NarrowPath(path);
        const char* err = nullptr;
        const int ret = SaveEXR(rgba.data(), static_cast<int>(image.width), static_cast<int>(image.height), 4, 1,
                                filename.c_str(), &err);
        if (ret != TINYEXR_SUCCESS) {
            std::string message = "TinyEXR SaveEXR failed";
            if (err) {
                message += ": ";
                message += err;
                FreeEXRErrorMessage(err);
            }
            throw std::runtime_error(message);
        }
    } else {
        SavePngRgba8(path, image);
    }
}
