#include "core/paths.h"

#include <windows.h>
#include <shlobj.h>

#include <cwchar>

namespace flashshot {

std::filesystem::path ExecutablePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

std::filesystem::path PicturesDirectory() {
    PWSTR folder = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &folder))) path = folder;
    CoTaskMemFree(folder);
    return path;
}

std::wstring CaptureFileName(const std::tm& time, const wchar_t* extension) {
    wchar_t name[64];
    swprintf_s(name, L"FlashShot %04d-%02d-%02d %02d-%02d-%02d%s", time.tm_year + 1900, time.tm_mon + 1, time.tm_mday,
               time.tm_hour, time.tm_min, time.tm_sec, extension);
    return name;
}

std::filesystem::path UniquePath(const std::filesystem::path& folder, const std::wstring& fileName) {
    const std::filesystem::path name(fileName);
    std::filesystem::path candidate = folder / name;
    std::error_code error;
    for (int copy = 2; std::filesystem::exists(candidate, error) || error; ++copy) {
        if (error || copy > 999) return {};
        candidate = folder / (name.stem().wstring() + L" (" + std::to_wstring(copy) + L")" + name.extension().wstring());
    }
    return candidate;
}

}  // namespace flashshot
