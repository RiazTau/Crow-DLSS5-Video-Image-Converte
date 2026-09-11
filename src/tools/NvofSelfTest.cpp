#include "D3D12Context.h"
#include "video/NvofFlowSession.h"
#include "video/NvofRuntimeProbe.h"
#include <Windows.h>
#include <dxgi1_6.h>
#include <iostream>
#include <string>

#ifndef DLSS5_HAS_NVOF_SDK
#define DLSS5_HAS_NVOF_SDK 0
#endif

int wmain() {
    std::wcout << L"Crow-DLSS5-Video-Image-Converter NVOF Self-Test V0.6.6-alpha2\n";
    std::wcout << L"=====================================\n";
    try {
        D3D12Context d3d;
        DXGI_ADAPTER_DESC1 desc{};
        if (d3d.Adapter() && SUCCEEDED(d3d.Adapter()->GetDesc1(&desc))) {
            std::wcout << L"D3D12 adapter : " << desc.Description << L"\n";
        } else {
            std::wcout << L"D3D12 adapter : <unknown>\n";
        }
        const auto runtime = video::ProbeNvofRuntime();
        std::wcout << L"Driver module  : " << (runtime.moduleLoaded ? L"FOUND" : L"MISSING") << L"\n";
        std::wcout << L"D3D12 export   : " << (runtime.d3d12EntryPoint ? L"FOUND" : L"MISSING") << L"\n";
        std::wcout << L"Version export : " << (runtime.maxVersionEntryPoint ? L"FOUND" : L"MISSING") << L"\n";
        std::wcout << L"SDK headers    : " << (DLSS5_HAS_NVOF_SDK ? L"DETECTED AT BUILD" : L"NOT SUPPLIED") << L"\n";
        std::wcout << L"Native bridge  : " << (video::NvofFlowSession::NativeBackendCompiled() ? L"READY" : L"NOT COMPILED") << L"\n";
        std::wcout << L"Status         : " << runtime.message << L"\n";
        std::wcout << L"Build status   : " << video::NvofFlowSession::BuildStatusText() << L"\n\n";
        if (!runtime.moduleLoaded || !runtime.d3d12EntryPoint) return 2;
        if (!video::NvofFlowSession::NativeBackendCompiled()) return 3;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Self-test failed: " << e.what() << "\n";
        return 1;
    }
}
