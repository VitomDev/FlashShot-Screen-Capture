#pragma once

#include "core/image.h"

#include <windows.h>

namespace flashshot {

// Puts an image on the clipboard both as a bitmap (CF_DIB) and as PNG, which
// keeps it sharp in apps that understand it (browsers, Office, chat apps).
// Another application may hold the clipboard open for a moment, so opening
// is retried briefly; false when it stays busy.
bool SetClipboardImage(HWND owner, const BgraImage& image);

}  // namespace flashshot
