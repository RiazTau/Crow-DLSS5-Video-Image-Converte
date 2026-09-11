#pragma once
#include <Windows.h>

class SnippetCallerCompat {
public:
    SnippetCallerCompat() = default;
    ~SnippetCallerCompat();
    SnippetCallerCompat(const SnippetCallerCompat&) = delete;
    SnippetCallerCompat& operator=(const SnippetCallerCompat&) = delete;

    bool Install(HMODULE snippetModule);
    void Uninstall() noexcept;
    bool Installed() const noexcept { return _slot != nullptr; }

private:
    using GetModuleFileNameWFn = DWORD(WINAPI*)(HMODULE, LPWSTR, DWORD);
    static DWORD WINAPI HookGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size) noexcept;
    static void** FindIatSlot(HMODULE module, const char* functionName) noexcept;
    static void* FunctionPtr(GetModuleFileNameWFn fn) noexcept;

    void** _slot = nullptr;
    static HMODULE s_callerModule;
    static GetModuleFileNameWFn s_original;
    static SnippetCallerCompat* s_owner;
};
