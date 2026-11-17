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

// Print Screen on its own, FlashShot's default shortcut.
inline constexpr Hotkey kPrintScreen{0, VK_SNAPSHOT};

// What the Print Screen keyboard hook does with each Print Screen event.
// Windows 11 gives Print Screen to the Snipping Tool before any registered
// shortcut, so FlashShot takes the key in a low-level hook instead. Only the
// key on its own is taken: Alt+Print Screen, Win+Print Screen and the like
// stay with Windows. A press that was taken keeps its release too, so the
// other side never sees half a key press.
class PrintScreenFilter {
public:
    enum class Action { Pass, Swallow, Capture };
    // `leave`: this press is not FlashShot's, e.g. Ctrl, Alt, Shift or Win is down.
    Action OnKey(bool down, bool leave);

private:
    bool taken_ = false;
};

}  // namespace flashshot
