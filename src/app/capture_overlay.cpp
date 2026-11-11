#include "app/capture_overlay.h"

#include "app/theme.h"
#include "core/clipboard.h"
#include "core/encode.h"
#include "core/gdiplus_headers.h"
#include "core/paths.h"
#include "core/render.h"

#include <commctrl.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <ctime>

namespace flashshot {

namespace {

constexpr wchar_t kClassName[] = L"FlashShotOverlay";
constexpr int kDimPercent = 45;
constexpr UINT_PTR kMessageTimer = 1;
constexpr UINT kMessageMilliseconds = 5000;
constexpr COLORREF kErrorColor = RGB(232, 72, 85);

// Layout in 96-DPI pixels.
constexpr int kCropHandle = 8;
constexpr int kObjectHandle = 10;
constexpr int kBorder = 2;

enum MenuCommand { kMenuBase = 100 };

Gdiplus::Color ToColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

// A darkened copy of the snapshot, prepared once so painting only blits.
HBITMAP CreateDimmedCopy(const ScreenSnapshot& snapshot) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = snapshot.width();
    info.bmiHeader.biHeight = -snapshot.height();
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return nullptr;
    const size_t bytes = static_cast<size_t>(snapshot.width()) * snapshot.height() * 4;
    const uint8_t* source = snapshot.pixels();
    auto* target = static_cast<uint8_t*>(bits);
    constexpr int keep = (100 - kDimPercent) * 256 / 100;
    for (size_t i = 0; i < bytes; ++i) target[i] = static_cast<uint8_t>((source[i] * keep) >> 8);
    return bitmap;
}

std::wstring ErrorText(DWORD error) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text = buffer ? buffer : L"Error " + std::to_wstring(error);
    LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L'.')) text.pop_back();
    return text;
}

std::tm LocalNow() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    return local;
}

bool Down(int key) { return GetKeyState(key) < 0; }

}  // namespace

CaptureOverlay::CaptureOverlay(HINSTANCE instance, HWND host, std::unique_ptr<ScreenSnapshot> snapshot, Settings& settings)
    : instance_(instance),
      host_(host),
      snapshot_(std::move(snapshot)),
      monitors_(ListMonitors(snapshot_->bounds())),
      settings_(settings),
      session_(snapshot_->width(), snapshot_->height(), settings.styles, MeasureTextExtent) {}

CaptureOverlay::~CaptureOverlay() {
    DestroyTextEditor();
    if (tooltip_) DestroyWindow(tooltip_);
    if (window_) DestroyWindow(window_);
    if (dimmed_) DeleteObject(dimmed_);
}

bool CaptureOverlay::Show() {
    static const ATOM windowClass = [this] {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance_;
        wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = kClassName;
        return RegisterClassExW(&wc);
    }();
    if (!windowClass || monitors_.empty()) return false;
    dimmed_ = CreateDimmedCopy(*snapshot_);
    if (!dimmed_) return false;

    const RECT& bounds = snapshot_->bounds();
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, L"FlashShot capture", WS_POPUP | WS_CLIPCHILDREN,
                              bounds.left, bounds.top, snapshot_->width(), snapshot_->height(), nullptr, nullptr, instance_, this);
    if (!window_) return false;
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0,
                               window_, nullptr, instance_, nullptr);
    if (tooltip_) SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 600);

    const POINT cursor = [] {
        POINT p;
        GetCursorPos(&p);
        return p;
    }();
    uiDpi_ = MonitorAt(monitors_, {cursor.x - bounds.left, cursor.y - bounds.top}).dpi;
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    SetFocus(window_);
    return true;
}

// --- Painting ---------------------------------------------------------------------

void CaptureOverlay::Paint() {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window_, &paint);
    const RECT area = paint.rcPaint;
    const int width = area.right - area.left;
    const int height = area.bottom - area.top;
    if (width <= 0 || height <= 0) {
        EndPaint(window_, &paint);
        return;
    }

    // Compose off screen and copy once to avoid flicker.
    HDC buffer = CreateCompatibleDC(dc);
    HBITMAP bufferBitmap = CreateCompatibleBitmap(dc, width, height);
    HGDIOBJ previousBuffer = SelectObject(buffer, bufferBitmap);
    HDC source = CreateCompatibleDC(dc);
    HGDIOBJ previousSource = SelectObject(source, dimmed_);
    BitBlt(buffer, 0, 0, width, height, source, area.left, area.top, SRCCOPY);

    // The region keeps its original brightness, with the annotations on top.
    RECT bright;
    const RECT& region = session_.selection();
    if (IntersectRect(&bright, &region, &area)) {
        SelectObject(source, snapshot_->bitmap());
        BitBlt(buffer, bright.left - area.left, bright.top - area.top, bright.right - bright.left, bright.bottom - bright.top, source,
               bright.left, bright.top, SRCCOPY);
        OffsetRect(&bright, -area.left, -area.top);
        DrawAnnotations(buffer, {area.left, area.top}, bright, session_.annotations(), session_.editingText());
    }
    SelectObject(source, previousSource);
    DeleteDC(source);

    PaintChrome(buffer, {area.left, area.top});

    BitBlt(dc, area.left, area.top, width, height, buffer, 0, 0, SRCCOPY);
    SelectObject(buffer, previousBuffer);
    DeleteObject(bufferBitmap);
    DeleteDC(buffer);
    EndPaint(window_, &paint);
}

