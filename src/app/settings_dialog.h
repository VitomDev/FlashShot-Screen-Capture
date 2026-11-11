#pragma once

#include "core/settings.h"

#include <windows.h>

#include <functional>
#include <string>

namespace flashshot {

// Applies confirmed settings (registers the shortcut, saves, sets autostart).
// Returns false with a message when they cannot be used, e.g. the shortcut
// is taken by another application.
using ApplySettings = std::function<bool(const Settings& settings, bool autostart, std::wstring& error)>;

// The small Settings window. Modal; a second request focuses the open one.
void ShowSettingsDialog(HINSTANCE instance, HWND owner, const Settings& current, const ApplySettings& apply);

}  // namespace flashshot
