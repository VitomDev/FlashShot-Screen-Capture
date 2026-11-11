#pragma once

#include <windows.h>

namespace flashshot {

// Colors and fonts of FlashShot's own windows, following the Windows light/dark
// app setting and accent color, in the style of Windows 11 flyouts.
struct Theme {
    bool dark = false;
    COLORREF background;
    COLORREF text;
    COLORREF secondaryText;
    COLORREF divider;
    COLORREF hover;
    COLORREF pressed;
    COLORREF accent;
    COLORREF accentText;

    static Theme Current();
};

// "Segoe UI Variable" on Windows 11, "Segoe UI" on Windows 10.
HFONT CreateUiFont(UINT dpi, int points, int weight = FW_NORMAL);
// "Segoe Fluent Icons" on Windows 11, "Segoe MDL2 Assets" on Windows 10.
HFONT CreateIconFont(UINT dpi, int points);

// Lets menus follow the dark app theme (tray and popup menus).
void EnableDarkMenus();

// Rounded corners, a matching border and dark-mode scroll bars.
void ApplyWindowStyle(HWND window, const Theme& theme);

// Drawing helpers for owner-drawn controls.
void FillRounded(HDC dc, const RECT& rect, COLORREF color, int radius);
void OutlineRounded(HDC dc, const RECT& rect, COLORREF color, int radius, int width = 1);
int TextWidth(HWND window, HFONT font, const wchar_t* text);

// Hover highlight for owner-drawn buttons: repaints them on enter and leave.
void EnableHover(HWND button);
bool IsHot(HWND button);

// The owner-drawn buttons shared by FlashShot's windows: an optional icon and a
// label, centered. Primary is accent filled, Standard has a subtle fill and
// border, Subtle only shows a fill on hover.
enum class ButtonStyle { Primary, Standard, Subtle };
struct ButtonFonts {
    HFONT text;
    HFONT icon;
};
void DrawButton(const DRAWITEMSTRUCT& item, const Theme& theme, const ButtonFonts& fonts, ButtonStyle style,
                const wchar_t* icon, const wchar_t* label, int radius, COLORREF iconColor = CLR_INVALID);
// Width that fits the icon and label with comfortable padding.
int ButtonWidth(HWND window, const ButtonFonts& fonts, const wchar_t* icon, const wchar_t* label, UINT dpi);

namespace icons {
inline constexpr wchar_t kCopy[] = L"";
inline constexpr wchar_t kCheck[] = L"";
inline constexpr wchar_t kClose[] = L"";
inline constexpr wchar_t kInfo[] = L"";
inline constexpr wchar_t kSave[] = L"";
inline constexpr wchar_t kDelete[] = L"";
inline constexpr wchar_t kSettings[] = L"";
inline constexpr wchar_t kMore[] = L"";
inline constexpr wchar_t kUndo[] = L"";
inline constexpr wchar_t kRedo[] = L"";
inline constexpr wchar_t kPointer[] = L"";
inline constexpr wchar_t kArrow[] = L"";
inline constexpr wchar_t kPen[] = L"";
inline constexpr wchar_t kMarker[] = L"";
inline constexpr wchar_t kText[] = L"";
inline constexpr wchar_t kCrop[] = L"";
inline constexpr wchar_t kWarning[] = L"";
}  // namespace icons

}  // namespace flashshot