void CaptureOverlay::PaintChrome(HDC dc, POINT origin) {
    const RECT& region = session_.selection();
    const Theme theme = Theme::Current();
    const auto at = [&](RECT r) {
        OffsetRect(&r, -origin.x, -origin.y);
        return r;
    };
    if (!IsRectEmpty(&region)) {
        Gdiplus::Graphics g(dc);
        g.TranslateTransform(static_cast<float>(-origin.x), static_cast<float>(-origin.y));
        const float border = static_cast<float>(Scale(kBorder));
        // Outside the region, so the border never hides exported pixels.
        Gdiplus::Pen frame(ToColor(theme.accent), border);
        g.DrawRectangle(&frame, region.left - border / 2, region.top - border / 2, region.right - region.left + border,
                        region.bottom - region.top + border);

        const Box regionBox = Box::FromRect(region);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        const int editing = session_.editingText();
        const int selected = session_.selected();
        if (editing >= 0 || selected >= 0) {
            const Annotation& a = session_.annotations()[editing >= 0 ? editing : selected];
            const auto handles = Handles(a);
            if (editing >= 0 || handles.empty()) {
                // Objects without handles show a dashed outline when selected.
                const Box b = Bounds(a).Inflated(static_cast<float>(Scale(3)));
                Gdiplus::Pen dash(ToColor(theme.accent), static_cast<float>(Scale(1)));
                dash.SetDashStyle(Gdiplus::DashStyleDash);
                g.DrawRectangle(&dash, b.left, b.top, b.width(), b.height());
            }
            // Round handles for objects, square ones for the region.
            const float radius = Scale(kObjectHandle) / 2.0f;
            Gdiplus::SolidBrush fill(ToColor(theme.accent));
            Gdiplus::Pen ring(Gdiplus::Color(255, 255, 255, 255), static_cast<float>(Scale(2)));
            for (const Vec& h : handles) {
                g.FillEllipse(&fill, h.x - radius, h.y - radius, radius * 2, radius * 2);
                g.DrawEllipse(&ring, h.x - radius, h.y - radius, radius * 2, radius * 2);
            }
        }
        if (session_.HasSelection()) {
            g.SetSmoothingMode(Gdiplus::SmoothingModeNone);
            const float size = static_cast<float>(Scale(kCropHandle));
            Gdiplus::SolidBrush white(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::Pen edge(ToColor(theme.accent), static_cast<float>(Scale(1)));
            const float cx = (regionBox.left + regionBox.right) / 2, cy = (regionBox.top + regionBox.bottom) / 2;
            const Vec points[] = {{regionBox.left, regionBox.top},    {cx, regionBox.top},    {regionBox.right, regionBox.top},
                                  {regionBox.right, cy},              {regionBox.right, regionBox.bottom},
                                  {cx, regionBox.bottom},             {regionBox.left, regionBox.bottom}, {regionBox.left, cy}};
            for (const Vec& p : points) {
                const float x = std::round(p.x - size / 2), y = std::round(p.y - size / 2);
                g.FillRectangle(&white, x, y, size, size);
                g.DrawRectangle(&edge, x, y, size, size);
            }
        }
    }

    // Region size, in physical pixels.
    if (!IsRectEmpty(&labelRect_)) {
        const RECT r = at(labelRect_);
        FillRounded(dc, r, RGB(24, 24, 27), Scale(4));
        HFONT font = CreateUiFont(uiDpi_, 9, FW_SEMIBOLD);
        HGDIOBJ previous = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        const std::wstring size = std::to_wstring(region.right - region.left) + L" × " + std::to_wstring(region.bottom - region.top);
        RECT text = r;
        DrawTextW(dc, size.c_str(), -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, previous);
        DeleteObject(font);
    }

    toolbar_.Paint(dc, origin, session_);

    if (!message_.empty()) {
        const RECT r = at(messageRect_);
        FillRounded(dc, r, theme.background, Scale(6));
        OutlineRounded(dc, r, kErrorColor, Scale(6));
        HFONT icon = CreateIconFont(uiDpi_, 11);
        HFONT font = CreateUiFont(uiDpi_, 10);
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ previous = SelectObject(dc, icon);
        SetTextColor(dc, kErrorColor);
        RECT iconRect{r.left + Scale(12), r.top, r.left + Scale(28), r.bottom};
        DrawTextW(dc, icons::kWarning, -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, font);
        SetTextColor(dc, theme.text);
        RECT text{iconRect.right + Scale(8), r.top, r.right - Scale(12), r.bottom};
        DrawTextW(dc, message_.c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, previous);
        DeleteObject(icon);
        DeleteObject(font);
    }
}

void CaptureOverlay::Relayout() {
    const RECT& region = session_.selection();
    if (!IsRectEmpty(&region)) {
        uiDpi_ = MonitorAt(monitors_, {(region.left + region.right) / 2, (region.top + region.bottom) / 2}).dpi;
    }
    toolbar_.Layout(session_, settings_.shapeTool, settings_.drawTool, monitors_);

    chrome_ = {};
    labelRect_ = {};
    if (!IsRectEmpty(&region)) {
        chrome_ = region;
        InflateRect(&chrome_, Scale(kObjectHandle), Scale(kObjectHandle));
        // The size label sits above the region, or inside it at the top of the screen.
        const MonitorArea& monitor = MonitorAt(monitors_, {region.left, region.top});
        HDC dc = GetDC(window_);
        HFONT font = CreateUiFont(uiDpi_, 9, FW_SEMIBOLD);
        HGDIOBJ previous = SelectObject(dc, font);
        SIZE text{};
        GetTextExtentPoint32W(dc, L"00000 × 00000", 13, &text);
        SelectObject(dc, previous);
        DeleteObject(font);
        ReleaseDC(window_, dc);
        const int width = text.cx + Scale(12), height = Scale(22);
        int top = region.top - height - Scale(6);
        int left = region.left;
        if (top < monitor.bounds.top) top = region.top + Scale(6), left = region.left + Scale(6);
        left = std::clamp<int>(left, monitor.bounds.left, std::max<int>(monitor.bounds.left, monitor.bounds.right - width));
        labelRect_ = {left, top, left + width, top + height};
        UnionRect(&chrome_, &chrome_, &labelRect_);
    }
    if (!message_.empty() && !IsRectEmpty(&toolbar_.main())) {
        const RECT& bar = toolbar_.bounds();
        HDC dc = GetDC(window_);
        HFONT font = CreateUiFont(uiDpi_, 10);
        HGDIOBJ previous = SelectObject(dc, font);
        SIZE text{};
        GetTextExtentPoint32W(dc, message_.c_str(), static_cast<int>(message_.size()), &text);
        SelectObject(dc, previous);
        DeleteObject(font);
        ReleaseDC(window_, dc);
        const MonitorArea& monitor = MonitorAt(monitors_, {(bar.left + bar.right) / 2, bar.top});
        const int width = std::min<int>(text.cx + Scale(52), monitor.work.right - monitor.work.left - Scale(16));
        const int height = Scale(36);
        const bool barBelowRegion = bar.top >= region.bottom;
        int top = barBelowRegion ? bar.bottom + Scale(6) : bar.top - height - Scale(6);
        top = std::clamp<int>(top, monitor.work.top, std::max<int>(monitor.work.top, monitor.work.bottom - height));
        const int left = std::clamp<int>((bar.left + bar.right) / 2 - width / 2, monitor.work.left,
                                         std::max<int>(monitor.work.left, monitor.work.right - width));
        messageRect_ = {left, top, left + width, top + height};
        UnionRect(&chrome_, &chrome_, &messageRect_);
    } else {
        messageRect_ = {};
    }
    const RECT bars = toolbar_.bounds();
    UnionRect(&chrome_, &chrome_, &bars);
    UpdateTooltips();
}

void CaptureOverlay::AfterChange() {
    SyncTextEditor();
    const Box dirty = session_.TakeDirty();
    if (!dirty.empty()) {
        RECT r = dirty.Inflated(static_cast<float>(Scale(kObjectHandle) + Scale(4))).ToRect();
        InvalidateRect(window_, &r, FALSE);
    }
    if (session_.TakeLayoutChanged()) {
        RECT old = chrome_;
        Relayout();
        UnionRect(&old, &old, &chrome_);
        InvalidateRect(window_, &old, FALSE);
    }
}

void CaptureOverlay::UpdateTooltips() {
    if (!tooltip_) return;
    for (size_t i = 0; i < tooltipCount_; ++i) {
        TOOLINFOW info{sizeof(info)};
        info.hwnd = window_;
        info.uId = i + 1;
        SendMessageW(tooltip_, TTM_DELTOOL, 0, reinterpret_cast<LPARAM>(&info));
    }
    const auto& buttons = toolbar_.buttons();
    for (size_t i = 0; i < buttons.size(); ++i) {
        TOOLINFOW info{sizeof(info)};
        info.uFlags = TTF_SUBCLASS;
        info.hwnd = window_;
        info.uId = i + 1;
        info.rect = buttons[i].rect;
        info.lpszText = const_cast<LPWSTR>(buttons[i].tooltip.c_str());
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
    tooltipCount_ = buttons.size();
}

// --- Input ------------------------------------------------------------------------

void CaptureOverlay::OnPointerDown(POINT point, WPARAM keys, bool doubleClick) {
    if (closing_ || exporting_) return;
    const int button = toolbar_.HitTest(point);
    if (button >= 0) {
        pressedButton_ = button;
        toolbar_.SetPressed(button);
        InvalidateRect(window_, &toolbar_.button(button).rect, FALSE);
        SetCapture(window_);
        return;
    }
    if (toolbar_.Contains(point) || PtInRect(&labelRect_, point)) return;

    const Vec p{static_cast<float>(point.x), static_cast<float>(point.y)};
    if (doubleClick && (session_.tool() == Tool::Select || session_.tool() == Tool::Text)) {
        const auto hit = session_.HitTestAt(p, Tolerance());
        if (hit.target == Session::Target::Annotation && session_.annotations()[hit.index].kind == Kind::Text) {
            session_.BeginTextEdit(hit.index, p);
            AfterChange();
            return;
        }
    }
    session_.PointerDown(p, {(keys & MK_SHIFT) != 0, (keys & MK_CONTROL) != 0}, Tolerance());
    if (session_.GestureActive()) {
        canvasCapture_ = true;
        SetCapture(window_);
    }
    AfterChange();
}

void CaptureOverlay::OnPointerMove(POINT point, WPARAM keys) {
    if (canvasCapture_) {
        session_.PointerMove({static_cast<float>(point.x), static_cast<float>(point.y)}, {(keys & MK_SHIFT) != 0, (keys & MK_CONTROL) != 0});
        AfterChange();
        return;
    }
    UpdateHot(point);
}

void CaptureOverlay::OnPointerUp(POINT point, WPARAM keys) {
    if (pressedButton_ >= 0) {
        const int button = pressedButton_;
        pressedButton_ = -1;
        toolbar_.SetPressed(-1);
        ReleaseCapture();
        InvalidateRect(window_, &toolbar_.button(button).rect, FALSE);
        if (toolbar_.HitTest(point) == button) Execute(toolbar_.button(button));
        return;
    }
    if (!canvasCapture_) return;
    canvasCapture_ = false;
    const auto release = session_.PointerUp({static_cast<float>(point.x), static_cast<float>(point.y)},
                                            {(keys & MK_SHIFT) != 0, (keys & MK_CONTROL) != 0});
    ReleaseCapture();
    AfterChange();
    if (release == Session::Release::CopyRequested) Copy();
}

void CaptureOverlay::UpdateHot(POINT point) {
    const int hot = toolbar_.HitTest(point);
    if (hot == toolbar_.hot()) return;
    if (toolbar_.hot() >= 0) InvalidateRect(window_, &toolbar_.button(toolbar_.hot()).rect, FALSE);
    toolbar_.SetHot(hot);
    if (hot >= 0) InvalidateRect(window_, &toolbar_.button(hot).rect, FALSE);
    if (!tracking_) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
        tracking_ = TrackMouseEvent(&track) != FALSE;
    }
}

HCURSOR CaptureOverlay::CursorAt(POINT point) const {
    if (toolbar_.Contains(point) || PtInRect(&messageRect_, point)) return LoadCursorW(nullptr, IDC_ARROW);
    if (!session_.HasSelection()) return LoadCursorW(nullptr, IDC_CROSS);
    const auto hit = session_.HitTestAt({static_cast<float>(point.x), static_cast<float>(point.y)}, Tolerance());
    switch (hit.target) {
        case Session::Target::SelectionHandle: {
            static const LPCWSTR cursors[] = {IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE,
                                                  IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE};
            return LoadCursorW(nullptr, cursors[hit.handle]);
        }
        case Session::Target::AnnotationHandle:
            return LoadCursorW(nullptr, IDC_CROSS);
        case Session::Target::Annotation:
            return LoadCursorW(nullptr, session_.tool() == Tool::Text ? IDC_IBEAM : IDC_SIZEALL);
        case Session::Target::Inside:
            if (session_.tool() == Tool::Select) return LoadCursorW(nullptr, IDC_SIZEALL);
            if (session_.tool() == Tool::Text) return LoadCursorW(nullptr, IDC_IBEAM);
            return LoadCursorW(nullptr, IDC_CROSS);
        case Session::Target::Outside:
            break;
    }
    return LoadCursorW(nullptr, IDC_ARROW);
}

bool CaptureOverlay::OnKey(UINT key) {
    const bool ctrl = Down(VK_CONTROL), shift = Down(VK_SHIFT), alt = Down(VK_MENU);
    if (key == VK_ESCAPE) {
        if (session_.CancelGesture()) {
            canvasCapture_ = false;
            ReleaseCapture();
            AfterChange();
        } else {
            Close(CaptureResult::Kind::Canceled);
        }
        return true;
    }
    if (!session_.HasSelection() || session_.GestureActive() || exporting_) return false;
    if (ctrl && !alt) {
        switch (key) {
            case 'Z':
                shift ? session_.Redo() : session_.Undo();
                break;
            case 'Y':
                session_.Redo();
                break;
            case 'C':
                Copy();
                return true;
            case 'S':
                shift ? SaveAs() : QuickSave();
                return true;
            default:
                return false;
        }
        AfterChange();
        return true;
    }
    if (alt) return false;
    switch (key) {
        case VK_DELETE:
            session_.DeleteSelected();
            break;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN: {
            const int step = shift ? 10 : 1;
            const int dx = key == VK_LEFT ? -step : key == VK_RIGHT ? step : 0;
            const int dy = key == VK_UP ? -step : key == VK_DOWN ? step : 0;
            if (session_.selected() >= 0) {
                session_.NudgeSelected(static_cast<float>(dx), static_cast<float>(dy));
            } else {
                session_.NudgeSelection(dx, dy);
            }
            break;
        }
        default:
            for (size_t i = 0; i < kToolCount; ++i) {
                if (key == static_cast<UINT>(ToolKey(static_cast<Tool>(i)))) {
                    SetTool(static_cast<Tool>(i));
                    return true;
                }
            }
            return false;
    }
    AfterChange();
    return true;
}

void CaptureOverlay::SetTool(Tool tool) {
    if (tool == Tool::Line || tool == Tool::Rectangle || tool == Tool::Ellipse) settings_.shapeTool = tool;
    if (tool == Tool::Pen || tool == Tool::Marker) settings_.drawTool = tool;
    session_.SetTool(tool);
    AfterChange();
}

void CaptureOverlay::Execute(const ToolbarButton& button) {
    switch (button.action) {
        case Action::Tool:
            SetTool(static_cast<Tool>(button.value));
            break;
        case Action::ShapesGroup:
        case Action::DrawGroup:
            // A second click on the active group opens its choices.
            if (button.active) {
                ShowGroupMenu(button);
            } else {
                SetTool(static_cast<Tool>(button.value));
            }
            break;
        case Action::Undo:
            session_.Undo();
            break;
        case Action::Redo:
            session_.Redo();
            break;
        case Action::Cancel:
            Close(CaptureResult::Kind::Canceled);
            return;
        case Action::Save:
            SaveAs();
            return;
        case Action::Copy:
            Copy();
            return;
        case Action::More:
            ShowMoreMenu(button);
            return;
        case Action::Color:
            session_.SetColor(kPalette[button.value]);
            break;
        case Action::Size:
            if (const auto target = session_.OptionsTarget()) {
                const auto sizes = SizePresets(*target);
                if (button.value < static_cast<int>(sizes.size())) session_.SetSize(sizes[button.value]);
            }
            break;
        case Action::Background:
            session_.SetBackground(!session_.CurrentStyle().background);
            break;
        case Action::Delete:
            session_.DeleteSelected();
            break;
    }
    AfterChange();
    if (edit_) SetFocus(edit_);
}

void CaptureOverlay::ShowGroupMenu(const ToolbarButton& button) {
    const std::vector<Tool> tools = button.action == Action::ShapesGroup ? std::vector<Tool>{Tool::Line, Tool::Rectangle, Tool::Ellipse}
                                                                          : std::vector<Tool>{Tool::Pen, Tool::Marker};
    HMENU menu = CreatePopupMenu();
    for (Tool tool : tools) {
        const std::wstring label = std::wstring(ToolName(tool)) + L"\t" + ToolKey(tool);
        AppendMenuW(menu, MF_STRING | (session_.tool() == tool ? MF_CHECKED : 0), kMenuBase + static_cast<UINT>(tool), label.c_str());
    }
    POINT anchor{button.rect.left, button.rect.bottom};
    ClientToScreen(window_, &anchor);
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY, anchor.x, anchor.y, window_, nullptr);
    DestroyMenu(menu);
    if (command >= kMenuBase) SetTool(static_cast<Tool>(command - kMenuBase));
}

