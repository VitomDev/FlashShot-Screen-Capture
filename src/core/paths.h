#pragma once

#include <ctime>
#include <filesystem>
#include <string>

namespace flashshot {

std::filesystem::path ExecutablePath();

// The user's Pictures folder: where saving starts until another folder is used.
std::filesystem::path PicturesDirectory();

// "FlashShot 2026-11-11 19-30-05.png" for the given local time.
std::wstring CaptureFileName(const std::tm& time, const wchar_t* extension);
// A path in `folder` that does not exist yet: the name itself, else the name
// with " (2)", " (3)"... before the extension. Empty if none is free.
std::filesystem::path UniquePath(const std::filesystem::path& folder, const std::wstring& fileName);

}  // namespace flashshot
