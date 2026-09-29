#include "GuiApp.h"
#include <Windows.h>
#include <objbase.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const int result = RunGuiApp(instance, showCommand);
    if (SUCCEEDED(co)) CoUninitialize();
    return result;
}
