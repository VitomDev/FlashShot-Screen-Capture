#pragma once

#include "core/image.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace flashshot {

enum class ImageFormat { Png, Jpeg };

// Encoding through the Windows Imaging Component, built into Windows. Needs
// COM initialized on the calling thread. Empty on failure.
std::vector<uint8_t> EncodeImage(const BgraImage& image, ImageFormat format);
inline std::vector<uint8_t> EncodePng(const BgraImage& image) { return EncodeImage(image, ImageFormat::Png); }

// Writes the file through a temporary one, so a failed save never leaves a
// broken image behind or damages the file being replaced. Returns a
// Windows error code (ERROR_SUCCESS when saved).
DWORD SaveImage(const BgraImage& image, const std::filesystem::path& path, ImageFormat format);

// ".jpg" and ".jpeg" are JPEG, anything else PNG.
ImageFormat FormatForPath(const std::filesystem::path& path);

}  // namespace flashshot
