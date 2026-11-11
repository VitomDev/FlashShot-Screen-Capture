#pragma once

#include "app/screen_capture.h"
#include "app/theme.h"
#include "core/session.h"

#include <windows.h>

#include <array>
#include <string>
#include <vector>

namespace flashshot {

enum class Action { Tool, ShapesGroup, DrawGroup, Undo, Redo, Cancel, Save, Copy, More, Color, Size, Background, Delete };

struct ToolbarButton {
    Action action;
    int value = 0;  // Tool, color or size index
    RECT rect{};
    bool active = false;
    bool enabled = true;
    std::wstring name;     // Menu and tooltip text
    std::wstring tooltip;  // With the shortcut, when there is one
};

inline constexpr std::array<COLORREF, 8> kPalette = {RGB(229, 72, 77),  RGB(247, 107, 21), RGB(255, 213, 0),
                                                     RGB(48, 164, 108), RGB(0, 144, 255),  RGB(142, 78, 198),
                                                     RGB(255, 255, 255), RGB(0, 0, 0)};

// The floating bars of the editor: tools and outputs on the main bar, the
// options of the active tool or object on a second one. Placed next to the
// region inside the visible monitor; actions that do not fit go to a menu.
// Everything is in overlay client pixels (= snapshot pixels).
class Toolbar {
public:
    ~Toolbar();

    void Layout(const Session& session, Tool shapeTool, Tool drawTool, const std::vector<MonitorArea>& monitors);
    void Paint(HDC dc, POINT origin, const Session& session) const;

    // Index of the button under `point`, or -1.
    int HitTest(POINT point) const;
    bool Contains(POINT point) const;
    const ToolbarButton& button(int index) const { return buttons_[index]; }
    const std::vector<ToolbarButton>& buttons() const { return buttons_; }
    const std::vector<ToolbarButton>& overflow() const { return overflow_; }
    // Both bars and the size label, for repainting.
    RECT bounds() const;
    const RECT& main() const { return mainRect_; }
    UINT dpi() const { return dpi_; }

    void SetHot(int index) { hot_ = index; }
    void SetPressed(int index) { pressed_ = index; }
    int hot() const { return hot_; }

private:
    int Scale(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }
    void EnsureFonts(UINT dpi);
    void PaintButton(HDC dc, const ToolbarButton& button, int index, const Session& session) const;

    std::vector<ToolbarButton> buttons_;
    std::vector<ToolbarButton> overflow_;
    RECT mainRect_{};
    RECT optionsRect_{};
    std::vector<RECT> separators_;
    UINT dpi_ = 96;
    UINT fontDpi_ = 0;
    Theme theme_ = Theme::Current();
    HFONT textFont_ = nullptr;
    HFONT iconFont_ = nullptr;
    HFONT smallIconFont_ = nullptr;
    int hot_ = -1;
    int pressed_ = -1;
};

// Name of a tool with its shortcut key, e.g. "Arrow (A)".
std::wstring ToolTooltip(Tool tool);
const wchar_t* ToolName(Tool tool);
// The key that picks a tool, or 0.
wchar_t ToolKey(Tool tool);

// The monitor that holds `point`, or the nearest one.
const MonitorArea& MonitorAt(const std::vector<MonitorArea>& monitors, POINT point);

}  // namespace flashshot