void CaptureOverlay::ShowMoreMenu(const ToolbarButton& button) {
    const auto overflow = toolbar_.overflow();
    HMENU menu = CreatePopupMenu();
    for (size_t i = 0; i < overflow.size(); ++i) {
        AppendMenuW(menu, MF_STRING | (overflow[i].enabled ? 0 : MF_GRAYED) | (overflow[i].active ? MF_CHECKED : 0),
                    kMenuBase + i, overflow[i].tooltip.c_str());
    }
    POINT anchor{button.rect.left, button.rect.bottom};
    ClientToScreen(window_, &anchor);
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY, anchor.x, anchor.y, window_, nullptr);
    DestroyMenu(menu);
    if (command >= kMenuBase && command - kMenuBase < overflow.size()) {
        ToolbarButton chosen = overflow[command - kMenuBase];
        chosen.active = false;  // Groups pick their tool rather than opening a menu
        Execute(chosen);
    }
}

// --- Text editing --------------------------------------------------------------------

void CaptureOverlay::SyncTextEditor() {
    const int editing = session_.editingText();
    if (editing < 0) {
        DestroyTextEditor();
        return;
    }
    const Annotation& text = session_.annotations()[editing];
    if (!edit_) {
        edit_ = CreateWindowExW(0, L"EDIT", nullptr, WS_CHILD | ES_MULTILINE | ES_AUTOHSCROLL | ES_AUTOVSCROLL | ES_WANTRETURN, 0, 0, 0,
                                0, window_, nullptr, instance_, nullptr);
        if (!edit_) {
            session_.FinishTextEdit(false);
            return;
        }
        SetWindowSubclass(edit_, EditProc, 0, reinterpret_cast<DWORD_PTR>(this));
        updatingEditor_ = true;
        SetWindowTextW(edit_, text.text.c_str());
        updatingEditor_ = false;
        editFontSize_ = 0;
    }
    if (editFontSize_ != text.size) {
        if (editFont_) DeleteObject(editFont_);
        editFont_ = CreateTextFont(text.size);
        editFontSize_ = text.size;
        SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), FALSE);
        SendMessageW(edit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    }
    PlaceTextEditor();
    if (!IsWindowVisible(edit_)) {
        ShowWindow(edit_, SW_SHOW);
        SendMessageW(edit_, EM_SETSEL, 0, -1);
        SetFocus(edit_);
    }
}

