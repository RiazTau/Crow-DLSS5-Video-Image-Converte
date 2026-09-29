#pragma once
#include "D3D12Context.h"
#include <filesystem>
#include <vector>

// Shared NGX core lifetime used only when multiple NGX features are active in the
// same process. Individual feature runners can opt out of owning Core Init/Shutdown.
class NgxCoreSession {
public:
    NgxCoreSession(D3D12Context& d3d, std::vector<std::filesystem::path> searchPaths);
    ~NgxCoreSession();
    NgxCoreSession(const NgxCoreSession&) = delete;
    NgxCoreSession& operator=(const NgxCoreSession&) = delete;
private:
    D3D12Context& _d3d;
    bool _initialized = false;
};
