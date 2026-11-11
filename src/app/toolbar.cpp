#include "app/toolbar.h"

#include "core/gdiplus_headers.h"

#include <algorithm>
#include <cmath>

namespace flashshot {

namespace {

// Layout in 96-DPI pixels.
constexpr int kButton = 32;
constexpr int kSwatch = 26;
constexpr int kPadding = 4;
constexpr int kGap = 2;
constexpr int kSeparator = 9;
constexpr int kBarGap = 6;
constexpr int kMargin = 8;
constexpr int kRadius = 8;

struct Slot {
    bool separator = false;
    ToolbarButton button{Action::Cancel};
    int width = 0;
};

Gdiplus::Color ToColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

bool IsShape(Tool tool) { return tool == Tool::Line || tool == Tool::Rectangle || tool == Tool::Ellipse; }
bool IsDraw(Tool tool) { return tool == Tool::Pen || tool == Tool::Marker; }

const wchar_t* GlyphFor(Tool tool) {
    switch (tool) {
        case Tool::Select: return icons::kPointer;
        case Tool::Arrow: return icons::kArrow;
        case Tool::Pen: return icons::kPen;
        case Tool::Marker: return icons::kMarker;
        case Tool::Text: return icons::kText;
        default: return nullptr;
    }
}

// Icons that the icon font does not have, drawn in a box of `size` pixels.
void DrawToolShape(HDC dc, Tool tool, const RECT& box, COLORREF color, int number = 1) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    const float x = static_cast<float>(box.left), y = static_cast<float>(box.top);
    const float s = static_cast<float>(box.right - box.left);
    const float stroke = std::max(1.0f, s / 11);
    Gdiplus::Pen pen(ToColor(color), stroke);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::SolidBrush brush(ToColor(color));
    switch (tool) {
        case Tool::Line:
            g.DrawLine(&pen, x + s * 0.15f, y + s * 0.85f, x + s * 0.85f, y + s * 0.15f);
            break;
        case Tool::Rectangle:
            g.DrawRectangle(&pen, x + s * 0.12f, y + s * 0.22f, s * 0.76f, s * 0.56f);
            break;
        case Tool::Ellipse:
            g.DrawEllipse(&pen, x + s * 0.1f, y + s * 0.18f, s * 0.8f, s * 0.64f);
            break;
        case Tool::Censor: {
            g.FillRectangle(&brush, x + s * 0.08f, y + s * 0.3f, s * 0.84f, s * 0.4f);
            g.DrawLine(&pen, x + s * 0.08f, y + s * 0.15f, x + s * 0.55f, y + s * 0.15f);
            g.DrawLine(&pen, x + s * 0.08f, y + s * 0.85f, x + s * 0.7f, y + s * 0.85f);
            break;
        }
        case Tool::Number: {
            g.DrawEllipse(&pen, x + s * 0.06f, y + s * 0.06f, s * 0.88f, s * 0.88f);
            Gdiplus::Font font(L"Segoe UI", s * 0.56f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
            const std::wstring label = std::to_wstring(number);
            g.DrawString(label.c_str(), -1, &font, Gdiplus::RectF(x, y + s * 0.03f, s, s), &format, &brush);
            break;
        }
        default:
            break;
    }
}

void DrawGlyph(HDC dc, HFONT font, const wchar_t* glyph, const RECT& rect, COLORREF color) {
    HGDIOBJ previous = SelectObject(dc, font);
    SetTextColor(dc, color);
    RECT r = rect;
    DrawTextW(dc, glyph, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
}

}  // namespace

const wchar_t* ToolName(Tool tool) {
    switch (tool) {
        case Tool::Select: return L"Select";
        case Tool::Arrow: return L"Arrow";
        case Tool::Line: return L"Line";
        case Tool::Rectangle: return L"Rectangle";
        case Tool::Ellipse: return L"Ellipse";
        case Tool::Pen: return L"Pen";
        case Tool::Marker: return L"Marker";
        case Tool::Text: return L"Text";
        case Tool::Number: return L"Numbering";
        case Tool::Censor: return L"Censor";
    }
    return L"";
}

wchar_t ToolKey(Tool tool) {
    static constexpr wchar_t keys[] = {L'V', L'A', L'L', L'R', L'E', L'P', L'M', L'T', L'N', L'X'};
    return keys[static_cast<size_t>(tool)];
}

std::wstring ToolTooltip(Tool tool) { return std::wstring(ToolName(tool)) + L" (" + ToolKey(tool) + L")"; }

const MonitorArea& MonitorAt(const std::vector<MonitorArea>& monitors, POINT point) {
    const MonitorArea* nearest = &monitors.front();
    long best = LONG_MAX;
    for (const MonitorArea& monitor : monitors) {
        if (PtInRect(&monitor.bounds, point)) return monitor;
        const long dx = std::max({monitor.bounds.left - point.x, 0L, point.x - monitor.bounds.right + 1});
        const long dy = std::max({monitor.bounds.top - point.y, 0L, point.y - monitor.bounds.bottom + 1});
        if (dx + dy < best) {
            best = dx + dy;
            nearest = &monitor;
        }
    }
    return *nearest;
}

Toolbar::~Toolbar() {
    for (HFONT font : {textFont_, iconFont_, smallIconFont_}) {
        if (font) DeleteObject(font);
    }
}

void Toolbar::EnsureFonts(UINT dpi) {
    if (fontDpi_ == dpi) return;
    for (HFONT font : {textFont_, iconFont_, smallIconFont_}) {
        if (font) DeleteObject(font);
    }
    textFont_ = CreateUiFont(dpi, 10, FW_SEMIBOLD);
    iconFont_ = CreateIconFont(dpi, 12);
    smallIconFont_ = CreateIconFont(dpi, 6);
    fontDpi_ = dpi;
}

void Toolbar::Layout(const Session& session, Tool shapeTool, Tool drawTool, const std::vector<MonitorArea>& monitors) {
    buttons_.clear();
    overflow_.clear();
    separators_.clear();
    hot_ = pressed_ = -1;
    if (!session.HasSelection() || monitors.empty()) {
        mainRect_ = optionsRect_ = {};
        return;
    }
    const RECT& region = session.selection();
    const LONG centerX = (region.left + region.right) / 2;
    const MonitorArea& below = MonitorAt(monitors, {centerX, region.bottom - 1});
    dpi_ = below.dpi;
    EnsureFonts(dpi_);
    theme_ = Theme::Current();

    HDC measure = GetDC(nullptr);
    HGDIOBJ previousFont = SelectObject(measure, textFont_);
    const auto labeled = [&](const wchar_t* label) {
        SIZE size{};
        GetTextExtentPoint32W(measure, label, static_cast<int>(wcslen(label)), &size);
        return Scale(12 + 16 + 8 + 14) + size.cx;
    };

    // --- Main bar ---
    const Tool tool = session.tool();
    std::vector<Slot> slots;
    const auto add = [&](Action action, int value, std::wstring name, std::wstring tooltip, int width, bool active = false, bool enabled = true) {
        Slot slot;
        slot.button = {action, value, {}, active, enabled, std::move(name), std::move(tooltip)};
        slot.width = width;
        slots.push_back(std::move(slot));
    };
    const auto separator = [&] {
        Slot slot;
        slot.separator = true;
        slot.width = Scale(kSeparator);
        slots.push_back(slot);
    };
    const int square = Scale(kButton);
    for (Tool t : {Tool::Select, Tool::Arrow}) {
        add(Action::Tool, static_cast<int>(t), ToolName(t), ToolTooltip(t), square, tool == t);
    }
    add(Action::ShapesGroup, static_cast<int>(shapeTool), L"Shapes", L"Shapes: " + ToolTooltip(shapeTool), square, IsShape(tool));
    add(Action::DrawGroup, static_cast<int>(drawTool), L"Draw", L"Draw: " + ToolTooltip(drawTool), square, IsDraw(tool));
    for (Tool t : {Tool::Text, Tool::Number, Tool::Censor}) {
        add(Action::Tool, static_cast<int>(t), ToolName(t), ToolTooltip(t), square, tool == t);
    }
    separator();
    add(Action::Undo, 0, L"Undo", L"Undo (Ctrl+Z)", square, false, session.CanUndo());
    add(Action::Redo, 0, L"Redo", L"Redo (Ctrl+Shift+Z)", square, false, session.CanRedo());
    separator();
    add(Action::Cancel, 0, L"Cancel", L"Cancel (Esc)", square);
    add(Action::Save, 0, L"Save", L"Save as… (Ctrl+Shift+S)\nCtrl+S saves straight to the last folder", labeled(L"Save"));
    add(Action::Copy, 0, L"Copy", L"Copy to clipboard (Ctrl+C)", labeled(L"Copy"));

    const auto totalWidth = [&] {
        int width = Scale(kPadding) * 2;
        for (const Slot& slot : slots) width += slot.width + Scale(kGap);
        return width - Scale(kGap);
    };
    // Narrow screens: less used actions move to a "More" menu before the
    // bar would be cut or cover the region.
    const int available = (below.work.right - below.work.left) - 2 * Scale(kMargin);
    const auto collapse = [&](Action action, int value) {
        for (auto it = slots.begin(); it != slots.end(); ++it) {
            if (!it->separator && it->button.action == action && it->button.value == value) {
                overflow_.push_back(it->button);
                slots.erase(it);
                return;
            }
        }
    };
    const std::pair<Action, int> order[] = {{Action::Cancel, 0},
                                            {Action::Redo, 0},
                                            {Action::Undo, 0},
                                            {Action::Tool, static_cast<int>(Tool::Censor)},
                                            {Action::Tool, static_cast<int>(Tool::Number)},
                                            {Action::Tool, static_cast<int>(Tool::Text)},
                                            {Action::DrawGroup, static_cast<int>(drawTool)},
                                            {Action::ShapesGroup, static_cast<int>(shapeTool)},
                                            {Action::Tool, static_cast<int>(Tool::Arrow)}};
    for (const auto& [action, value] : order) {
        if (totalWidth() + (overflow_.empty() ? square + Scale(kGap) : 0) <= available) break;
        collapse(action, value);
    }
    if (!overflow_.empty()) {
        // Drop separators left without a neighbor, then add the menu button.
        for (size_t i = 0; i < slots.size();) {
            const bool orphan = slots[i].separator && (i == 0 || i + 1 == slots.size() || slots[i - 1].separator);
            if (orphan) slots.erase(slots.begin() + static_cast<std::ptrdiff_t>(i));
            else ++i;
        }
        Slot more;
        more.button = {Action::More, 0, {}, false, true, L"More", L"More actions"};
        more.width = square;
        slots.insert(slots.end() - 2, more);
    }

    // --- Options bar ---
    std::vector<Slot> options;
    const auto option = [&](Action action, int value, std::wstring name, int width, bool active) {
        Slot slot;
        slot.button = {action, value, {}, active, true, name, name};
        slot.width = width;
        options.push_back(std::move(slot));
    };
    if (const auto target = session.OptionsTarget()) {
        const ToolStyle style = session.CurrentStyle();
        static constexpr const wchar_t* colorNames[] = {L"Red", L"Orange", L"Yellow", L"Green", L"Blue", L"Purple", L"White", L"Black"};
        for (size_t i = 0; i < kPalette.size(); ++i) {
            option(Action::Color, static_cast<int>(i), colorNames[i], Scale(kSwatch), style.color == kPalette[i]);
        }
        const auto sizes = SizePresets(*target);
        if (!sizes.empty()) {
            Slot gap;
            gap.separator = true;
            gap.width = Scale(kSeparator);
            options.push_back(gap);
            static constexpr const wchar_t* sizeNames[] = {L"Small", L"Medium", L"Large", L"Extra large"};
            for (size_t i = 0; i < sizes.size(); ++i) {
                option(Action::Size, static_cast<int>(i), sizeNames[i], Scale(kSwatch + 2), style.size == sizes[i]);
            }
        }
        if (*target == Tool::Text) option(Action::Background, 0, L"Text background", square, style.background);
        if (session.selected() >= 0) {
            Slot gap;
            gap.separator = true;
            gap.width = Scale(kSeparator);
            options.push_back(gap);
            option(Action::Delete, 0, L"Delete (Del)", square, false);
        }
    }
    SelectObject(measure, previousFont);
    ReleaseDC(nullptr, measure);

    // --- Placement: below the region, else above it, else inside it ---
    const int mainHeight = square + 2 * Scale(kPadding);
    const int optionsHeight = options.empty() ? 0 : Scale(kSwatch + 6) + 2 * Scale(kPadding);
    const int barGap = Scale(kBarGap);
    const int blockHeight = mainHeight + (options.empty() ? 0 : barGap + optionsHeight);
    const int mainWidth = totalWidth();
    int optionsWidth = Scale(kPadding) * 2 - Scale(kGap);
    for (const Slot& slot : options) optionsWidth += slot.width + Scale(kGap);

    int mainTop, optionsTop;
    const MonitorArea& above = MonitorAt(monitors, {centerX, region.top});
    const MonitorArea* monitor = &below;
    if (region.bottom + barGap + blockHeight <= below.work.bottom) {
        mainTop = region.bottom + barGap;
        optionsTop = mainTop + mainHeight + barGap;
    } else if (region.top - barGap - blockHeight >= above.work.top) {
        monitor = &above;
        optionsTop = region.top - barGap - blockHeight;
        mainTop = options.empty() ? optionsTop : optionsTop + optionsHeight + barGap;
    } else {
        const LONG bottom = std::min(region.bottom, below.work.bottom) - barGap;
        mainTop = bottom - mainHeight;
        optionsTop = mainTop - barGap - optionsHeight;
    }
    const RECT& work = monitor->work;
    const auto placeX = [&](int width) {
        const int x = centerX - width / 2;
        return std::clamp<int>(x, work.left + Scale(kMargin), std::max<int>(work.left + Scale(kMargin), work.right - Scale(kMargin) - width));
    };
    const auto placeRow = [&](std::vector<Slot>& row, RECT& bar, int left, int top, int width, int height) {
        bar = {left, top, left + width, top + height};
        int x = left + Scale(kPadding);
        for (Slot& slot : row) {
            if (slot.separator) {
                separators_.push_back({x + slot.width / 2, top + Scale(10), x + slot.width / 2 + 1, top + height - Scale(10)});
            } else {
                const int buttonHeight = height - 2 * Scale(kPadding);
                slot.button.rect = {x, top + Scale(kPadding), x + slot.width, top + Scale(kPadding) + buttonHeight};
                buttons_.push_back(slot.button);
            }
            x += slot.width + Scale(kGap);
        }
    };
    placeRow(slots, mainRect_, placeX(mainWidth), mainTop, mainWidth, mainHeight);
    if (options.empty()) {
        optionsRect_ = {};
    } else {
        placeRow(options, optionsRect_, placeX(optionsWidth), optionsTop, optionsWidth, optionsHeight);
    }
}

int Toolbar::HitTest(POINT point) const {
    for (size_t i = 0; i < buttons_.size(); ++i) {
        if (PtInRect(&buttons_[i].rect, point)) return static_cast<int>(i);
    }
    return -1;
}

bool Toolbar::Contains(POINT point) const { return PtInRect(&mainRect_, point) || PtInRect(&optionsRect_, point); }

RECT Toolbar::bounds() const {
    RECT both;
    UnionRect(&both, &mainRect_, &optionsRect_);
    return both;
}

void Toolbar::PaintButton(HDC dc, const ToolbarButton& button, int index, const Session& session) const {
    const RECT& r = button.rect;
    const bool hot = index == hot_ && button.enabled;
    const bool pressed = index == pressed_ && hot;
    const int radius = Scale(4);
    const COLORREF disabled = theme_.dark ? RGB(100, 100, 100) : RGB(170, 170, 170);
    COLORREF foreground = button.enabled ? theme_.text : disabled;

    switch (button.action) {
        case Action::Copy:
            FillRounded(dc, r, hot ? RGB(GetRValue(theme_.accent) * 9 / 10, GetGValue(theme_.accent) * 9 / 10, GetBValue(theme_.accent) * 9 / 10)
                                   : theme_.accent, radius);
            foreground = theme_.accentText;
            break;
        case Action::Save:
            FillRounded(dc, r, hot ? theme_.hover : theme_.pressed, radius);
            OutlineRounded(dc, r, theme_.divider, radius);
            break;
        case Action::Color: {
            // Swatches: a ring shows the current color.
            const int size = Scale(16);
            const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
            if (button.active || hot) {
                RECT ring{cx - size / 2 - Scale(4), cy - size / 2 - Scale(4), cx + size / 2 + Scale(4), cy + size / 2 + Scale(4)};
                OutlineRounded(dc, ring, button.active ? theme_.accent : theme_.secondaryText, (ring.right - ring.left) / 2, Scale(2));
            }
            RECT swatch{cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2};
            FillRounded(dc, swatch, kPalette[button.value], size / 2);
            OutlineRounded(dc, swatch, theme_.divider, size / 2);
            return;
        }
        default:
            if (button.active) {
                FillRounded(dc, r, theme_.accent, radius);
                foreground = theme_.accentText;
            } else if (pressed) {
                FillRounded(dc, r, theme_.pressed, radius);
            } else if (hot) {
                FillRounded(dc, r, theme_.hover, radius);
            }
            break;
    }

    const int icon = Scale(16);
    const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    RECT iconBox{cx - icon / 2, cy - icon / 2, cx + icon / 2, cy + icon / 2};
    switch (button.action) {
        case Action::Tool:
        case Action::ShapesGroup:
        case Action::DrawGroup: {
            const Tool tool = static_cast<Tool>(button.value);
            if (const wchar_t* glyph = GlyphFor(tool)) {
                DrawGlyph(dc, iconFont_, glyph, r, foreground);
            } else {
                DrawToolShape(dc, tool, iconBox, foreground, session.nextNumber());
            }
            if (button.action != Action::Tool) {
                // Groups show a small chevron: clicking again opens their menu.
                RECT chevron{r.right - Scale(11), r.bottom - Scale(11), r.right - Scale(1), r.bottom - Scale(1)};
                DrawGlyph(dc, smallIconFont_, L"", chevron, foreground);
            }
            break;
        }
        case Action::Undo: DrawGlyph(dc, iconFont_, icons::kUndo, r, foreground); break;
        case Action::Redo: DrawGlyph(dc, iconFont_, icons::kRedo, r, foreground); break;
        case Action::Cancel: DrawGlyph(dc, iconFont_, icons::kClose, r, foreground); break;
        case Action::More: DrawGlyph(dc, iconFont_, icons::kMore, r, foreground); break;
        case Action::Delete: DrawGlyph(dc, iconFont_, icons::kDelete, r, foreground); break;
        case Action::Save:
        case Action::Copy: {
            const wchar_t* label = button.action == Action::Save ? L"Save" : L"Copy";
            RECT glyph{r.left + Scale(12), r.top, r.left + Scale(12) + icon, r.bottom};
            DrawGlyph(dc, iconFont_, button.action == Action::Save ? icons::kSave : icons::kCopy, glyph, foreground);
            HGDIOBJ previous = SelectObject(dc, textFont_);
            SetTextColor(dc, foreground);
            RECT text{glyph.right + Scale(8), r.top, r.right, r.bottom};
            DrawTextW(dc, label, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, previous);
            break;
        }
        case Action::Size: {
            const auto target = session.OptionsTarget();
            const auto sizes = SizePresets(target ? *target : Tool::Pen);
            if (target == Tool::Text) {
                HFONT font = CreateUiFont(dpi_, 7 + button.value * 2, FW_SEMIBOLD);
                HGDIOBJ previous = SelectObject(dc, font);
                SetTextColor(dc, foreground);
                RECT text = r;
                DrawTextW(dc, L"A", -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(dc, previous);
                DeleteObject(font);
            } else {
                const int dot = Scale(4 + button.value * 3);
                RECT d{cx - dot / 2, cy - dot / 2, cx - dot / 2 + dot, cy - dot / 2 + dot};
                FillRounded(dc, d, foreground, dot / 2);
            }
            break;
        }
        case Action::Background: {
            RECT pill{cx - Scale(10), cy - Scale(9), cx + Scale(10), cy + Scale(9)};
            FillRounded(dc, pill, foreground, Scale(3));
            DrawGlyph(dc, iconFont_, icons::kText, pill, button.active ? theme_.accent : theme_.background);
            break;
        }
        case Action::Color:
            break;
    }
}

void Toolbar::Paint(HDC dc, POINT origin, const Session& session) const {
    if (IsRectEmpty(&mainRect_)) return;
    // GDI+ ignores viewport offsets, so every rectangle is moved by hand.
    const auto at = [&](RECT r) {
        OffsetRect(&r, -origin.x, -origin.y);
        return r;
    };
    SetBkMode(dc, TRANSPARENT);
    for (const RECT* bar : {&mainRect_, &optionsRect_}) {
        if (IsRectEmpty(bar)) continue;
        FillRounded(dc, at(*bar), theme_.background, Scale(kRadius));
        OutlineRounded(dc, at(*bar), theme_.divider, Scale(kRadius));
    }
    HBRUSH divider = CreateSolidBrush(theme_.divider);
    for (const RECT& line : separators_) {
        const RECT r = at(line);
        FillRect(dc, &r, divider);
    }
    DeleteObject(divider);
    for (size_t i = 0; i < buttons_.size(); ++i) {
        ToolbarButton moved = buttons_[i];
        moved.rect = at(moved.rect);
        PaintButton(dc, moved, static_cast<int>(i), session);
    }
}

}  // namespace flashshot
