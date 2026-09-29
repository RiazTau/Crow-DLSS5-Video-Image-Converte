#include "ExternalSequence.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

int main() {
    using video::ParseExrSequencePattern;
    auto a = ParseExrSequencePattern(std::filesystem::path(L"C:/render/shot_1001.exr"));
    if (a.firstNumber != 1001 || a.padding != 4) return 1;
    if (a.FramePath(0).filename().wstring() != L"shot_1001.exr") return 2;
    if (a.FramePath(1).filename().wstring() != L"shot_1002.exr") return 3;
    if (a.FramePath(25).filename().wstring() != L"shot_1026.exr") return 4;

    auto b = ParseExrSequencePattern(std::filesystem::path(L"render.000001.EXR"));
    if (b.firstNumber != 1 || b.padding != 6) return 5;
    if (b.FramePath(9).filename().wstring() != L"render.000010.EXR") return 6;

    bool rejected = false;
    try { (void)ParseExrSequencePattern(std::filesystem::path(L"render.exr")); }
    catch (const std::exception&) { rejected = true; }
    if (!rejected) return 7;

    std::cout << "PASS: external EXR sequence numbering\n";
    return 0;
}
