#include "app/tray_app.h"
#include "app/vitool_tray.h"

#include "app/resource.h"
#include "app/screen_capture.h"
#include "app/settings_dialog.h"
#include "app/theme.h"
#include "core/paths.h"

#include <commctrl.h>
#include <windowsx.h>

namespace flashshot {

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 10;
constexpr int kCaptureHotkeyId = 1;
constexpr UINT_PTR kCaptureTimer = 1;
constexpr UINT kAfterWindowDelay = 250;  // Long enough for menus and windows to fade out

enum MenuId { kMenuCapture = 1, kMenuOpen, kMenuSettings, kMenuAutostart, kMenuExit };

// Gives the memory of a capture back to the system once FlashShot is idle again.
void TrimWorkingSet() { SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)); }

}  // namespace

bool TrayApp::Initialize(HINSTANCE instance, bool startedAtSignIn) {
    instance_ = instance;
    settings_ = Settings::Load();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kTrayWindowClass;
    if (!RegisterClassExW(&wc)) return false;
    // A hidden top-level window rather than a message-only one: it must
    // receive the TaskbarCreated broadcast, and the installer finds it by class.
    window_ = CreateWindowExW(0, kTrayWindowClass, L"FlashShot Screen Capture", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!window_) return false;

    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
    AddTrayIcon();
    EnableDarkMenus();
    if (!toast_.Create(instance) || !home_.Create(instance)) return false;
    home_.onNewCapture = [this] { RequestCapture(kAfterWindowDelay); };
    home_.onSettings = [this] { OpenSettings(); };

    shortcutProblem_ = RegisterShortcut(settings_.captureHotkey);
    if (!shortcutProblem_.empty()) Notify(shortcutProblem_, BalloonAction::OpenSettings);
    ConsumeFirstRun();
    if (!startedAtSignIn) ShowHome();
    return true;
}

bool TrayApp::PreTranslate(MSG& message) {
    HWND home = home_.window();
    if (home && IsWindowVisible(home) && (message.hwnd == home || IsChild(home, message.hwnd))) {
        return IsDialogMessageW(home, &message) != FALSE;
    }
    return false;
}

// --- Tray icon ---------------------------------------------------------------

void TrayApp::AddTrayIcon() {
    icon_ = {sizeof(icon_)};
    icon_.hWnd = window_;
    icon_.uID = 1;
    icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    icon_.uCallbackMessage = WM_APP_TRAY;
    LoadIconMetric(instance_, MAKEINTRESOURCEW(IDI_FLASHSHOT), LIM_SMALL, &icon_.hIcon);
    wcscpy_s(icon_.szTip, L"FlashShot Screen Capture");
    vitool_tray::NotifyIcon(NIM_ADD, &icon_);
    icon_.uVersion = NOTIFYICON_VERSION_4;
    vitool_tray::NotifyIcon(NIM_SETVERSION, &icon_);
}

void TrayApp::ShowTrayMenu(POINT point) {
    HMENU menu = CreatePopupMenu();
    const std::wstring capture = L"New capture\t" + ToString(settings_.captureHotkey);
    AppendMenuW(menu, MF_STRING | (overlay_ ? MF_GRAYED : 0), kMenuCapture, capture.c_str());
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Open FlashShot");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings");
    AppendMenuW(menu, MF_STRING | (IsAutostartEnabled() ? MF_CHECKED : 0), kMenuAutostart, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit FlashShot");
    SetMenuDefaultItem(menu, kMenuCapture, FALSE);

    // Required so the menu closes when clicking elsewhere.
    SetForegroundWindow(window_);
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, point.x, point.y, window_, nullptr);
    DestroyMenu(menu);
    switch (command) {
        case kMenuCapture: RequestCapture(kAfterWindowDelay); break;
        case kMenuOpen: ShowHome(); break;
        case kMenuSettings: OpenSettings(); break;
        case kMenuAutostart: ToggleAutostart(); break;
        case kMenuExit: Exit(); break;
    }
}

void TrayApp::Notify(const std::wstring& message, BalloonAction action) {
    balloonAction_ = action;
    NOTIFYICONDATAW data = icon_;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    wcscpy_s(data.szInfoTitle, L"FlashShot Screen Capture");
    wcsncpy_s(data.szInfo, message.c_str(), _TRUNCATE);
    vitool_tray::NotifyIcon(NIM_MODIFY, &data);
}

void TrayApp::ShowHome() {
    if (overlay_) return;
    home_.Show(ToString(settings_.captureHotkey), shortcutProblem_);
}

// --- Shortcut and settings ---------------------------------------------------

std::wstring TrayApp::RegisterShortcut(const Hotkey& hotkey) {
    UnregisterHotKey(window_, kCaptureHotkeyId);
    if (RegisterHotKey(window_, kCaptureHotkeyId, hotkey.modifiers | MOD_NOREPEAT, hotkey.key)) return {};
    if (hotkey == Hotkey{0, VK_SNAPSHOT} && IsPrintScreenTakenBySnipping()) {
        return L"Print Screen opens the Windows Snipping Tool. Turn that off in Windows Settings > Accessibility > Keyboard, "
               L"or choose another shortcut.";
    }
    return ToString(hotkey) + L" is already used by another app. Choose another shortcut.";
}