void CaptureOverlay::PlaceTextEditor() {
    const Annotation& text = session_.annotations()[session_.editingText()];
    const float padding = TextPadding(text);
    const int x = static_cast<int>(std::lround(text.points[0].x + padding));
    const int y = static_cast<int>(std::lround(text.points[0].y + padding));
    // Room for the caret and the next letter, so the box never scrolls.
    const int width = static_cast<int>(std::ceil(text.textExtent.x)) + static_cast<int>(text.size);
    const int height = static_cast<int>(std::ceil(text.textExtent.y));
    SetWindowPos(edit_, HWND_TOP, x, y, width, height, SWP_NOACTIVATE);
    UpdateEditorBackground();
    InvalidateRect(edit_, nullptr, TRUE);
}

// Without a background the edit box shows the capture behind it: a pattern
// brush made of exactly what the overlay draws there.
void CaptureOverlay::UpdateEditorBackground() {
    if (editBrush_) DeleteObject(editBrush_);
    editBrush_ = nullptr;
    const Annotation& text = session_.annotations()[session_.editingText()];
    if (text.background) {
        editBrush_ = CreateSolidBrush(ContrastColor(text.color));
        return;
    }
    RECT r;
    GetWindowRect(edit_, &r);
    MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&r), 2);
    const int width = r.right - r.left, height = r.bottom - r.top;
    if (width <= 0 || height <= 0) return;
    HDC screen = GetDC(window_);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    HDC source = CreateCompatibleDC(screen);
    HGDIOBJ previousSource = SelectObject(source, snapshot_->bitmap());
    BitBlt(dc, 0, 0, width, height, source, r.left, r.top, SRCCOPY);
    SelectObject(source, previousSource);
    DeleteDC(source);
    RECT clip{0, 0, width, height};
    RECT region = session_.selection();
    OffsetRect(&region, -r.left, -r.top);
    IntersectRect(&clip, &clip, &region);
    DrawAnnotations(dc, {r.left, r.top}, clip, session_.annotations(), session_.editingText());
    SelectObject(dc, previous);
    DeleteDC(dc);
    ReleaseDC(window_, screen);
    editBrush_ = CreatePatternBrush(bitmap);
    DeleteObject(bitmap);
}

