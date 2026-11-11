#pragma once

#include <windows.h>

#include <string>

namespace flashshot {

// A global shortcut in RegisterHotKey terms: MOD_* flags plus a virtual key.
struct Hotkey {
    UINT modifiers = 0;
    UINT key = 0;

    bool operator==(const Hotkey&) const = default;
    bool IsValid() const;
};

// Human-readable form, e.g. "Ctrl + Alt + S".
std::wstring ToString(const Hotkey& hotkey);

// Compact form for the settings store: modifiers in the high word.
DWORD Pack(const Hotkey& hotkey);
Hotkey Unpack(DWORD value);

}  // namespace flashshot
