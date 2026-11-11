#pragma once

// GDI+ headers expect min/max and the COM declarations that NOMINMAX and
// WIN32_LEAN_AND_MEAN leave out.
#include <windows.h>
#include <objidl.h>

#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>
