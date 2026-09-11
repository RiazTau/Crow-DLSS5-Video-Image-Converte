#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstddef>
#include <vector>

class D3D12Context {
public:
    struct TextureTransferBuffer {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT numRows = 0;
        UINT64 rowSize = 0;
        UINT64 totalBytes = 0;
    };

    D3D12Context();
    ~D3D12Context();
    D3D12Context(const D3D12Context&) = delete;
    D3D12Context& operator=(const D3D12Context&) = delete;

    ID3D12Device* Device() const noexcept { return _device.Get(); }
    ID3D12CommandQueue* Queue() const noexcept { return _queue.Get(); }
    ID3D12GraphicsCommandList* List() const noexcept { return _list.Get(); }
    IDXGIAdapter1* Adapter() const noexcept { return _adapter.Get(); }

    void ResetList();
    void ExecuteAndWait();
    void Flush();

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTexture2D(
        DXGI_FORMAT format, uint32_t width, uint32_t height,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    void UploadTexture2D(
        ID3D12Resource* texture,
        const void* src,
        size_t srcRowBytes,
        uint32_t rows,
        D3D12_RESOURCE_STATES finalState = D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_COPY_DEST);

    std::vector<uint8_t> ReadbackTexture2D(
        ID3D12Resource* texture,
        uint32_t bytesPerPixel,
        D3D12_RESOURCE_STATES beforeState = D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATES afterState = D3D12_RESOURCE_STATE_COMMON);

    // V0.6.3 performance path: persistent staging resources + one command-list submission per frame.
    TextureTransferBuffer CreateUploadTransferBuffer(ID3D12Resource* texture);
    TextureTransferBuffer CreateReadbackTransferBuffer(ID3D12Resource* texture);
    void WriteUploadTransferBuffer(const TextureTransferBuffer& transfer, const void* src,
                                   size_t srcRowBytes, uint32_t rows);
    void RecordUploadTexture2D(ID3D12Resource* texture, const TextureTransferBuffer& transfer,
                               D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);
    void RecordReadbackTexture2D(ID3D12Resource* texture, const TextureTransferBuffer& transfer,
                                 D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);
    std::vector<uint8_t> ReadTransferBuffer(const TextureTransferBuffer& transfer,
                                            ID3D12Resource* texture, uint32_t bytesPerPixel);

    static void Transition(
        ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
        D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);

private:
    void _CreateDevice();
    void _CreateQueueObjects();
    void _WaitFor(uint64_t value);

    Microsoft::WRL::ComPtr<IDXGIFactory6> _factory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> _adapter;
    Microsoft::WRL::ComPtr<ID3D12Device> _device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> _queue;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> _allocator;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> _list;
    Microsoft::WRL::ComPtr<ID3D12Fence> _fence;
    HANDLE _fenceEvent = nullptr;
    uint64_t _fenceValue = 0;
};
