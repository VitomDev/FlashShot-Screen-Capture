#pragma once

#include "app/screen_capture.h"
#include "app/toolbar.h"
#include "core/session.h"
#include "core/settings.h"

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace flashshot {

// Posted to the host window once the capture is over; the host then reads
// result() and deletes the overlay.
inline constexpr UINT WM_APP_CAPTURE_CLOSED = WM_APP + 21;

struct CaptureResult {
    enum class Kind { Canceled, Copied, Saved, DisplayChanged };
    Kind kind = Kind::Canceled;
    RECT regionOnScreen{};
    std::filesystem::path file;
};

// One capture session on screen: a borderless topmost window over every
// monitor that shows the frozen desktop, lets the user pick a region and
// annotate it, and copies or saves the result. All window coordinates equal
// snapshot coordinates (physical pixels); the editing logic lives in Session.
class CaptureOverlay {
public:
    CaptureOverlay(HINSTANCE instance, HWND host, std::unique_ptr<ScreenSnapshot> snapshot, Settings& settings);
    ~CaptureOverlay();
    CaptureOverlay(const CaptureOverlay&) = delete;
    CaptureOverlay& operator=(const CaptureOverlay&) = delete;

    bool Show();
    const CaptureResult& result() const { return result_; }

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    int Scale(int value) const { return MulDiv(value, static_cast<int>(uiDpi_), 96); }
    float Tolerance() const { return static_cast<float>(Scale(6)); }

    // Painting
    void Paint();
    void PaintChrome(HDC dc, POINT origin);
    void Relayout();
    void AfterChange();

    // Input
    void OnPointerDown(POINT point, WPARAM keys, bool doubleClick);
    void OnPointerMove(POINT point, WPARAM keys);
    void OnPointerUp(POINT point, WPARAM keys);
    bool OnKey(UINT key);
    void UpdateHot(POINT point);
    HCURSOR CursorAt(POINT point) const;
    void Execute(const ToolbarButton& button);
    void ShowGroupMenu(const ToolbarButton& button);
    void ShowMoreMenu(const ToolbarButton& button);
    void SetTool(Tool tool);
    void UpdateTooltips();

    // Text editing through a real edit box, styled like the result.
    void SyncTextEditor();
    void PlaceTextEditor();
    void UpdateEditorBackground();
    void DestroyTextEditor();

    // Output
    void Copy();
    void SaveAs();
    void QuickSave();
    void ShowMessage(const std::wstring& text);
    void Close(CaptureResult::Kind kind, const std::filesystem::path& file = {});

    HINSTANCE instance_;
    HWND host_;
    HWND window_ = nullptr;
    HWND tooltip_ = nullptr;
    std::unique_ptr<ScreenSnapshot> snapshot_;
    std::vector<MonitorArea> monitors_;
    Settings& settings_;
    Session session_;
    Toolbar toolbar_;
    HBITMAP dimmed_ = nullptr;
    UINT uiDpi_ = 96;
    RECT chrome_{};
    RECT labelRect_{};
    RECT messageRect_{};
    std::wstring message_;
    bool tracking_ = false;
    bool canvasCapture_ = false;
    int pressedButton_ = -1;
    bool exporting_ = false;
    bool closing_ = false;
    CaptureResult result_;
    size_t tooltipCount_ = 0;

    HWND edit_ = nullptr;
    HFONT editFont_ = nullptr;
    float editFontSize_ = 0;
    HBRUSH editBrush_ = nullptr;
    bool updatingEditor_ = false;
};

}  // namespace flashshot
