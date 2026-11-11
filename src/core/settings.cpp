#include "core/settings.h"

#include <algorithm>
#include <cmath>

namespace flashshot {

namespace {

bool ReadDword(HKEY key, const wchar_t* name, DWORD& value) {
    DWORD size = sizeof(value);
    return RegGetValueW(key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
}

bool WriteDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

std::wstring ReadString(HKEY key, const wchar_t* name) {
    wchar_t value[1024]{};
    DWORD size = sizeof(value);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return {};
    return value;
}

bool WriteString(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

}  // namespace

Settings Settings::Load(const wchar_t* keyPath) {
    Settings settings;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return settings;

    DWORD value = 0;
    if (ReadDword(key, L"CaptureHotkey", value) && Unpack(value).IsValid()) settings.captureHotkey = Unpack(value);
    settings.saveFolder = ReadString(key, L"SaveFolder");
    for (size_t i = 0; i < kToolCount; ++i) {
        ToolStyle& style = settings.styles[i];
        const std::wstring prefix = L"Style" + std::to_wstring(i);
        if (ReadDword(key, (prefix + L"Color").c_str(), value)) style.color = value & 0xFFFFFF;
        // Sizes are stored in tenths of a pixel; only offered sizes are accepted.
        const auto presets = SizePresets(static_cast<Tool>(i));
        if (ReadDword(key, (prefix + L"Size").c_str(), value) &&
            std::find(presets.begin(), presets.end(), static_cast<float>(value) / 10) != presets.end()) {
            style.size = static_cast<float>(value) / 10;
        }
        if (ReadDword(key, (prefix + L"Background").c_str(), value)) style.background = value != 0;
    }
    if (ReadDword(key, L"ShapeTool", value) && (value == static_cast<DWORD>(Tool::Line) || value == static_cast<DWORD>(Tool::Rectangle) ||
                                                value == static_cast<DWORD>(Tool::Ellipse))) {
        settings.shapeTool = static_cast<Tool>(value);
    }
    if (ReadDword(key, L"DrawTool", value) && (value == static_cast<DWORD>(Tool::Pen) || value == static_cast<DWORD>(Tool::Marker))) {
        settings.drawTool = static_cast<Tool>(value);
    }
    RegCloseKey(key);
    return settings;
}

bool Settings::Save(const wchar_t* keyPath) const {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    bool saved = WriteDword(key, L"CaptureHotkey", Pack(captureHotkey)) && WriteString(key, L"SaveFolder", saveFolder) &&
                 WriteDword(key, L"ShapeTool", static_cast<DWORD>(shapeTool)) &&
                 WriteDword(key, L"DrawTool", static_cast<DWORD>(drawTool));
    for (size_t i = 0; i < kToolCount && saved; ++i) {
        const std::wstring prefix = L"Style" + std::to_wstring(i);
        saved = WriteDword(key, (prefix + L"Color").c_str(), styles[i].color) &&
                WriteDword(key, (prefix + L"Size").c_str(), static_cast<DWORD>(std::lround(styles[i].size * 10))) &&
                WriteDword(key, (prefix + L"Background").c_str(), styles[i].background ? 1 : 0);
    }
    RegCloseKey(key);
    return saved;
}

bool ConsumeFirstRun(const wchar_t* keyPath) {
    DWORD shown = 0;
    DWORD size = sizeof(shown);
    if (RegGetValueW(HKEY_CURRENT_USER, keyPath, L"FirstRunDone", RRF_RT_REG_DWORD, nullptr, &shown, &size) == ERROR_SUCCESS &&
        shown) {
        return false;
    }
    shown = 1;
    RegSetKeyValueW(HKEY_CURRENT_USER, keyPath, L"FirstRunDone", REG_DWORD, &shown, sizeof(shown));
    return true;
}

std::wstring AutostartCommand(const std::wstring& exePath) { return L"\"" + exePath + L"\" --background"; }

bool IsAutostartEnabled(const wchar_t* valueName) {
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, valueName, RRF_RT_REG_SZ, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    // Task Manager's "Disable" keeps the Run value and flags it here instead:
    // an odd first byte means disabled.
    BYTE state[12]{};
    DWORD size = sizeof(state);
    if (RegGetValueW(HKEY_CURRENT_USER, kStartupApprovedKey, valueName, RRF_RT_REG_BINARY, nullptr, state, &size) ==
            ERROR_SUCCESS &&
        size > 0) {
        return (state[0] & 1) == 0;
    }
    return true;
}

bool SetAutostartEnabled(bool enabled, const std::wstring& exePath, const wchar_t* valueName) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    LSTATUS status;
    if (enabled) {
        // Setting one named value can never create a duplicate entry.
        const std::wstring command = AutostartCommand(exePath);
        status = RegSetValueExW(key, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, valueName);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    // Drop any Task Manager override so the Run value alone decides.
    RegDeleteKeyValueW(HKEY_CURRENT_USER, kStartupApprovedKey, valueName);
    return status == ERROR_SUCCESS;
}

bool IsPrintScreenTakenBySnipping() {
    DWORD enabled = 0;
    DWORD size = sizeof(enabled);
    return RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Keyboard", L"PrintScreenKeyForSnippingEnabled", RRF_RT_REG_DWORD,
                        nullptr, &enabled, &size) == ERROR_SUCCESS &&
           enabled != 0;
}

}  // namespace flashshot
