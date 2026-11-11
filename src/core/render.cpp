#include "core/render.h"

#include "core/gdiplus_headers.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>


namespace flashshot {

namespace {

constexpr BYTE kMarkerAlpha = 105;

Gdiplus::Color ToColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

Gdiplus::PointF ToPoint(Vec v) { return {v.x, v.y}; }

std::unique_ptr<Gdiplus::Pen> StrokePen(const Annotation& a, BYTE alpha = 255) {
    auto pen = std::make_unique<Gdiplus::Pen>(ToColor(a.color, alpha), a.size);
    pen->SetStartCap(Gdiplus::LineCapRound);
    pen->SetEndCap(Gdiplus::LineCapRound);
    pen->SetLineJoin(Gdiplus::LineJoinRound);
    return pen;
}

void AddRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& r, float radius) {
    const float d = std::min({radius * 2, r.Width, r.Height});
    if (d <= 0) {
        path.AddRectangle(r);
        return;
    }
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}

void DrawArrow(Gdiplus::Graphics& g, const Annotation& a) {
    const Vec s = a.points[0], e = a.points[1];
    const float length = Distance(s, e);
    if (length <= 0) return;
    const float ux = (e.x - s.x) / length, uy = (e.y - s.y) / length;
    const float head = std::min(ArrowHeadLength(a.size), length);
    const float half = head * 0.5f;
    const Vec base{e.x - ux * head, e.y - uy * head};
    // The shaft stops inside the head so the tip stays sharp.
    auto pen = StrokePen(a);
    pen->SetEndCap(Gdiplus::LineCapFlat);
    const Vec shaftEnd{e.x - ux * head * 0.6f, e.y - uy * head * 0.6f};
    if (Distance(s, shaftEnd) > 0 && length > head * 0.6f) g.DrawLine(pen.get(), ToPoint(s), ToPoint(shaftEnd));
    const Gdiplus::PointF tip[] = {ToPoint(e), {base.x - uy * half, base.y + ux * half}, {base.x + uy * half, base.y - ux * half}};
    Gdiplus::SolidBrush brush(ToColor(a.color));
    g.FillPolygon(&brush, tip, 3);
}

// Marker strokes are drawn opaque on a layer, then blended once, so a stroke
// that crosses itself does not get darker where it overlaps.
void DrawMarker(Gdiplus::Graphics& g, const Annotation& a, const Box& visible) {
    const Box area = Bounds(a);
    const Box box{std::max(area.left, visible.left), std::max(area.top, visible.top), std::min(area.right, visible.right),
                  std::min(area.bottom, visible.bottom)};
    if (box.empty()) return;
    const RECT r = box.ToRect();
    const int w = r.right - r.left, h = r.bottom - r.top;
    Gdiplus::Bitmap layer(w, h, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics lg(&layer);
        lg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        lg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        lg.TranslateTransform(static_cast<float>(-r.left), static_cast<float>(-r.top));
        Gdiplus::SolidBrush brush(ToColor(a.color));
        if (a.points.size() == 1) {
            const float radius = a.size / 2;
            lg.FillEllipse(&brush, a.points[0].x - radius, a.points[0].y - radius, a.size, a.size);
        } else {
            std::vector<Gdiplus::PointF> points;
            for (const Vec& p : a.points) points.push_back(ToPoint(p));
            lg.DrawLines(StrokePen(a).get(), points.data(), static_cast<INT>(points.size()));
        }
    }
    Gdiplus::ColorMatrix matrix = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, kMarkerAlpha / 255.0f, 0}, {0, 0, 0, 0, 1}}};
    Gdiplus::ImageAttributes attributes;
    attributes.SetColorMatrix(&matrix);
    g.DrawImage(&layer, Gdiplus::Rect(r.left, r.top, w, h), 0, 0, w, h, Gdiplus::UnitPixel, &attributes);
}

