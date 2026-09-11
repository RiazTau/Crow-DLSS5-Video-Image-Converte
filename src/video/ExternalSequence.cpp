#include "ExternalSequence.h"
#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace video {

ExrSequencePattern ParseExrSequencePattern(const std::filesystem::path& firstFrame) {
    if (firstFrame.empty()) throw std::runtime_error("External EXR sequence first-frame path is empty");
    std::wstring ext = firstFrame.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    if (ext != L".exr") throw std::runtime_error("External render-data sequences currently require .exr files");

    const std::wstring stem = firstFrame.stem().wstring();
    if (stem.empty()) throw std::runtime_error("External EXR sequence has an empty filename stem");
    size_t begin = stem.size();
    while (begin > 0 && std::iswdigit(stem[begin - 1])) --begin;
    if (begin == stem.size()) {
        throw std::runtime_error("External EXR sequence filename must end in a frame number, e.g. shot_0001.exr");
    }

    const std::wstring digits = stem.substr(begin);
    uint64_t start = 0;
    try {
        start = std::stoull(digits);
    } catch (...) {
        throw std::runtime_error("External EXR sequence frame number is invalid");
    }

    ExrSequencePattern out;
    out.directory = firstFrame.parent_path();
    out.prefix = stem.substr(0, begin);
    out.suffix = firstFrame.extension().wstring();
    out.firstNumber = start;
    out.padding = static_cast<unsigned>(digits.size());
    return out;
}

std::filesystem::path ExrSequencePattern::FramePath(uint64_t zeroBasedFrame) const {
    std::wostringstream name;
    name << prefix << std::setfill(L'0') << std::setw(static_cast<int>(padding))
         << (firstNumber + zeroBasedFrame) << suffix;
    return directory / name.str();
}

std::wstring ExrSequencePattern::Description() const {
    std::wostringstream s;
    s << prefix << L"[" << std::setfill(L'0') << std::setw(static_cast<int>(padding)) << firstNumber
      << L"...]" << suffix;
    return s.str();
}

} // namespace video
