#include "FgGuiApp.h"
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    return fg::RunFgGui(instance, showCommand);
}