void DrawNumber(Gdiplus::Graphics& g, const Annotation& a) {
    const Vec c = a.points[0];
    const float r = a.size;
    Gdiplus::SolidBrush fill(ToColor(a.color));
    g.FillEllipse(&fill, c.x - r, c.y - r, 2 * r, 2 * r);
    Gdiplus::Pen ring(ToColor(ContrastColor(a.color), 200), std::max(1.5f, r / 9));
    g.DrawEllipse(&ring, c.x - r, c.y - r, 2 * r, 2 * r);

    const std::wstring label = std::to_wstring(a.number);
    const float fontSize = r * (label.size() > 2 ? 0.8f : label.size() > 1 ? 0.95f : 1.15f);
    Gdiplus::Font font(L"Segoe UI", fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);
    format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    Gdiplus::SolidBrush text(ToColor(ContrastColor(a.color)));
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
    g.DrawString(label.c_str(), static_cast<INT>(label.size()), &font, Gdiplus::RectF(c.x - r, c.y - r + r * 0.04f, 2 * r, 2 * r),
                 &format, &text);
}

// Text and censor boxes go through plain GDI: text so it matches the edit
// box pixel for pixel, censor boxes so they cover whole pixels only.
void DrawWithGdi(Gdiplus::Graphics& g, const RECT& clip, const std::function<void(HDC)>& draw) {
    g.Flush(Gdiplus::FlushIntentionSync);
    HDC dc = g.GetHDC();
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
    draw(dc);
    RestoreDC(dc, saved);
    g.ReleaseHDC(dc);
}

void DrawTextLetters(HDC dc, const Annotation& a, POINT origin) {
    HFONT font = CreateTextFont(a.size);
    HGDIOBJ previous = SelectObject(dc, font);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, a.color);
    SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
    const float padding = TextPadding(a);
    const int x = static_cast<int>(std::lround(a.points[0].x + padding)) - origin.x;
    int y = static_cast<int>(std::lround(a.points[0].y + padding)) - origin.y;
    for (const std::wstring& line : SplitLines(a.text)) {
        TextOutW(dc, x, y, line.c_str(), static_cast<int>(line.size()));
        y += metrics.tmHeight;
    }
    SelectObject(dc, previous);
    DeleteObject(font);
}

}  // namespace

GdiplusSession::GdiplusSession() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token_, &input, nullptr) != Gdiplus::Ok) token_ = 0;
}

GdiplusSession::~GdiplusSession() {
    if (token_) Gdiplus::GdiplusShutdown(token_);
}

HFONT CreateTextFont(float size) {
    LOGFONTW font{};
    font.lfHeight = -std::max(1L, std::lround(size));
    font.lfWeight = FW_SEMIBOLD;
    font.lfCharSet = DEFAULT_CHARSET;
    // Grayscale smoothing: ClearType's colored fringes would end up in the image.
    font.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(font.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&font);
}

std::vector<std::wstring> SplitLines(const std::wstring& text) {
    std::vector<std::wstring> lines(1);
    for (wchar_t c : text) {
        if (c == L'\n') {
            lines.emplace_back();
        } else if (c != L'\r') {
            lines.back() += c == L'\t' ? L' ' : c;
        }
    }
    return lines;
}

Vec MeasureTextExtent(const Annotation& text) {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateTextFont(text.size);
    HGDIOBJ previous = SelectObject(dc, font);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    const auto lines = SplitLines(text.text);
    LONG width = 0;
    for (const std::wstring& line : lines) {
        SIZE size{};
        GetTextExtentPoint32W(dc, line.c_str(), static_cast<int>(line.size()), &size);
        width = std::max(width, size.cx);
    }
    // Italic-like overhangs of the last letter stay inside the box.
    width += metrics.tmOverhang + std::max(1L, metrics.tmAveCharWidth / 6);
    SelectObject(dc, previous);
    DeleteObject(font);
    DeleteDC(dc);
    return {static_cast<float>(width), static_cast<float>(metrics.tmHeight * static_cast<LONG>(lines.size()))};
}

