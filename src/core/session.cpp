#include "core/session.h"

#include "core/geometry.h"

#include <algorithm>
#include <cmath>

namespace flashshot {

namespace {

constexpr size_t kMaxUndo = 200;

size_t Index(Tool tool) { return static_cast<size_t>(tool); }

int Round(float value) { return static_cast<int>(std::lround(value)); }

}  // namespace

ToolStyles DefaultToolStyles() {
    ToolStyles styles;
    styles.fill(ToolStyle{RGB(229, 72, 77), 4, false});
    styles[Index(Tool::Marker)] = {RGB(255, 213, 0), 18, false};
    styles[Index(Tool::Text)] = {RGB(229, 72, 77), 24, false};
    styles[Index(Tool::Number)] = {RGB(229, 72, 77), 16, false};
    styles[Index(Tool::Censor)] = {RGB(0, 0, 0), 0, false};
    return styles;
}

std::vector<float> SizePresets(Tool tool) {
    switch (tool) {
        case Tool::Marker: return {10, 18, 28, 40};
        case Tool::Text: return {16, 24, 36, 52};
        case Tool::Number: return {12, 16, 22, 30};
        case Tool::Censor:
        case Tool::Select: return {};
        default: return {2, 4, 7, 12};
    }
}

Tool ToolFor(Kind kind) {
    switch (kind) {
        case Kind::Arrow: return Tool::Arrow;
        case Kind::Line: return Tool::Line;
        case Kind::Rectangle: return Tool::Rectangle;
        case Kind::Ellipse: return Tool::Ellipse;
        case Kind::Pen: return Tool::Pen;
        case Kind::Marker: return Tool::Marker;
        case Kind::Text: return Tool::Text;
        case Kind::Number: return Tool::Number;
        case Kind::Censor: return Tool::Censor;
    }
    return Tool::Select;
}

bool DrawsShapes(Tool tool) { return tool != Tool::Select && tool != Tool::Text && tool != Tool::Number; }

Session::Session(int width, int height, const ToolStyles& styles, MeasureText measure)
    : width_(width), height_(height), measure_(std::move(measure)), styles_(styles) {}

// --- Region ----------------------------------------------------------------------

RECT Session::ClampSelection(RECT rect) const {
    // Keeps the size and slides the region back inside the snapshot.
    const LONG w = std::min<LONG>(rect.right - rect.left, width_);
    const LONG h = std::min<LONG>(rect.bottom - rect.top, height_);
    rect.left = std::clamp<LONG>(rect.left, 0, width_ - w);
    rect.top = std::clamp<LONG>(rect.top, 0, height_ - h);
    return {rect.left, rect.top, rect.left + w, rect.top + h};
}

void Session::SetSelection(const RECT& rect) {
    RECT clamped{std::clamp<LONG>(rect.left, 0, width_), std::clamp<LONG>(rect.top, 0, height_),
                 std::clamp<LONG>(rect.right, 0, width_), std::clamp<LONG>(rect.bottom, 0, height_)};
    selection_ = clamped;
    hasSelection_ = IsUsableSelection(clamped);
    MarkAllDirty();
    SetLayoutChanged();
}

bool Session::NudgeSelection(int dx, int dy) {
    if (!hasSelection_ || !Idle()) return false;
    RECT moved = selection_;
    OffsetRect(&moved, dx, dy);
    moved = ClampSelection(moved);
    if (EqualRect(&moved, &selection_)) return false;
    selection_ = moved;
    MarkAllDirty();
    SetLayoutChanged();
    return true;
}

// --- Tools and styles --------------------------------------------------------------

void Session::SetTool(Tool tool) {
    if (editing_ >= 0) FinishTextEdit(true);
    tool_ = tool;
    if (tool != Tool::Select) Deselect();
    SetLayoutChanged();
}

std::optional<Tool> Session::OptionsTarget() const {
    if (editing_ >= 0) return Tool::Text;
    if (selected_ >= 0) return ToolFor(state_.annotations[selected_].kind);
    if (tool_ != Tool::Select) return tool_;
    return std::nullopt;
}

ToolStyle Session::CurrentStyle() const {
    const int index = editing_ >= 0 ? editing_ : selected_;
    if (index >= 0) {
        const Annotation& a = state_.annotations[index];
        return {a.color, a.size, a.background};
    }
    return styles_[Index(tool_)];
}

void Session::Remeasure(Annotation& annotation) const {
    if (annotation.kind == Kind::Text && measure_) annotation.textExtent = measure_(annotation);
}

void Session::ApplyStyle(const std::function<void(Annotation&)>& change, const std::function<void(ToolStyle&)>& changeStyle) {
    const int index = editing_ >= 0 ? editing_ : selected_;
    if (index >= 0) {
        Annotation& annotation = state_.annotations[index];
        MarkAnnotationDirty(index);
        // Inside a text edit the change becomes part of that edit's undo step.
        if (editing_ < 0) Begin();
        change(annotation);
        Remeasure(annotation);
        MarkAnnotationDirty(index);
        if (editing_ < 0) CommitIfChanged();
        changeStyle(styles_[Index(ToolFor(annotation.kind))]);
    } else if (tool_ != Tool::Select) {
        changeStyle(styles_[Index(tool_)]);
    }
    SetLayoutChanged();
}

void Session::SetColor(COLORREF color) {
    ApplyStyle([color](Annotation& a) { a.color = color; }, [color](ToolStyle& s) { s.color = color; });
}

void Session::SetSize(float size) {
    ApplyStyle([size](Annotation& a) { a.size = size; }, [size](ToolStyle& s) { s.size = size; });
}

void Session::SetBackground(bool background) {
    ApplyStyle([background](Annotation& a) { a.background = background; },
               [background](ToolStyle& s) { s.background = background; });
}

// --- Annotations ------------------------------------------------------------------

void Session::Deselect() {
    if (selected_ < 0) return;
    MarkAnnotationDirty(selected_);
    selected_ = -1;
    SetLayoutChanged();
}

bool Session::DeleteSelected() {
    if (selected_ < 0 || !Idle()) return false;
    Begin();
    MarkAnnotationDirty(selected_);
    state_.annotations.erase(state_.annotations.begin() + selected_);
    selected_ = -1;
    SetLayoutChanged();
    return CommitIfChanged();
}

bool Session::NudgeSelected(float dx, float dy) {
    if (selected_ < 0 || !Idle()) return false;
    Begin();
    MarkAnnotationDirty(selected_);
    Translate(state_.annotations[selected_], dx, dy);
    MarkAnnotationDirty(selected_);
    SetLayoutChanged();
    return CommitIfChanged();
}

Annotation Session::NewAnnotation(Kind kind, Vec point) const {
    const ToolStyle& style = styles_[Index(ToolFor(kind))];
    Annotation annotation;
    annotation.kind = kind;
    annotation.color = style.color;
    annotation.size = style.size;
    annotation.background = style.background;
    annotation.points = {point};
    if (kind != Kind::Pen && kind != Kind::Marker && kind != Kind::Text && kind != Kind::Number) {
        annotation.points.push_back(point);
    }
    return annotation;
}

// --- Pointer gestures ---------------------------------------------------------------

Session::Hit Session::HitTestAt(Vec p, float tolerance) const {
    if (!hasSelection_) return {};
    if (tool_ == Tool::Select && selected_ >= 0) {
        const auto handles = Handles(state_.annotations[selected_]);
        for (size_t i = 0; i < handles.size(); ++i) {
            if (Distance(p, handles[i]) <= tolerance * 1.5f) return {Target::AnnotationHandle, selected_, static_cast<int>(i)};
        }
    }

    // The region's edges and corners resize it whatever the tool.
    const Box box = Box::FromRect(selection_);
    if (box.Inflated(tolerance).Contains(p)) {
        const bool left = std::abs(p.x - box.left) <= tolerance, right = std::abs(p.x - box.right) <= tolerance;
        const bool top = std::abs(p.y - box.top) <= tolerance, bottom = std::abs(p.y - box.bottom) <= tolerance;
        int handle = -1;
        if (top && left) handle = 0;
        else if (top && right) handle = 2;
        else if (bottom && right) handle = 4;
        else if (bottom && left) handle = 6;
        else if (top) handle = 1;
        else if (right) handle = 3;
        else if (bottom) handle = 5;
        else if (left) handle = 7;
        if (handle >= 0) return {Target::SelectionHandle, -1, handle};
    }

    if (tool_ == Tool::Select || tool_ == Tool::Text) {
        // Topmost first: the last drawn object wins.
        for (int i = static_cast<int>(state_.annotations.size()) - 1; i >= 0; --i) {
            const Annotation& annotation = state_.annotations[i];
            if (tool_ == Tool::Text && annotation.kind != Kind::Text) continue;
            if (i != editing_ && HitTest(annotation, p, tolerance)) return {Target::Annotation, i};
        }
    }
    return {box.Contains(p) ? Target::Inside : Target::Outside};
}

void Session::PointerDown(Vec p, Modifiers modifiers, float tolerance) {
    if (gesture_ != Gesture::None) return;
    if (editing_ >= 0) FinishTextEdit(true);
    anchor_ = last_ = p;
    startSelection_ = selection_;

    if (!hasSelection_) {
        gesture_ = Gesture::NewSelection;
        copyOnRelease_ = modifiers.ctrl;
        selection_ = SelectionRect({Round(p.x), Round(p.y)}, {Round(p.x), Round(p.y)}, {0, 0, width_, height_});
        SetLayoutChanged();
        return;
    }

    const Hit hit = HitTestAt(p, tolerance);
    switch (hit.target) {
        case Target::AnnotationHandle:
            Begin();
            gesture_ = Gesture::ResizeAnnotation;
            handle_ = hit.handle;
            return;
        case Target::SelectionHandle:
            gesture_ = Gesture::ResizeSelection;
            handle_ = hit.handle;
            return;
        case Target::Annotation:
            if (tool_ == Tool::Text) {
                BeginTextEdit(hit.index, p);
                return;
            }
            Begin();
            if (selected_ != hit.index) {
                Deselect();
                selected_ = hit.index;
                MarkAnnotationDirty(selected_);
                SetLayoutChanged();
            }
            gesture_ = Gesture::MoveAnnotation;
            return;
        case Target::Outside:
            Deselect();
            return;
        case Target::Inside:
            break;
    }

    switch (tool_) {
        case Tool::Select:
            Deselect();
            gesture_ = Gesture::MoveSelection;
            return;
        case Tool::Text:
            BeginTextEdit(-1, p);
            return;
        case Tool::Number: {
            Begin();
            Annotation number = NewAnnotation(Kind::Number, p);
            number.number = state_.nextNumber++;
            state_.annotations.push_back(number);
            MarkAnnotationDirty(static_cast<int>(state_.annotations.size()) - 1);
            CommitIfChanged();
            return;
        }
        default: {
            static constexpr Kind kinds[] = {Kind::Line, Kind::Arrow, Kind::Line, Kind::Rectangle, Kind::Ellipse,
                                             Kind::Pen, Kind::Marker, Kind::Text, Kind::Number, Kind::Censor};
            Begin();
            state_.annotations.push_back(NewAnnotation(kinds[Index(tool_)], p));
            gesture_ = Gesture::Draw;
            return;
        }
    }
}

void Session::PointerMove(Vec p, Modifiers modifiers) {
    const Vec delta{p.x - last_.x, p.y - last_.y};
    switch (gesture_) {
        case Gesture::None:
            return;
        case Gesture::NewSelection:
            selection_ = SelectionRect({Round(anchor_.x), Round(anchor_.y)}, {Round(p.x), Round(p.y)}, {0, 0, width_, height_});
            SetLayoutChanged();
            break;
        case Gesture::MoveSelection: {
            RECT moved = startSelection_;
            OffsetRect(&moved, Round(p.x - anchor_.x), Round(p.y - anchor_.y));
            selection_ = ClampSelection(moved);
            MarkAllDirty();
            SetLayoutChanged();
            break;
        }
        case Gesture::ResizeSelection: {
            RECT r = startSelection_;
            const LONG x = std::clamp<LONG>(Round(p.x), 0, width_), y = std::clamp<LONG>(Round(p.y), 0, height_);
            if (handle_ == 0 || handle_ == 6 || handle_ == 7) r.left = x;
            if (handle_ == 2 || handle_ == 3 || handle_ == 4) r.right = x;
            if (handle_ == 0 || handle_ == 1 || handle_ == 2) r.top = y;
            if (handle_ == 4 || handle_ == 5 || handle_ == 6) r.bottom = y;
            // Dragging past the opposite edge flips the region instead of collapsing it.
            selection_ = {std::min(r.left, r.right), std::min(r.top, r.bottom), std::max(r.left, r.right), std::max(r.top, r.bottom)};
            if (selection_.right == selection_.left) selection_.right = std::min<LONG>(selection_.left + 1, width_);
            if (selection_.bottom == selection_.top) selection_.bottom = std::min<LONG>(selection_.top + 1, height_);
            MarkAllDirty();
            SetLayoutChanged();
            break;
        }
        case Gesture::Draw: {
            const int index = static_cast<int>(state_.annotations.size()) - 1;
            Annotation& annotation = state_.annotations[index];
            MarkAnnotationDirty(index);
            switch (annotation.kind) {
                case Kind::Arrow:
                case Kind::Line:
                    annotation.points[1] = modifiers.shift ? SnapAngle(anchor_, p) : p;
                    break;
                case Kind::Pen:
                case Kind::Marker:
                    if (Distance(annotation.points.back(), p) >= 1) annotation.points.push_back(p);
                    break;
                default:
                    annotation.points[1] = modifiers.shift ? SnapSquare(anchor_, p) : p;
                    break;
            }
            MarkAnnotationDirty(index);
            break;
        }
        case Gesture::MoveAnnotation:
            MarkAnnotationDirty(selected_);
            Translate(state_.annotations[selected_], delta.x, delta.y);
            MarkAnnotationDirty(selected_);
            SetLayoutChanged();
            break;
        case Gesture::ResizeAnnotation:
            MarkAnnotationDirty(selected_);
            MoveHandle(state_.annotations[selected_], static_cast<size_t>(handle_), p, modifiers.shift);
            MarkAnnotationDirty(selected_);
            SetLayoutChanged();
            break;
    }
    last_ = p;
}

Session::Release Session::PointerUp(Vec p, Modifiers modifiers) {
    if (gesture_ == Gesture::None) return Release::None;
    PointerMove(p, modifiers);
    const Gesture gesture = gesture_;
    gesture_ = Gesture::None;
    SetLayoutChanged();

    switch (gesture) {
        case Gesture::NewSelection:
            // A click without a drag does not make a region.
            if (!IsUsableSelection(selection_)) {
                selection_ = {};
                return Release::None;
            }
            hasSelection_ = true;
            MarkAllDirty();
            return copyOnRelease_ ? Release::CopyRequested : Release::SelectionMade;
        case Gesture::Draw: {
            const Annotation& drawn = state_.annotations.back();
            bool keep = true;
            if (drawn.kind == Kind::Arrow || drawn.kind == Kind::Line) {
                keep = Distance(drawn.points[0], drawn.points[1]) >= 3;
            } else if (drawn.kind != Kind::Pen && drawn.kind != Kind::Marker) {
                const Box box = Box::Spanning(drawn.points[0], drawn.points[1]);
                keep = box.width() >= 2 && box.height() >= 2;
            }
            if (!keep) {
                MarkAnnotationDirty(static_cast<int>(state_.annotations.size()) - 1);
                state_.annotations.pop_back();
            }
            CommitIfChanged();
            break;
        }
        case Gesture::MoveAnnotation:
        case Gesture::ResizeAnnotation:
            CommitIfChanged();
            break;
        default:
            break;
    }
    return Release::None;
}

bool Session::CancelGesture() {
    if (gesture_ == Gesture::None) return false;
    switch (gesture_) {
        case Gesture::NewSelection:
            selection_ = {};
            break;
        case Gesture::MoveSelection:
        case Gesture::ResizeSelection:
            selection_ = startSelection_;
            MarkAllDirty();
            break;
        default:
            MarkAllDirty();
            state_ = before_;
            if (selected_ >= static_cast<int>(state_.annotations.size())) selected_ = -1;
            break;
    }
    gesture_ = Gesture::None;
    SetLayoutChanged();
    return true;
}

// --- Text editing ----------------------------------------------------------------------

void Session::BeginTextEdit(int index, Vec point) {
    Deselect();
    Begin();
    if (index < 0) {
        Annotation text = NewAnnotation(Kind::Text, point);
        // The click lands on the middle of the first line.
        const float padding = TextPadding(text);
        text.points[0] = {std::round(point.x - padding), std::round(point.y - padding - text.size * 0.6f)};
        Remeasure(text);
        state_.annotations.push_back(text);
        index = static_cast<int>(state_.annotations.size()) - 1;
    }
    editing_ = index;
    MarkAnnotationDirty(index);
    SetLayoutChanged();
}

void Session::SetEditingText(const std::wstring& text) {
    if (editing_ < 0) return;
    Annotation& annotation = state_.annotations[editing_];
    MarkAnnotationDirty(editing_);
    annotation.text = text;
    Remeasure(annotation);
    MarkAnnotationDirty(editing_);
}

void Session::FinishTextEdit(bool keep) {
    if (editing_ < 0) return;
    MarkAnnotationDirty(editing_);
    if (!keep) {
        state_ = before_;
    } else {
        const std::wstring& text = state_.annotations[editing_].text;
        if (text.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
            state_.annotations.erase(state_.annotations.begin() + editing_);
        }
        CommitIfChanged();
    }
    editing_ = -1;
    SetLayoutChanged();
}

// --- History ----------------------------------------------------------------------------

void Session::Begin() { before_ = state_; }

bool Session::CommitIfChanged() {
    if (state_ == before_) return false;
    undo_.push_back(std::move(before_));
    if (undo_.size() > kMaxUndo) undo_.erase(undo_.begin());
    redo_.clear();
    before_ = {};
    return true;
}

bool Session::Undo() {
    if (!CanUndo()) return false;
    MarkAllDirty();
    redo_.push_back(std::move(state_));
    state_ = std::move(undo_.back());
    undo_.pop_back();
    selected_ = -1;
    SetLayoutChanged();
    return true;
}

bool Session::Redo() {
    if (!CanRedo()) return false;
    MarkAllDirty();
    undo_.push_back(std::move(state_));
    state_ = std::move(redo_.back());
    redo_.pop_back();
    selected_ = -1;
    SetLayoutChanged();
    return true;
}

// --- Repaint tracking ---------------------------------------------------------------------

void Session::MarkAnnotationDirty(int index) {
    if (index >= 0 && index < static_cast<int>(state_.annotations.size())) MarkDirty(Bounds(state_.annotations[index]));
}

void Session::MarkAllDirty() { MarkDirty({0, 0, static_cast<float>(width_), static_cast<float>(height_)}); }

Box Session::TakeDirty() {
    const Box dirty = dirty_;
    dirty_ = {};
    return dirty;
}

bool Session::TakeLayoutChanged() {
    const bool changed = layoutChanged_;
    layoutChanged_ = false;
    return changed;
}

}  // namespace flashshot
