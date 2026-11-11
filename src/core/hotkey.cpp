#include "core/hotkey.h"

namespace flashshot {

namespace {

constexpr UINT kSupportedModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;

std::wstring KeyName(UINT key) {
    if ((key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z')) return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
    // GetKeyNameText has no usable name for it on every keyboard layout.
    if (key == VK_SNAPSHOT) return L"Print Screen";

    // Extended keys need the extended bit or GetKeyNameText names the numpad twin.
    UINT scanCode = MapVirtualKeyW(key, MAPVK_VK_TO_VSC);
    switch (key) {
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE:
            scanCode |= 0x100;
            break;
    }
    wchar_t name[64]{};
    if (scanCode && GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name, 64) > 0) return name;
    return L"Key " + std::to_wstring(key);
}

}  // namespace

bool Hotkey::IsValid() const {
    // A shortcut without modifiers would steal a key from every application;
    // Print Screen is the one key meant for exactly this.
    if (key == 0 || (modifiers & ~kSupportedModifiers & ~MOD_NOREPEAT) != 0) return false;
    return (modifiers & kSupportedModifiers) != 0 || key == VK_SNAPSHOT;
}

std::wstring ToString(const Hotkey& hotkey) {
    std::wstring text;
    if (hotkey.modifiers & MOD_CONTROL) text += L"Ctrl + ";
    if (hotkey.modifiers & MOD_ALT) text += L"Alt + ";
    if (hotkey.modifiers & MOD_SHIFT) text += L"Shift + ";
    if (hotkey.modifiers & MOD_WIN) text += L"Win + ";
    return text + KeyName(hotkey.key);
}

DWORD Pack(const Hotkey& hotkey) { return MAKELONG(hotkey.key, hotkey.modifiers); }

Hotkey Unpack(DWORD value) { return {HIWORD(value), LOWORD(value)}; }

}  // namespace flashshot