void CaptureOverlay::DestroyTextEditor() {
    if (edit_) {
        const bool focused = GetFocus() == edit_;
        DestroyWindow(edit_);
        edit_ = nullptr;
        if (focused && window_) SetFocus(window_);
    }
    if (editFont_) DeleteObject(editFont_);
    if (editBrush_) DeleteObject(editBrush_);
    editFont_ = nullptr;
    editBrush_ = nullptr;
    editFontSize_ = 0;
}

LRESULT CALLBACK CaptureOverlay::EditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto* self = reinterpret_cast<CaptureOverlay*>(data);
    const bool ctrl = Down(VK_CONTROL);
    switch (message) {
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                // Escape leaves the text as it was before this edit.
                self->session_.FinishTextEdit(false);
                self->AfterChange();
                return 0;
            }
            if (wParam == VK_RETURN && ctrl) {
                self->session_.FinishTextEdit(true);
                self->AfterChange();
                return 0;
            }
            if (ctrl && wParam == 'A') {
                SendMessageW(edit, EM_SETSEL, 0, -1);
                return 0;
            }
            if (ctrl && wParam == 'S') {
                Down(VK_SHIFT) ? self->SaveAs() : self->QuickSave();
                return 0;
            }
            break;
        case WM_CHAR:
            // Swallow the control characters handled above and tabs (shown as spaces).
            if (wParam == 0x1B || wParam == 0x01 || wParam == 0x13 || wParam == L'\t' || (wParam == L'\n' && ctrl)) return 0;
            break;
        case WM_NCDESTROY:
            RemoveWindowSubclass(edit, EditProc, 0);
            break;
    }
    return DefSubclassProc(edit, message, wParam, lParam);
}

