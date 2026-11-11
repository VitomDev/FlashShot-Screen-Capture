#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace flashshot {

// All geometry is in snapshot pixels: physical pixels of the virtual desktop,
// with (0, 0) at its top-left corner.
struct Vec {
    float x = 0;
    float y = 0;

    bool operator==(const Vec&) const = default;
};

struct Box {
    float left = 0, top = 0, right = 0, bottom = 0;

    bool empty() const { return right <= left || bottom <= top; }
    float width() const { return right - left; }
    float height() const { return bottom - top; }
    bool Contains(Vec p) const { return p.x >= left && p.x < right && p.y >= top && p.y < bottom; }
    Box Inflated(float amount) const { return {left - amount, top - amount, right + amount, bottom + amount}; }
    Box Union(const Box& other) const;
    bool Intersects(const Box& other) const;
    RECT ToRect() const;  // Smallest integer rectangle that covers the box
    static Box FromRect(const RECT& rect);
    static Box Spanning(Vec a, Vec b);
};

enum class Kind { Arrow, Line, Rectangle, Ellipse, Pen, Marker, Text, Number, Censor };

// One editable object drawn on the capture.
//   Arrow, Line:              points = {start, end}
//   Rectangle, Ellipse, Censor: points = {corner, opposite corner}
//   Pen, Marker:              points = the freehand path
//   Text:                     points = {top-left of the box}; size = font height
//   Number:                   points = {center}; size = circle radius
struct Annotation {
    Kind kind = Kind::Line;
    std::vector<Vec> points;
    COLORREF color = RGB(229, 72, 77);
    float size = 4;           // Stroke width, font height or radius
    bool background = false;  // Text only: draw on a contrasting pill
    std::wstring text;
    int number = 0;
    Vec textExtent;  // Text only: measured size of the text, without padding

    bool operator==(const Annotation&) const = default;
};

// Padding between a text's box and its letters.
float TextPadding(const Annotation& text);
// Arrow head length for a stroke width.
float ArrowHeadLength(float width);

// Area covered on screen, including the stroke width.
Box Bounds(const Annotation& annotation);
bool HitTest(const Annotation& annotation, Vec point, float tolerance);

// Handles that reshape an annotation: both ends of arrows and lines, the
// eight box handles of rectangles, ellipses and censor boxes, none for the rest.
std::vector<Vec> Handles(const Annotation& annotation);
void MoveHandle(Annotation& annotation, size_t handle, Vec point, bool constrain);
void Translate(Annotation& annotation, float dx, float dy);

// Shift-drag helpers: 45° steps for lines, squares and circles for boxes.
Vec SnapAngle(Vec from, Vec to);
Vec SnapSquare(Vec from, Vec to);

float Distance(Vec a, Vec b);
float DistanceToSegment(Vec p, Vec a, Vec b);

// Contrasting text/background color for a fill (white on dark, black on light).
COLORREF ContrastColor(COLORREF color);

}  // namespace flashshot
