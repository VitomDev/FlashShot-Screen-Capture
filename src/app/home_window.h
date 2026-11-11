#pragma once

#include "app/theme.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace flashshot {

// The window shown when FlashShot is opened (Start menu, Vitool Box, a second
// launch): a New capture button, Settings, and the state of the shortcut.
// Closing it only hides it; FlashShot keeps running in the notification area.
class HomeWindow {
public:
    bool Create(HINSTANCE instance);
    void Destroy();
    // `shortcut` is the capture shortcut; `problem` explains why it does not work.
    void Show(const std::wstring& shortcut, const std::wstring& problem);
    void Hide();
    bool IsVisible() const { return window_ && IsWindowVisible(window_); }
    HWND window() const { return window_; }

    std::function<void()> onNewCapture;
    std::function<void()> onSettings;

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    int Scale(int value) const { return MulDiv(value, static_cast<int>(GetDpiForWindow(window_)), 96); }
    void RefreshStyle();
    void Layout();
    void Paint();

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    std::wstring shortcut_;
    std::wstring problem_;
    Theme theme_{};
    HBRUSH background_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT textFont_ = nullptr;
    HFONT buttonFont_ = nullptr;
    HFONT iconFont_ = nullptr;
    HICON logo_ = nullptr;
    RECT logoRect_{}, titleRect_{}, subtitleRect_{}, introRect_{}, shortcutRect_{}, footerRect_{};
};

}  // namespace flashshot
