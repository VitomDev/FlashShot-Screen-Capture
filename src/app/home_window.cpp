#include "app/home_window.h"

#include "app/resource.h"

#include <commctrl.h>

#include <algorithm>

namespace flashshot {

namespace {

constexpr wchar_t kClassName[] = L"FlashShotHome";
constexpr int kNewCapture = 100;
constexpr int kSettings = 101;

// Layout in 96-DPI pixels.
constexpr int kWidth = 400;
constexpr int kPadding = 24;
constexpr int kLogo = 48;
constexpr int kButtonHeight = 40;
constexpr int kRadius = 4;

}  // namespace

bool HomeWindow::Create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_FLASHSHOT));
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    window_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_APPWINDOW, kClassName, L"FlashShot Screen Capture",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                              100, 100, nullptr, nullptr, instance, this);
    if (!window_) return false;
    for (int id : {kNewCapture, kSettings}) {
        HWND button = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 0, 0, window_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        EnableHover(button);
    }
    RefreshStyle();
    Layout();
    return true;
}

void HomeWindow::Destroy() {
    if (window_) DestroyWindow(window_);
    window_ = nullptr;
    for (HGDIOBJ object : {static_cast<HGDIOBJ>(background_), static_cast<HGDIOBJ>(titleFont_), static_cast<HGDIOBJ>(textFont_),
                           static_cast<HGDIOBJ>(buttonFont_), static_cast<HGDIOBJ>(iconFont_)}) {
        if (object) DeleteObject(object);
    }
    if (logo_) DestroyIcon(logo_);
    background_ = nullptr;
    titleFont_ = textFont_ = buttonFont_ = iconFont_ = nullptr;
    logo_ = nullptr;
}