// --- Output -------------------------------------------------------------------------------

void CaptureOverlay::Copy() {
    if (exporting_ || closing_ || !session_.HasSelection()) return;
    session_.FinishTextEdit(true);
    AfterChange();
    exporting_ = true;
    BgraImage image = Compose(snapshot_->pixels(), snapshot_->width(), snapshot_->height(), session_.selection(), session_.annotations());
    const bool copied = !image.empty() && SetClipboardImage(host_, image);
    Wipe(image.pixels);
    if (copied) {
        Close(CaptureResult::Kind::Copied);
        return;
    }
    exporting_ = false;
    ShowMessage(L"Couldn't copy: another app is using the clipboard. Try again.");
}

void CaptureOverlay::SaveAs() {
    if (exporting_ || closing_ || !session_.HasSelection()) return;
    session_.FinishTextEdit(true);
    AfterChange();
    exporting_ = true;

    IFileSaveDialog* dialog = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        const COMDLG_FILTERSPEC types[] = {{L"PNG image", L"*.png"}, {L"JPEG image", L"*.jpg;*.jpeg"}};
        dialog->SetFileTypes(2, types);
        dialog->SetFileTypeIndex(1);
        dialog->SetDefaultExtension(L"png");
        dialog->SetFileName(CaptureFileName(LocalNow(), L".png").c_str());
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        const std::filesystem::path folder = settings_.saveFolder.empty() ? PicturesDirectory() : std::filesystem::path(settings_.saveFolder);
        IShellItem* start = nullptr;
        if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
            dialog->SetFolder(start);
            start->Release();
        }
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->Show(window_)) && SUCCEEDED(dialog->GetResult(&item))) {
            PWSTR name = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) path = name;
            CoTaskMemFree(name);
            item->Release();
        }
        dialog->Release();
    }
    SetForegroundWindow(window_);
    SetFocus(window_);
    if (path.empty()) {
        // Canceled: back to editing, nothing lost.
        exporting_ = false;
        return;
    }
    BgraImage image = Compose(snapshot_->pixels(), snapshot_->width(), snapshot_->height(), session_.selection(), session_.annotations());
    const DWORD error = SaveImage(image, path, FormatForPath(path));
    Wipe(image.pixels);
    if (error == ERROR_SUCCESS) {
        settings_.saveFolder = path.parent_path().wstring();
        Close(CaptureResult::Kind::Saved, path);
        return;
    }
    exporting_ = false;
    ShowMessage(L"Couldn't save " + path.filename().wstring() + L": " + ErrorText(error) + L".");
}

