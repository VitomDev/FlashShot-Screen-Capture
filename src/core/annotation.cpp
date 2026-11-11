#include "core/annotation.h"

#include <algorithm>
#include <cmath>

namespace flashshot {

namespace {

constexpr float kPi = 3.14159265f;

Box BoxOf(const Annotation& annotation) {
    if (annotation.points.size() < 2) return {};
    return Box::Spanning(annotation.points[0], annotation.points[1]);
}

Box PathBox(const std::vector<Vec>& points) {
    Box box{points[0].x, points[0].y, points[0].x, points[0].y};
    for (const Vec& p : points) {
        box.left = std::min(box.left, p.x);
        box.top = std::min(box.top, p.y);
        box.right = std::max(box.right, p.x);
        box.bottom = std::max(box.bottom, p.y);
    }
    return box;
}

bool IsBoxKind(Kind kind) { return kind == Kind::Rectangle || kind == Kind::Ellipse || kind == Kind::Censor; }

}  // namespace

Box Box::Union(const Box& other) const {
    if (empty()) return other;
    if (other.empty()) return *this;
    return {std::min(left, other.left), std::min(top, other.top), std::max(right, other.right), std::max(bottom, other.bottom)};
}

bool Box::Intersects(const Box& other) const {
    return left < other.right && other.left < right && top < other.bottom && other.top < bottom;
}

RECT Box::ToRect() const {
    return {static_cast<LONG>(std::floor(left)), static_cast<LONG>(std::floor(top)), static_cast<LONG>(std::ceil(right)),
            static_cast<LONG>(std::ceil(bottom))};
}

Box Box::FromRect(const RECT& rect) {
    return {static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right),
            static_cast<float>(rect.bottom)};
}

Box Box::Spanning(Vec a, Vec b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)}; }

float Distance(Vec a, Vec b) { return std::hypot(a.x - b.x, a.y - b.y); }

float DistanceToSegment(Vec p, Vec a, Vec b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0) return Distance(p, a);
    const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0f, 1.0f);
    return Distance(p, {a.x + t * dx, a.y + t * dy});
}

float TextPadding(const Annotation& text) { return std::round(text.size * 0.25f); }

float ArrowHeadLength(float width) { return std::max(12.0f, width * 3.5f + 6); }

COLORREF ContrastColor(COLORREF color) {
    const int luma = (GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114) / 1000;
    return luma > 150 ? RGB(24, 24, 27) : RGB(255, 255, 255);
}

Box Bounds(const Annotation& annotation) {
    if (annotation.points.empty()) return {};
    const Vec origin = annotation.points[0];
    switch (annotation.kind) {
        case Kind::Text: {
            const float padding = TextPadding(annotation);
            return {origin.x, origin.y, origin.x + annotation.textExtent.x + 2 * padding,
                    origin.y + annotation.textExtent.y + 2 * padding};
        }
        case Kind::Number:
            return Box{origin.x, origin.y, origin.x, origin.y}.Inflated(annotation.size + 2);
        case Kind::Censor:
            return BoxOf(annotation).Inflated(1);
        case Kind::Arrow:
            return PathBox(annotation.points).Inflated(ArrowHeadLength(annotation.size) + 2);
        default:
            return PathBox(annotation.points).Inflated(annotation.size / 2 + 2);
    }
}

bool HitTest(const Annotation& annotation, Vec p, float tolerance) {
    if (annotation.points.empty()) return false;
    const float reach = annotation.size / 2 + tolerance;
    switch (annotation.kind) {
        case Kind::Arrow:
        case Kind::Line:
            return annotation.points.size() >= 2 && DistanceToSegment(p, annotation.points[0], annotation.points[1]) <= reach;
        case Kind::Pen:
        case Kind::Marker:
            if (annotation.points.size() == 1) return Distance(p, annotation.points[0]) <= reach;
            for (size_t i = 1; i < annotation.points.size(); ++i) {
                if (DistanceToSegment(p, annotation.points[i - 1], annotation.points[i]) <= reach) return true;
            }
            return false;
        case Kind::Rectangle: {
            // Outlines are picked by their line, so the inside stays free for drawing.
            const Box box = BoxOf(annotation);
            if (!box.Inflated(reach).Contains(p)) return false;
            const Box inner = box.Inflated(-reach);
            return inner.empty() || !inner.Contains(p);
        }
        case Kind::Ellipse: {
            const Box box = BoxOf(annotation);
            const float rx = std::max(box.width() / 2, 1.0f), ry = std::max(box.height() / 2, 1.0f);
            const float nx = (p.x - (box.left + rx)) / rx, ny = (p.y - (box.top + ry)) / ry;
            const float radial = std::sqrt(nx * nx + ny * ny);
            return std::abs(radial - 1) * std::min(rx, ry) <= reach;
        }
        case Kind::Number:
            return Distance(p, annotation.points[0]) <= annotation.size + tolerance;
        case Kind::Text:
        case Kind::Censor:
            return Bounds(annotation).Inflated(tolerance).Contains(p);
    }
    return false;
}

std::vector<Vec> Handles(const Annotation& annotation) {
    if (annotation.kind == Kind::Arrow || annotation.kind == Kind::Line) return annotation.points;
    if (!IsBoxKind(annotation.kind)) return {};
    const Box b = BoxOf(annotation);
    const float cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2;
    // Clockwise from the top-left corner; odd indexes are edges.
    return {{b.left, b.top}, {cx, b.top}, {b.right, b.top}, {b.right, cy},
            {b.right, b.bottom}, {cx, b.bottom}, {b.left, b.bottom}, {b.left, cy}};
}

void MoveHandle(Annotation& annotation, size_t handle, Vec point, bool constrain) {
    if (annotation.kind == Kind::Arrow || annotation.kind == Kind::Line) {
        if (handle > 1 || annotation.points.size() < 2) return;
        annotation.points[handle] = constrain ? SnapAngle(annotation.points[1 - handle], point) : point;
        return;
    }
    if (!IsBoxKind(annotation.kind) || handle > 7) return;
    Box b = BoxOf(annotation);
    if (handle % 2 == 0) {
        // Corners move freely against the opposite corner.
        const Vec opposite = Handles(annotation)[(handle + 4) % 8];
        const Vec corner = constrain ? SnapSquare(opposite, point) : point;
        annotation.points = {opposite, corner};
        return;
    }
    if (handle == 1) b.top = point.y;
    if (handle == 3) b.right = point.x;
    if (handle == 5) b.bottom = point.y;
    if (handle == 7) b.left = point.x;
    annotation.points = {{b.left, b.top}, {b.right, b.bottom}};
}

void Translate(Annotation& annotation, float dx, float dy) {
    for (Vec& p : annotation.points) {
        p.x += dx;
        p.y += dy;
    }
}

Vec SnapAngle(Vec from, Vec to) {
    const float dx = to.x - from.x, dy = to.y - from.y;
    const float length = std::hypot(dx, dy);
    if (length <= 0) return to;
    const float step = kPi / 4;
    const float angle = std::round(std::atan2(dy, dx) / step) * step;
    // Diagonals keep the drag's projection so they follow the cursor.
    const float projected = dx * std::cos(angle) + dy * std::sin(angle);
    return {from.x + std::round(projected * std::cos(angle)), from.y + std::round(projected * std::sin(angle))};
}

Vec SnapSquare(Vec from, Vec to) {
    const float dx = to.x - from.x, dy = to.y - from.y;
    const float side = std::max(std::abs(dx), std::abs(dy));
    return {from.x + (dx < 0 ? -side : side), from.y + (dy < 0 ? -side : side)};
}

}  // namespace flashshot