void HomeWindow::Show(const std::wstring& shortcut, const std::wstring& problem) {
    shortcut_ = shortcut;
    problem_ = problem;
    if (!IsWindowVisible(window_)) {
        // Open centered on the monitor with the mouse.
        POINT cursor;
        GetCursorPos(&cursor);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowPos(window_, nullptr, monitor.rcWork.left, monitor.rcWork.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        RefreshStyle();
        Layout();
        RECT bounds;
        GetWindowRect(window_, &bounds);
        const RECT& work = monitor.rcWork;
        SetWindowPos(window_, HWND_TOP, work.left + (work.right - work.left - (bounds.right - bounds.left)) / 2,
                     std::max<int>(work.top, work.top + (work.bottom - work.top - (bounds.bottom - bounds.top)) / 2), 0, 0, SWP_NOSIZE);
        SendMessageW(window_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
    } else {
        Layout();
    }
    ShowWindow(window_, IsIconic(window_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(GetDlgItem(window_, kNewCapture));
}

void HomeWindow::Hide() {
    if (window_) ShowWindow(window_, SW_HIDE);
}

void HomeWindow::RefreshStyle() {
    theme_ = Theme::Current();
    if (background_) DeleteObject(background_);
    background_ = CreateSolidBrush(theme_.background);
    for (HFONT font : {titleFont_, textFont_, buttonFont_, iconFont_}) {
        if (font) DeleteObject(font);
    }
    const UINT dpi = GetDpiForWindow(window_);
    titleFont_ = CreateUiFont(dpi, 16, FW_SEMIBOLD);
    textFont_ = CreateUiFont(dpi, 10);
    buttonFont_ = CreateUiFont(dpi, 10, FW_SEMIBOLD);
    iconFont_ = CreateIconFont(dpi, 11);
    if (logo_) DestroyIcon(logo_);
    logo_ = nullptr;
    LoadIconWithScaleDown(instance_, MAKEINTRESOURCEW(IDI_FLASHSHOT), Scale(kLogo), Scale(kLogo), &logo_);
    ApplyWindowStyle(window_, theme_);
}

void HomeWindow::Layout() {
    const int width = Scale(kWidth);
    const int padding = Scale(kPadding);
    const int inner = width - 2 * padding;
    int y = padding;
    logoRect_ = {padding, y, padding + Scale(kLogo), y + Scale(kLogo)};
    titleRect_ = {logoRect_.right + Scale(16), y + Scale(2), width - padding, y + Scale(28)};
    subtitleRect_ = {titleRect_.left, titleRect_.bottom, width - padding, y + Scale(kLogo)};
    y += Scale(kLogo) + Scale(18);
    introRect_ = {padding, y, width - padding, y + Scale(40)};
    y += Scale(40) + Scale(14);
    SetWindowPos(GetDlgItem(window_, kNewCapture), nullptr, padding, y, inner, Scale(kButtonHeight), SWP_NOZORDER | SWP_NOACTIVATE);
    y += Scale(kButtonHeight) + Scale(6);
    shortcutRect_ = {padding, y, width - padding, y + Scale(problem_.empty() ? 22 : 40)};
    y = shortcutRect_.bottom + Scale(12);
    SetWindowPos(GetDlgItem(window_, kSettings), nullptr, padding, y, inner, Scale(kButtonHeight - 4), SWP_NOZORDER | SWP_NOACTIVATE);
    y += Scale(kButtonHeight - 4) + Scale(18);
    footerRect_ = {padding, y, width - padding, y + Scale(36)};
    y = footerRect_.bottom + padding;

    RECT frame{0, 0, width, y};
    AdjustWindowRectExForDpi(&frame, GetWindowLongW(window_, GWL_STYLE), FALSE, GetWindowLongW(window_, GWL_EXSTYLE),
                             GetDpiForWindow(window_));
    SetWindowPos(window_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(window_, nullptr, TRUE);
}

void HomeWindow::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window_, &paint);
    RECT client;
    GetClientRect(window_, &client);
    FillRect(dc, &client, background_);
    SetBkMode(dc, TRANSPARENT);
    if (logo_) DrawIconEx(dc, logoRect_.left, logoRect_.top, logo_, Scale(kLogo), Scale(kLogo), 0, nullptr, DI_NORMAL);

    const auto draw = [&](HFONT font, COLORREF color, const std::wstring& text, RECT rect, UINT format) {
        HGDIOBJ previous = SelectObject(dc, font);
        SetTextColor(dc, color);
        DrawTextW(dc, text.c_str(), -1, &rect, format | DT_NOPREFIX);
        SelectObject(dc, previous);
    };
    draw(titleFont_, theme_.text, L"FlashShot", titleRect_, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    draw(textFont_, theme_.secondaryText, L"Screen Capture", subtitleRect_, DT_LEFT | DT_TOP | DT_SINGLELINE);
    draw(textFont_, theme_.text, L"Select part of the screen, mark it up with arrows, text, numbers or censor boxes, then copy or save it.",
         introRect_, DT_LEFT | DT_WORDBREAK);
    if (problem_.empty()) {
        draw(textFont_, theme_.secondaryText, L"Shortcut: " + shortcut_ + L", from any app.", shortcutRect_,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        draw(textFont_, RGB(232, 72, 85), problem_, shortcutRect_, DT_CENTER | DT_WORDBREAK);
    }
    draw(textFont_, theme_.secondaryText,
         L"FlashShot keeps running in the notification area. Closing this window does not stop it.", footerRect_,
         DT_CENTER | DT_WORDBREAK);
    EndPaint(window_, &paint);
}

LRESULT CALLBACK HomeWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<HomeWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<HomeWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT HomeWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_COMMAND:
            if (LOWORD(wParam) == kNewCapture && onNewCapture) onNewCapture();
            if (LOWORD(wParam) == kSettings && onSettings) onSettings();
            return 0;
        case WM_DRAWITEM: {
            const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            const ButtonFonts fonts{buttonFont_, iconFont_};
            if (item.CtlID == kNewCapture) {
                DrawButton(item, theme_, fonts, ButtonStyle::Primary, icons::kCrop, L"New capture", Scale(kRadius));
            } else {
                DrawButton(item, theme_, fonts, ButtonStyle::Standard, icons::kSettings, L"Settings", Scale(kRadius));
            }
            return TRUE;
        }
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETTINGCHANGE:
            RefreshStyle();
            Layout();
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            RefreshStyle();
            Layout();
            return 0;
        }
        case WM_CLOSE:
            Hide();
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace flashshot