bool TrayApp::ApplySettings(const Settings& settings, bool autostart, std::wstring& error) {
    // The old shortcut was paused while Settings is open and comes back when
    // it closes, so a shortcut that fails here never loses the working one.
    error = RegisterShortcut(settings.captureHotkey);
    if (!error.empty()) {
        UnregisterHotKey(window_, kCaptureHotkeyId);
        return false;
    }
    settings_.captureHotkey = settings.captureHotkey;
    settings_.saveFolder = settings.saveFolder;
    if (!settings_.Save()) {
        error = L"Could not save the settings.";
        return false;
    }
    if (autostart != IsAutostartEnabled() && !SetAutostartEnabled(autostart, ExecutablePath().wstring())) {
        error = L"Could not change Start with Windows.";
        return false;
    }
    shortcutProblem_.clear();
    return true;
}

void TrayApp::OpenSettings() {
    if (overlay_ || settingsOpen_) return;
    settingsOpen_ = true;
    // Pause the shortcut so it can be recorded in Settings.
    UnregisterHotKey(window_, kCaptureHotkeyId);
    ShowSettingsDialog(instance_, nullptr, settings_,
                       [this](const Settings& settings, bool autostart, std::wstring& error) { return ApplySettings(settings, autostart, error); });
    settingsOpen_ = false;
    if (!exiting_) {
        shortcutProblem_ = RegisterShortcut(settings_.captureHotkey);
        if (home_.IsVisible()) ShowHome();
    }
}

void TrayApp::ToggleAutostart() {
    if (!SetAutostartEnabled(!IsAutostartEnabled(), ExecutablePath().wstring())) Notify(L"Could not change Start with Windows.");
}

void TrayApp::Exit() {
    exiting_ = true;
    KillTimer(window_, kCaptureTimer);
    overlay_.reset();
    toast_.Destroy();
    home_.Destroy();
    UnregisterHotKey(window_, kCaptureHotkeyId);
    vitool_tray::NotifyIcon(NIM_DELETE, &icon_);
    if (icon_.hIcon) DestroyIcon(icon_.hIcon);
    settings_.Save();
    DestroyWindow(window_);
}

// --- Capture -------------------------------------------------------------------

void TrayApp::RequestCapture(UINT delay) {
    // One capture at a time; repeated shortcuts while one is open are ignored.
    if (overlay_ || capturePending_ || settingsOpen_ || exiting_) return;
    if (home_.IsVisible()) {
        home_.Hide();
        delay = std::max(delay, kAfterWindowDelay);
    }
    if (delay == 0) {
        BeginCapture();
        return;
    }
    capturePending_ = true;
    SetTimer(window_, kCaptureTimer, delay, nullptr);
}

void TrayApp::BeginCapture() {
    capturePending_ = false;
    if (overlay_ || exiting_) return;
    previousForeground_ = GetForegroundWindow();
    auto snapshot = ScreenSnapshot::Capture();
    if (!snapshot) {
        Notify(L"Could not capture the screen.");
        return;
    }
    overlay_ = std::make_unique<CaptureOverlay>(instance_, window_, std::move(snapshot), settings_);
    if (!overlay_->Show()) {
        overlay_.reset();
        Notify(L"Could not start the capture.");
        TrimWorkingSet();
    }
}

void TrayApp::FinishCapture() {
    if (!overlay_) return;
    const CaptureResult result = overlay_->result();
    overlay_.reset();
    settings_.Save();
    // Give the focus back to the app that was in front before the capture.
    if (previousForeground_ && IsWindow(previousForeground_)) SetForegroundWindow(previousForeground_);
    previousForeground_ = nullptr;
    switch (result.kind) {
        case CaptureResult::Kind::Copied:
            toast_.Show(result.regionOnScreen, L"Copied to clipboard", Toast::Kind::Success);
            break;
        case CaptureResult::Kind::Saved:
            toast_.Show(result.regionOnScreen, L"Saved as " + result.file.filename().wstring(), Toast::Kind::Success);
            break;
        case CaptureResult::Kind::DisplayChanged:
            Notify(L"The displays changed, so the capture was closed. Start a new one whenever you like.");
            break;
        case CaptureResult::Kind::Canceled:
            break;
    }
    TrimWorkingSet();
}

// --- Window procedure ----------------------------------------------------------

LRESULT CALLBACK TrayApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* app = static_cast<TrayApp*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return app ? app->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT TrayApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (vitool_tray::HandleMessage(message, wParam)) return 0;
    if (message == taskbarCreatedMessage_ && taskbarCreatedMessage_ != 0) {
        AddTrayIcon();  // Explorer restarted.
        return 0;
    }
    switch (message) {
        case WM_HOTKEY:
            if (wParam == kCaptureHotkeyId) RequestCapture(0);
            return 0;
        case WM_APP_NEW_CAPTURE:
            RequestCapture(0);
            return 0;
        case WM_TIMER:
            if (wParam == kCaptureTimer) {
                KillTimer(window_, kCaptureTimer);
                BeginCapture();
            }
            return 0;
        case WM_APP_CAPTURE_CLOSED:
            FinishCapture();
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lParam)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    RequestCapture(0);
                    break;
                case WM_CONTEXTMENU:
                    ShowTrayMenu({GET_X_LPARAM(wParam), GET_Y_LPARAM(wParam)});
                    break;
                case NIN_BALLOONUSERCLICK:
                    if (balloonAction_ == BalloonAction::OpenSettings) OpenSettings();
                    break;
            }
            return 0;
        case WM_APP_ALREADY_RUNNING:
            ShowHome();
            return 0;
        case WM_CLOSE:  // Sent by the installer before upgrading or uninstalling.
        case WM_ENDSESSION:
            if (message == WM_CLOSE || wParam) Exit();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace flashshot