void CaptureOverlay::QuickSave() {
    if (exporting_ || closing_ || !session_.HasSelection()) return;
    session_.FinishTextEdit(true);
    AfterChange();
    const std::filesystem::path folder = settings_.saveFolder.empty() ? PicturesDirectory() : std::filesystem::path(settings_.saveFolder);
    std::error_code error;
    if (folder.empty() || !std::filesystem::is_directory(folder, error)) {
        ShowMessage(L"The folder " + folder.wstring() + L" is not available. Use Save to choose another.");
        return;
    }
    const std::filesystem::path path = UniquePath(folder, CaptureFileName(LocalNow(), L".png"));
    if (path.empty()) {
        ShowMessage(L"Couldn't find a free file name in " + folder.wstring() + L".");
        return;
    }
    exporting_ = true;
    BgraImage image = Compose(snapshot_->pixels(), snapshot_->width(), snapshot_->height(), session_.selection(), session_.annotations());
    const DWORD result = SaveImage(image, path, ImageFormat::Png);
    Wipe(image.pixels);
    if (result == ERROR_SUCCESS) {
        settings_.saveFolder = folder.wstring();
        Close(CaptureResult::Kind::Saved, path);
        return;
    }
    exporting_ = false;
    ShowMessage(L"Couldn't save to " + folder.wstring() + L": " + ErrorText(result) + L".");
}

