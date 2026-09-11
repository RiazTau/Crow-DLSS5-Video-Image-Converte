#include "ImageWic.h"
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string(what) + " failed: HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
}

ComPtr<IWICImagingFactory> CreateFactory() {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "CoCreateInstance(WIC)");
    }
    return factory;
}
}

Rgba8Image LoadImageRgba8(const std::filesystem::path& path) {
    auto factory = CreateFactory();
    ComPtr<IWICBitmapDecoder> decoder;
    ThrowIfFailed(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
        WICDecodeMetadataCacheOnDemand, &decoder), "CreateDecoderFromFilename");
    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame), "GetFrame");
    UINT width = 0, height = 0;
    ThrowIfFailed(frame->GetSize(&width, &height), "GetSize");
    if (!width || !height) throw std::runtime_error("Input image has zero size");

    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(factory->CreateFormatConverter(&converter), "CreateFormatConverter");
    ThrowIfFailed(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom), "WIC format conversion");

    Rgba8Image out;
    out.width = width;
    out.height = height;
    out.pixels.resize(static_cast<size_t>(width) * height * 4u);
    const UINT stride = width * 4u;
    ThrowIfFailed(converter->CopyPixels(nullptr, stride, static_cast<UINT>(out.pixels.size()), out.pixels.data()), "CopyPixels");
    return out;
}

void SavePngRgba8(const std::filesystem::path& path, const Rgba8Image& image) {
    if (!image.width || !image.height || image.pixels.size() != static_cast<size_t>(image.width) * image.height * 4u) {
        throw std::runtime_error("Invalid RGBA8 image passed to SavePngRgba8");
    }
    auto factory = CreateFactory();
    ComPtr<IWICStream> stream;
    ThrowIfFailed(factory->CreateStream(&stream), "CreateStream");
    ThrowIfFailed(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "InitializeFromFilename");
    ComPtr<IWICBitmapEncoder> encoder;
    ThrowIfFailed(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "CreateEncoder(PNG)");
    ThrowIfFailed(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Encoder Initialize");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    ThrowIfFailed(encoder->CreateNewFrame(&frame, &props), "CreateNewFrame");
    ThrowIfFailed(frame->Initialize(props.Get()), "Frame Initialize");
    ThrowIfFailed(frame->SetSize(image.width, image.height), "Frame SetSize");
    // The Windows PNG encoder natively accepts 32bpp BGRA, not 32bpp RGBA.
    // Keep the processing pipeline in RGBA and swizzle only at file output.
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    ThrowIfFailed(frame->SetPixelFormat(&fmt), "SetPixelFormat");
    if (!IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA)) throw std::runtime_error("PNG encoder rejected BGRA8 format");
    std::vector<uint8_t> bgra(image.pixels.size());
    for (size_t i = 0; i < image.pixels.size(); i += 4) {
        bgra[i + 0] = image.pixels[i + 2];
        bgra[i + 1] = image.pixels[i + 1];
        bgra[i + 2] = image.pixels[i + 0];
        bgra[i + 3] = image.pixels[i + 3];
    }
    const UINT stride = image.width * 4u;
    ThrowIfFailed(frame->WritePixels(image.height, stride, static_cast<UINT>(bgra.size()), bgra.data()), "WritePixels");
    ThrowIfFailed(frame->Commit(), "Frame Commit");
    ThrowIfFailed(encoder->Commit(), "Encoder Commit");
}
