#pragma once

#include "core/hotkey.h"
#include "core/session.h"

#include <windows.h>

#include <string>

namespace flashshot {

// Registry locations. The installer and the tests rely on these names.
inline constexpr wchar_t kSettingsKey[] = L"Software\\FlashShot Screen Capture";
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kRunValue[] = L"FlashShot Screen Capture";
inline constexpr wchar_t kStartupApprovedKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";

struct Settings {
    Hotkey captureHotkey{0, VK_SNAPSHOT};
    std::wstring saveFolder;  // Last folder saved to; empty means Pictures
    ToolStyles styles = DefaultToolStyles();
    Tool shapeTool = Tool::Rectangle;  // Last choice inside the Shapes group
    Tool drawTool = Tool::Pen;         // and inside the Draw group

    // `keyPath` is relative to HKEY_CURRENT_USER; tests use a scratch key.
    static Settings Load(const wchar_t* keyPath = kSettingsKey);
    bool Save(const wchar_t* keyPath = kSettingsKey) const;
};

// True only the first time it is called for a settings key, so FlashShot can
// greet the user on its very first launch. Uninstalling removes the key.
bool ConsumeFirstRun(const wchar_t* keyPath = kSettingsKey);

// "Start with Windows" is the presence of FlashShot's value in the per-user
// Run key, so the menu always shows the real state.
bool IsAutostartEnabled(const wchar_t* valueName = kRunValue);
bool SetAutostartEnabled(bool enabled, const std::wstring& exePath, const wchar_t* valueName = kRunValue);
std::wstring AutostartCommand(const std::wstring& exePath);

// Windows 11 can open the Snipping Tool with Print Screen, which keeps the
// key from any other app.
bool IsPrintScreenTakenBySnipping();

}  // namespace flashshot
