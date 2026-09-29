#include "SnippetCallerCompat.h"
#include <cstring>
#include <cstddef>

HMODULE SnippetCallerCompat::s_callerModule = nullptr;
SnippetCallerCompat::GetModuleFileNameWFn SnippetCallerCompat::s_original = nullptr;
SnippetCallerCompat* SnippetCallerCompat::s_owner = nullptr;

SnippetCallerCompat::~SnippetCallerCompat() { Uninstall(); }

void* SnippetCallerCompat::FunctionPtr(GetModuleFileNameWFn fn) noexcept {
    void* p = nullptr;
    static_assert(sizeof(p) == sizeof(fn));
    std::memcpy(&p, &fn, sizeof(p));
    return p;
}

void** SnippetCallerCompat::FindIatSlot(HMODULE module, const char* functionName) noexcept {
    if (!module || !functionName) return nullptr;
    auto* base = reinterpret_cast<std::byte*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return nullptr;
    if (dir.VirtualAddress >= nt->OptionalHeader.SizeOfImage) return nullptr;

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        if (!desc->OriginalFirstThunk || !desc->FirstThunk) continue;
        const char* lib = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(lib, "KERNEL32.dll") != 0 &&
            _stricmp(lib, "api-ms-win-core-libraryloader-l1-2-0.dll") != 0 &&
            _stricmp(lib, "api-ms-win-core-libraryloader-l1-1-0.dll") != 0) continue;

        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk);
        auto* addrs = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addrs) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            auto* imp = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(imp->Name), functionName) == 0) {
                return reinterpret_cast<void**>(&addrs->u1.Function);
            }
        }
    }
    return nullptr;
}

DWORD WINAPI SnippetCallerCompat::HookGetModuleFileNameW(HMODULE module, LPWSTR filename, DWORD size) noexcept {
    if (module == s_callerModule) {
        constexpr wchar_t kAuthorizedName[] = L"nvngx.dll";
        constexpr DWORD kChars = static_cast<DWORD>((sizeof(kAuthorizedName) / sizeof(wchar_t)) - 1);
        if (!filename || size == 0) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        if (size <= kChars) {
            const DWORD toCopy = size > 0 ? size - 1 : 0;
            if (toCopy) std::memcpy(filename, kAuthorizedName, static_cast<size_t>(toCopy) * sizeof(wchar_t));
            filename[size - 1] = L'\0';
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return size;
        }
        std::memcpy(filename, kAuthorizedName, sizeof(kAuthorizedName));
        return kChars;
    }
    return s_original ? s_original(module, filename, size) : 0;
}

bool SnippetCallerCompat::Install(HMODULE snippetModule) {
    if (_slot) return true;
    if (s_owner && s_owner != this) return false;
    _slot = FindIatSlot(snippetModule, "GetModuleFileNameW");
    if (!_slot) return false;

    auto hookFn = &SnippetCallerCompat::HookGetModuleFileNameW;
    void* hookAddress = FunctionPtr(hookFn);
    HMODULE caller = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(hookAddress), &caller)) {
        _slot = nullptr;
        return false;
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(_slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        _slot = nullptr;
        return false;
    }
    void* original = InterlockedExchangePointer(reinterpret_cast<void* volatile*>(_slot), hookAddress);
    GetModuleFileNameWFn originalFn = nullptr;
    std::memcpy(&originalFn, &original, sizeof(originalFn));
    DWORD ignored = 0;
    VirtualProtect(_slot, sizeof(void*), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), _slot, sizeof(void*));
    if (!originalFn) {
        _slot = nullptr;
        return false;
    }

    s_original = originalFn;
    s_callerModule = caller;
    s_owner = this;
    return true;
}

void SnippetCallerCompat::Uninstall() noexcept {
    if (!_slot || s_owner != this || !s_original) return;
    DWORD oldProtect = 0;
    if (VirtualProtect(_slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(_slot), FunctionPtr(s_original));
        DWORD ignored = 0;
        VirtualProtect(_slot, sizeof(void*), oldProtect, &ignored);
        FlushInstructionCache(GetCurrentProcess(), _slot, sizeof(void*));
    }
    _slot = nullptr;
    s_original = nullptr;
    s_callerModule = nullptr;
    s_owner = nullptr;
}
