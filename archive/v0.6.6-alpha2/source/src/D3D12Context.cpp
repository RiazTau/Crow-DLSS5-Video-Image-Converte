#include "D3D12Context.h"
#include <stdexcept>
#include <string>
#include <cstring>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace {
void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string(what) + " failed: HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
}

D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = type;
    p.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    p.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    p.CreationNodeMask = 1;
    p.VisibleNodeMask = 1;
    return p;
}
}

D3D12Context::D3D12Context() {
    _CreateDevice();
    _CreateQueueObjects();
}

D3D12Context::~D3D12Context() {
    if (_queue && _fence) {
        try { Flush(); } catch (...) {}
    }
    if (_fenceEvent) CloseHandle(_fenceEvent);
}

void D3D12Context::_CreateDevice() {
    UINT flags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
        debug->EnableDebugLayer();
        flags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif
    ThrowIfFailed(CreateDXGIFactory2(flags, IID_PPV_ARGS(&_factory)), "CreateDXGIFactory2");

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT hr = _factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        ThrowIfFailed(hr, "EnumAdapterByGpuPreference");
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (desc.VendorId != 0x10DE) continue;
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
            _adapter = adapter;
            break;
        }
    }
    if (!_adapter) throw std::runtime_error("No compatible NVIDIA D3D12 adapter found");
    ThrowIfFailed(D3D12CreateDevice(_adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_device)), "D3D12CreateDevice");
}

void D3D12Context::_CreateQueueObjects() {
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(_device->CreateCommandQueue(&q, IID_PPV_ARGS(&_queue)), "CreateCommandQueue");
    ThrowIfFailed(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_allocator)), "CreateCommandAllocator");
    ThrowIfFailed(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _allocator.Get(), nullptr, IID_PPV_ARGS(&_list)), "CreateCommandList");
    ThrowIfFailed(_list->Close(), "Close initial command list");
    ThrowIfFailed(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)), "CreateFence");
    _fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!_fenceEvent) throw std::runtime_error("CreateEventW failed");
}

void D3D12Context::ResetList() {
    ThrowIfFailed(_allocator->Reset(), "CommandAllocator Reset");
    ThrowIfFailed(_list->Reset(_allocator.Get(), nullptr), "CommandList Reset");
}

void D3D12Context::_WaitFor(uint64_t value) {
    if (_fence->GetCompletedValue() >= value) return;
    ThrowIfFailed(_fence->SetEventOnCompletion(value, _fenceEvent), "SetEventOnCompletion");
    WaitForSingleObject(_fenceEvent, INFINITE);
}

void D3D12Context::ExecuteAndWait() {
    ThrowIfFailed(_list->Close(), "CommandList Close");
    ID3D12CommandList* lists[] = { _list.Get() };
    _queue->ExecuteCommandLists(1, lists);
    Flush();
}

void D3D12Context::Flush() {
    const uint64_t value = ++_fenceValue;
    ThrowIfFailed(_queue->Signal(_fence.Get(), value), "Queue Signal");
    _WaitFor(value);
}

ComPtr<ID3D12Resource> D3D12Context::CreateTexture2D(
    DXGI_FORMAT format, uint32_t width, uint32_t height,
    D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Alignment = 0;
    d.Width = width;
    d.Height = height;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = format;
    d.SampleDesc.Count = 1;
    d.SampleDesc.Quality = 0;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = flags;
    auto heap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> resource;
    ThrowIfFailed(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
        initialState, nullptr, IID_PPV_ARGS(&resource)), "CreateCommittedResource(Texture2D)");
    return resource;
}

D3D12Context::TextureTransferBuffer D3D12Context::CreateUploadTransferBuffer(ID3D12Resource* texture) {
    if (!texture) throw std::runtime_error("CreateUploadTransferBuffer invalid texture");
    TextureTransferBuffer transfer;
    const auto desc = texture->GetDesc();
    _device->GetCopyableFootprints(&desc, 0, 1, 0, &transfer.footprint,
                                   &transfer.numRows, &transfer.rowSize, &transfer.totalBytes);

    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = transfer.totalBytes;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto heap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&transfer.resource)),
        "Create persistent upload transfer buffer");
    return transfer;
}

D3D12Context::TextureTransferBuffer D3D12Context::CreateReadbackTransferBuffer(ID3D12Resource* texture) {
    if (!texture) throw std::runtime_error("CreateReadbackTransferBuffer invalid texture");
    TextureTransferBuffer transfer;
    const auto desc = texture->GetDesc();
    _device->GetCopyableFootprints(&desc, 0, 1, 0, &transfer.footprint,
                                   &transfer.numRows, &transfer.rowSize, &transfer.totalBytes);

    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = transfer.totalBytes;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto heap = HeapProps(D3D12_HEAP_TYPE_READBACK);
    ThrowIfFailed(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&transfer.resource)),
        "Create persistent readback transfer buffer");
    return transfer;
}

void D3D12Context::WriteUploadTransferBuffer(const TextureTransferBuffer& transfer, const void* src,
                                              size_t srcRowBytes, uint32_t rows) {
    if (!transfer.resource || !src) throw std::runtime_error("WriteUploadTransferBuffer invalid argument");
    if (rows > transfer.numRows || srcRowBytes > transfer.footprint.Footprint.RowPitch) {
        throw std::runtime_error("WriteUploadTransferBuffer source layout mismatch");
    }
    uint8_t* mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    ThrowIfFailed(transfer.resource->Map(0, &noRead, reinterpret_cast<void**>(&mapped)),
                  "Map persistent upload transfer buffer");
    const auto* srcBytes = static_cast<const uint8_t*>(src);
    for (uint32_t y = 0; y < rows; ++y) {
        std::memcpy(mapped + transfer.footprint.Offset + static_cast<size_t>(y) * transfer.footprint.Footprint.RowPitch,
                    srcBytes + static_cast<size_t>(y) * srcRowBytes, srcRowBytes);
    }
    transfer.resource->Unmap(0, nullptr);
}

