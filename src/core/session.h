#pragma once

#include "core/annotation.h"

#include <array>
#include <functional>
#include <optional>
#include <vector>

namespace flashshot {

enum class Tool { Select, Arrow, Line, Rectangle, Ellipse, Pen, Marker, Text, Number, Censor };
inline constexpr size_t kToolCount = 10;

// What each tool draws with. `size` is a stroke width, a font height or a
// number radius, in image pixels; `background` is only used by Text.
struct ToolStyle {
    COLORREF color = RGB(229, 72, 77);
    float size = 4;
    bool background = false;

    bool operator==(const ToolStyle&) const = default;
};
using ToolStyles = std::array<ToolStyle, kToolCount>;

ToolStyles DefaultToolStyles();
// The sizes offered for a tool, smallest first.
std::vector<float> SizePresets(Tool tool);
Tool ToolFor(Kind kind);
bool DrawsShapes(Tool tool);

struct Modifiers {
    bool shift = false;
    bool ctrl = false;
};

// One capture being edited: the region chosen on the frozen screen, the
// annotations on it, the active tool and the undo history. Pure logic in
// snapshot pixels, driven by the overlay window and by the tests.
class Session {
public:
    using MeasureText = std::function<Vec(const Annotation&)>;

    Session(int width, int height, const ToolStyles& styles, MeasureText measure);

    // --- Region ---
    bool HasSelection() const { return hasSelection_; }
    const RECT& selection() const { return selection_; }
    int width() const { return width_; }
    int height() const { return height_; }
    void SetSelection(const RECT& rect);
    bool NudgeSelection(int dx, int dy);

    // --- Tools and styles ---
    Tool tool() const { return tool_; }
    void SetTool(Tool tool);
    const ToolStyles& styles() const { return styles_; }
    // The tool whose options are shown: the edited or selected object's, else
    // the active drawing tool's. Nothing for the Select tool on its own.
    std::optional<Tool> OptionsTarget() const;
    ToolStyle CurrentStyle() const;
    // Change the selected object (one undo step) or the tool's next drawings.
    void SetColor(COLORREF color);
    void SetSize(float size);
    void SetBackground(bool background);

    // --- Annotations ---
    const std::vector<Annotation>& annotations() const { return state_.annotations; }
    int selected() const { return selected_; }
    int nextNumber() const { return state_.nextNumber; }
    void Deselect();
    bool DeleteSelected();
    bool NudgeSelected(float dx, float dy);

    // --- Pointer gestures ---
    enum class Target { Outside, Inside, SelectionHandle, Annotation, AnnotationHandle };
    struct Hit {
        Target target = Target::Outside;
        int index = -1;   // Annotation
        int handle = -1;  // Handle index, clockwise from the top-left corner
    };
    Hit HitTestAt(Vec point, float tolerance) const;

    enum class Release { None, SelectionMade, CopyRequested };
    void PointerDown(Vec point, Modifiers modifiers, float tolerance);
    void PointerMove(Vec point, Modifiers modifiers);
    Release PointerUp(Vec point, Modifiers modifiers);
    bool GestureActive() const { return gesture_ != Gesture::None; }
    // Escape during a drag: puts things back as they were before it.
    bool CancelGesture();

    // --- Text editing (the window shows an edit box meanwhile) ---
    // Starts editing `index`, or a new text at `point` when index is -1.
    void BeginTextEdit(int index, Vec point);
    int editingText() const { return editing_; }
    void SetEditingText(const std::wstring& text);
    // Keeps the edit (empty text removes the object) or reverts it.
    void FinishTextEdit(bool keep);

    // --- History ---
    bool CanUndo() const { return !undo_.empty() && Idle(); }
    bool CanRedo() const { return !redo_.empty() && Idle(); }
    bool Undo();
    bool Redo();

    // Area whose look changed since the last call, in snapshot pixels.
    Box TakeDirty();
    // True once after the region, the selected object or the tool changed.
    bool TakeLayoutChanged();

private:
    struct State {
        std::vector<Annotation> annotations;
        int nextNumber = 1;
        bool operator==(const State&) const = default;
    };
    enum class Gesture { None, NewSelection, MoveSelection, ResizeSelection, Draw, MoveAnnotation, ResizeAnnotation };

    bool Idle() const { return gesture_ == Gesture::None && editing_ < 0; }
    void Begin();         // Remembers the state an undo step would go back to
    bool CommitIfChanged();  // Records that step when something changed
    void MarkDirty(const Box& box) { dirty_ = dirty_.Union(box); }
    void MarkAnnotationDirty(int index);
    void MarkAllDirty();
    void SetLayoutChanged() { layoutChanged_ = true; }
    RECT ClampSelection(RECT rect) const;
    Annotation NewAnnotation(Kind kind, Vec point) const;
    void ApplyStyle(const std::function<void(Annotation&)>& change, const std::function<void(ToolStyle&)>& changeStyle);
    void Remeasure(Annotation& annotation) const;

    int width_;
    int height_;
    MeasureText measure_;
    ToolStyles styles_;
    Tool tool_ = Tool::Select;

    bool hasSelection_ = false;
    RECT selection_{};

    State state_;
    std::vector<State> undo_;
    std::vector<State> redo_;
    State before_;

    int selected_ = -1;
    int editing_ = -1;

    Gesture gesture_ = Gesture::None;
    Vec anchor_{};
    Vec last_{};
    RECT startSelection_{};
    int handle_ = -1;
    bool copyOnRelease_ = false;

    Box dirty_{};
    bool layoutChanged_ = false;
};

}  // namespace flashshot