void DrawAnnotations(HDC dc, POINT origin, const RECT& clip, const std::vector<Annotation>& annotations, int editing) {
    if (annotations.empty() || IsRectEmpty(&clip)) return;
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetClip(Gdiplus::Rect(clip.left, clip.top, clip.right - clip.left, clip.bottom - clip.top));
    g.TranslateTransform(static_cast<float>(-origin.x), static_cast<float>(-origin.y));
    const Box visible{static_cast<float>(clip.left + origin.x), static_cast<float>(clip.top + origin.y),
                      static_cast<float>(clip.right + origin.x), static_cast<float>(clip.bottom + origin.y)};

    for (size_t i = 0; i < annotations.size(); ++i) {
        const Annotation& a = annotations[i];
        if (a.points.empty() || !Bounds(a).Intersects(visible)) continue;
        switch (a.kind) {
            case Kind::Arrow:
                DrawArrow(g, a);
                break;
            case Kind::Line:
                g.DrawLine(StrokePen(a).get(), ToPoint(a.points[0]), ToPoint(a.points[1]));
                break;
            case Kind::Rectangle:
            case Kind::Ellipse: {
                const Box b = Box::Spanning(a.points[0], a.points[1]);
                auto pen = StrokePen(a);
                if (a.kind == Kind::Rectangle) {
                    pen->SetLineJoin(Gdiplus::LineJoinMiter);
                    g.DrawRectangle(pen.get(), b.left, b.top, b.width(), b.height());
                } else {
                    g.DrawEllipse(pen.get(), b.left, b.top, b.width(), b.height());
                }
                break;
            }
            case Kind::Pen:
                if (a.points.size() == 1) {
                    Gdiplus::SolidBrush brush(ToColor(a.color));
                    g.FillEllipse(&brush, a.points[0].x - a.size / 2, a.points[0].y - a.size / 2, a.size, a.size);
                } else {
                    std::vector<Gdiplus::PointF> points;
                    for (const Vec& p : a.points) points.push_back(ToPoint(p));
                    g.DrawLines(StrokePen(a).get(), points.data(), static_cast<INT>(points.size()));
                }
                break;
            case Kind::Marker:
                DrawMarker(g, a, visible);
                break;
            case Kind::Number:
                DrawNumber(g, a);
                break;
            case Kind::Censor:
                DrawWithGdi(g, clip, [&](HDC target) {
                    RECT r = Box::Spanning(a.points[0], a.points[1]).ToRect();
                    OffsetRect(&r, -origin.x, -origin.y);
                    HBRUSH brush = CreateSolidBrush(a.color);
                    FillRect(target, &r, brush);
                    DeleteObject(brush);
                });
                break;
            case Kind::Text: {
                if (a.background) {
                    const Box b = Bounds(a);
                    Gdiplus::GraphicsPath pill;
                    AddRoundedRect(pill, Gdiplus::RectF(b.left, b.top, b.width(), b.height()), TextPadding(a));
                    Gdiplus::SolidBrush brush(ToColor(ContrastColor(a.color)));
                    g.FillPath(&brush, &pill);
                }
                if (static_cast<int>(i) != editing) DrawWithGdi(g, clip, [&](HDC target) { DrawTextLetters(target, a, origin); });
                break;
            }
        }
    }
}

BgraImage Compose(const uint8_t* pixels, int width, int height, const RECT& region, const std::vector<Annotation>& annotations) {
    RECT r{std::clamp<LONG>(region.left, 0, width), std::clamp<LONG>(region.top, 0, height),
           std::clamp<LONG>(region.right, 0, width), std::clamp<LONG>(region.bottom, 0, height)};
    BgraImage image;
    if (r.right <= r.left || r.bottom <= r.top) return image;
    image.width = r.right - r.left;
    image.height = r.bottom - r.top;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) return {};
    const size_t row = static_cast<size_t>(image.width) * 4;
    auto* target = static_cast<uint8_t*>(bits);
    for (int y = 0; y < image.height; ++y) {
        memcpy(target + row * y, pixels + (static_cast<size_t>(r.top + y) * width + r.left) * 4, row);
    }

    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ previous = SelectObject(dc, bitmap);
    DrawAnnotations(dc, {r.left, r.top}, {0, 0, image.width, image.height}, annotations);
    GdiFlush();
    SelectObject(dc, previous);
    DeleteDC(dc);

    image.pixels.assign(target, target + row * image.height);
    // GDI leaves alpha undefined; the exported image is opaque.
    for (size_t i = 3; i < image.pixels.size(); i += 4) image.pixels[i] = 255;
    SecureZeroMemory(bits, row * image.height);
    DeleteObject(bitmap);
    return image;
}

}  // namespace flashshot