void D3D12Context::RecordUploadTexture2D(ID3D12Resource* texture, const TextureTransferBuffer& transfer,
                                         D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState) {
    if (!texture || !transfer.resource) throw std::runtime_error("RecordUploadTexture2D invalid argument");
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = transfer.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = transfer.footprint;
    Transition(_list.Get(), texture, beforeState, D3D12_RESOURCE_STATE_COPY_DEST);
    _list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(_list.Get(), texture, D3D12_RESOURCE_STATE_COPY_DEST, afterState);
}

void D3D12Context::RecordReadbackTexture2D(ID3D12Resource* texture, const TextureTransferBuffer& transfer,
                                           D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState) {
    if (!texture || !transfer.resource) throw std::runtime_error("RecordReadbackTexture2D invalid argument");
    Transition(_list.Get(), texture, beforeState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = transfer.resource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = transfer.footprint;
    _list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(_list.Get(), texture, D3D12_RESOURCE_STATE_COPY_SOURCE, afterState);
}

std::vector<uint8_t> D3D12Context::ReadTransferBuffer(const TextureTransferBuffer& transfer,
                                                       ID3D12Resource* texture, uint32_t bytesPerPixel) {
    if (!transfer.resource || !texture || !bytesPerPixel) {
        throw std::runtime_error("ReadTransferBuffer invalid argument");
    }
    const auto desc = texture->GetDesc();
    const size_t tightRow = static_cast<size_t>(desc.Width) * bytesPerPixel;
    if (tightRow > transfer.footprint.Footprint.RowPitch || desc.Height > transfer.numRows) {
        throw std::runtime_error("ReadTransferBuffer destination layout mismatch");
    }
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{0, static_cast<SIZE_T>(transfer.totalBytes)};
    ThrowIfFailed(transfer.resource->Map(0, &readRange, reinterpret_cast<void**>(&mapped)),
                  "Map persistent readback transfer buffer");
    std::vector<uint8_t> out(tightRow * desc.Height);
    for (uint32_t y = 0; y < desc.Height; ++y) {
        std::memcpy(out.data() + static_cast<size_t>(y) * tightRow,
                    mapped + transfer.footprint.Offset + static_cast<size_t>(y) * transfer.footprint.Footprint.RowPitch,
                    tightRow);
    }
    D3D12_RANGE noWrite{0, 0};
    transfer.resource->Unmap(0, &noWrite);
    return out;
}

void D3D12Context::Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
    D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState) {
    if (beforeState == afterState) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = beforeState;
    b.Transition.StateAfter = afterState;
    list->ResourceBarrier(1, &b);
}

void D3D12Context::UploadTexture2D(ID3D12Resource* texture, const void* src,
    size_t srcRowBytes, uint32_t rows, D3D12_RESOURCE_STATES finalState,
    D3D12_RESOURCE_STATES beforeState) {
    if (!texture || !src) throw std::runtime_error("UploadTexture2D invalid argument");
    const auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    _device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSize, &totalBytes);
    if (rows > numRows || srcRowBytes > footprint.Footprint.RowPitch) throw std::runtime_error("UploadTexture2D source layout mismatch");

    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = totalBytes;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto heap = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
    ComPtr<ID3D12Resource> upload;
    ThrowIfFailed(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload)), "Create upload buffer");

    uint8_t* mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    ThrowIfFailed(upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped)), "Map upload buffer");
    const auto* srcBytes = static_cast<const uint8_t*>(src);
    for (uint32_t y = 0; y < rows; ++y) {
        std::memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
            srcBytes + static_cast<size_t>(y) * srcRowBytes, srcRowBytes);
    }
    upload->Unmap(0, nullptr);

    ResetList();
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = texture;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    Transition(_list.Get(), texture, beforeState, D3D12_RESOURCE_STATE_COPY_DEST);
    _list->CopyTextureRegion(&dst, 0, 0, 0, &source, nullptr);
    Transition(_list.Get(), texture, D3D12_RESOURCE_STATE_COPY_DEST, finalState);
    ExecuteAndWait();
}

std::vector<uint8_t> D3D12Context::ReadbackTexture2D(ID3D12Resource* texture,
    uint32_t bytesPerPixel, D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState) {
    if (!texture || !bytesPerPixel) throw std::runtime_error("ReadbackTexture2D invalid argument");
    const auto desc = texture->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    _device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSize, &totalBytes);

    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = totalBytes;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    auto heap = HeapProps(D3D12_HEAP_TYPE_READBACK);
    ComPtr<ID3D12Resource> readback;
    ThrowIfFailed(_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "Create readback buffer");

    ResetList();
    Transition(_list.Get(), texture, beforeState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;
    _list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(_list.Get(), texture, D3D12_RESOURCE_STATE_COPY_SOURCE, afterState);
    ExecuteAndWait();

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{0, static_cast<SIZE_T>(totalBytes)};
    ThrowIfFailed(readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Map readback buffer");
    const size_t tightRow = static_cast<size_t>(desc.Width) * bytesPerPixel;
    std::vector<uint8_t> out(tightRow * desc.Height);
    for (uint32_t y = 0; y < desc.Height; ++y) {
        std::memcpy(out.data() + static_cast<size_t>(y) * tightRow,
            mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
            tightRow);
    }
    D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);
    return out;
}
