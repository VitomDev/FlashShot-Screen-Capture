#pragma once

#include <windows.h>

namespace flashshot {

// Takes Print Screen through a low-level keyboard hook. Windows 11 gives the
// key to the Snipping Tool before any registered shortcut, but keyboard hooks
// see it first. Only Print Screen on its own is taken (see PrintScreenFilter);
// every other key passes straight through. The hook runs on a thread of its
// own, so it always answers in the time Windows allows, even while a capture
// is being set up on the main thread.
class PrintScreenHook {
public:
    PrintScreenHook() = default;
    PrintScreenHook(const PrintScreenHook&) = delete;
    PrintScreenHook& operator=(const PrintScreenHook&) = delete;
    ~PrintScreenHook() { Stop(); }

    // Posts `message` to `target` on each press. With `onlyForOwnWindows` the
    // key is taken only while a FlashShot window is in front, so Settings can
    // record it without blocking it everywhere else.
    bool Start(HWND target, UINT message, bool onlyForOwnWindows);
    void Stop();
    bool running() const { return thread_ != nullptr; }

private:
    static DWORD WINAPI ThreadMain(void* parameter);
    static LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam);

    HANDLE thread_ = nullptr;
    DWORD threadId_ = 0;
};

}  // namespace flashshot