void CaptureOverlay::ShowMessage(const std::wstring& text) {
    RECT old = chrome_;
    message_ = text;
    Relayout();
    UnionRect(&old, &old, &chrome_);
    InvalidateRect(window_, &old, FALSE);
    SetTimer(window_, kMessageTimer, kMessageMilliseconds, nullptr);
    MessageBeep(MB_ICONWARNING);
}

void CaptureOverlay::Close(CaptureResult::Kind kind, const std::filesystem::path& file) {
    if (closing_) return;
    closing_ = true;
    if (session_.editingText() >= 0) session_.FinishTextEdit(kind != CaptureResult::Kind::Canceled);
    DestroyTextEditor();
    result_.kind = kind;
    result_.file = file;
    result_.regionOnScreen = session_.selection();
    OffsetRect(&result_.regionOnScreen, snapshot_->bounds().left, snapshot_->bounds().top);
    settings_.styles = session_.styles();
    if (GetCapture() == window_) ReleaseCapture();
    ShowWindow(window_, SW_HIDE);
    PostMessageW(host_, WM_APP_CAPTURE_CLOSED, 0, 0);
}

// --- Window procedure ------------------------------------------------------------------------

LRESULT CALLBACK CaptureOverlay::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<CaptureOverlay*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<CaptureOverlay*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT CaptureOverlay::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    switch (message) {
        case WM_PAINT:
            Paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && reinterpret_cast<HWND>(wParam) == window_) {
                POINT cursor;
                GetCursorPos(&cursor);
                ScreenToClient(window_, &cursor);
                SetCursor(CursorAt(cursor));
                return TRUE;
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            OnPointerDown(point, wParam, message == WM_LBUTTONDBLCLK);
            return 0;
        case WM_MOUSEMOVE:
            OnPointerMove(point, wParam);
            return 0;
        case WM_LBUTTONUP:
            OnPointerUp(point, wParam);
            return 0;
        case WM_MOUSELEAVE:
            tracking_ = false;
            UpdateHot({-1, -1});
            return 0;
        case WM_RBUTTONUP:
            if (session_.CancelGesture()) {
                canvasCapture_ = false;
                ReleaseCapture();
                AfterChange();
            } else if (!session_.HasSelection()) {
                Close(CaptureResult::Kind::Canceled);
            }
            return 0;
        case WM_CAPTURECHANGED:
            // Capture taken away mid-drag (another window popped up): undo the drag.
            if (canvasCapture_ && reinterpret_cast<HWND>(lParam) != window_) {
                canvasCapture_ = false;
                session_.CancelGesture();
                AfterChange();
            }
            if (pressedButton_ >= 0 && reinterpret_cast<HWND>(lParam) != window_) {
                pressedButton_ = -1;
                toolbar_.SetPressed(-1);
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (OnKey(static_cast<UINT>(wParam))) return 0;
            break;
        case WM_COMMAND:
            if (reinterpret_cast<HWND>(lParam) == edit_ && edit_ && HIWORD(wParam) == EN_CHANGE && !updatingEditor_) {
                const int length = GetWindowTextLengthW(edit_);
                std::wstring text(static_cast<size_t>(length) + 1, L'\0');
                GetWindowTextW(edit_, text.data(), length + 1);
                text.resize(static_cast<size_t>(length));
                session_.SetEditingText(text);
                AfterChange();
            }
            return 0;
        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lParam) == edit_ && session_.editingText() >= 0) {
                const Annotation& text = session_.annotations()[session_.editingText()];
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, text.color);
                if (text.background) {
                    SetBkColor(dc, ContrastColor(text.color));
                } else {
                    SetBkMode(dc, TRANSPARENT);
                }
                SetBrushOrgEx(dc, 0, 0, nullptr);
                return reinterpret_cast<LRESULT>(editBrush_ ? editBrush_ : GetStockObject(WHITE_BRUSH));
            }
            break;
        case WM_TIMER:
            if (wParam == kMessageTimer) {
                KillTimer(window_, kMessageTimer);
                RECT old = chrome_;
                message_.clear();
                Relayout();
                UnionRect(&old, &old, &messageRect_);
                InvalidateRect(window_, &old, FALSE);
            }
            return 0;
        case WM_DISPLAYCHANGE:
            // The frozen image no longer matches the monitors.
            Close(CaptureResult::Kind::DisplayChanged);
            return 0;
        case WM_DPICHANGED:
            // The overlay spans every monitor; keep its size in physical pixels.
            return 0;
        case WM_SETTINGCHANGE:
            Relayout();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_CLOSE:
            Close(CaptureResult::Kind::Canceled);
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

}  // namespace flashshot
