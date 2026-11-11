#pragma once

#include "app/capture_overlay.h"
#include "app/home_window.h"
#include "app/toast.h"
#include "core/settings.h"

#include <windows.h>
#include <shellapi.h>

#include <memory>
#include <string>

namespace flashshot {

inline constexpr wchar_t kTrayWindowClass[] = L"FlashShotScreenCaptureTray";
inline constexpr UINT WM_APP_ALREADY_RUNNING = WM_APP + 12;
// Starts a capture; also used by the tests.
inline constexpr UINT WM_APP_NEW_CAPTURE = WM_APP + 20;

// The background application: tray icon, the global shortcut, the home and
// settings windows, and at most one capture session at a time.
class TrayApp {
public:
    bool Initialize(HINSTANCE instance, bool startedAtSignIn);
    // Lets the home window handle Tab and Enter. Returns true if consumed.
    bool PreTranslate(MSG& message);

private:
    enum class BalloonAction { None, OpenSettings };

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void AddTrayIcon();
    void ShowTrayMenu(POINT point);
    void Notify(const std::wstring& message, BalloonAction action = BalloonAction::None);
    void ShowHome();

    // Registers the capture shortcut; empty on success, else what went wrong.
    std::wstring RegisterShortcut(const Hotkey& hotkey);
    bool ApplySettings(const Settings& settings, bool autostart, std::wstring& error);
    void OpenSettings();
    void ToggleAutostart();
    void Exit();

    // `delay` lets a menu or window fade out before the screen is frozen.
    void RequestCapture(UINT delay);
    void BeginCapture();
    void FinishCapture();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    UINT taskbarCreatedMessage_ = 0;
    NOTIFYICONDATAW icon_{};
    Settings settings_;
    BalloonAction balloonAction_ = BalloonAction::None;
    std::wstring shortcutProblem_;
    bool settingsOpen_ = false;
    bool capturePending_ = false;
    bool exiting_ = false;

    std::unique_ptr<CaptureOverlay> overlay_;
    HWND previousForeground_ = nullptr;
    HomeWindow home_;
    Toast toast_;
};

}  // namespace flashshot
