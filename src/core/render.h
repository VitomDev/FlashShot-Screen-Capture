#pragma once

#include "core/annotation.h"
#include "core/image.h"

#include <windows.h>

#include <vector>

namespace flashshot {

// GDI+ must be started once per process before anything is drawn.
class GdiplusSession {
public:
    GdiplusSession();
    ~GdiplusSession();
    GdiplusSession(const GdiplusSession&) = delete;
    GdiplusSession& operator=(const GdiplusSession&) = delete;
    bool ok() const { return token_ != 0; }

private:
    ULONG_PTR token_ = 0;
};

// The font of text annotations; the editor's text box uses the same one so
// typing looks exactly like the result. The caller deletes it.
HFONT CreateTextFont(float size);
// Size of a text's letters (without padding), in pixels.
Vec MeasureTextExtent(const Annotation& text);
// Lines of a text, accepting "\r\n" and "\n".
std::vector<std::wstring> SplitLines(const std::wstring& text);

// Draws annotations in order onto `dc`, whose pixel (0, 0) shows snapshot
// pixel `origin`, limited to `clip` (in dc pixels). The text being edited
// (`editing`) only gets its background: the edit box shows its letters.
// The same function draws the editor and the exported image.
void DrawAnnotations(HDC dc, POINT origin, const RECT& clip, const std::vector<Annotation>& annotations, int editing = -1);

// The final image: `region` of the snapshot (top-down BGRA) with the
// annotations burned in and cut at its edges. Opaque, at native resolution.
BgraImage Compose(const uint8_t* pixels, int width, int height, const RECT& region, const std::vector<Annotation>& annotations);

}  // namespace flashshot
