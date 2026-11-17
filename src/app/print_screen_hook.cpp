#include "app/print_screen_hook.h"

#include "core/hotkey.h"

namespace flashshot {

namespace {

// There is one hook at most; its procedure has no context pointer. Everything
// but `filter` is set before the hook thread starts and only read after.
struct HookState {
    HWND target = nullptr;
    UINT message = 0;
    bool onlyForOwnWindows = false;
    PrintScreenFilter filter;  // Hook thread only
    HANDLE ready = nullptr;
    bool installed = false;
};
HookState hookState;

bool ModifiersHeld() {
    for (const int key : {VK_CONTROL, VK_MENU, VK_SHIFT, VK_LWIN, VK_RWIN}) {
        if (GetAsyncKeyState(key) < 0) return true;
    }
    return false;
}

bool OwnWindowInFront() {
    DWORD process = 0;
    HWND front = GetForegroundWindow();
    return front && GetWindowThreadProcessId(front, &process) && process == GetCurrentProcessId();
}

}  // namespace

bool PrintScreenHook::Start(HWND target, UINT message, bool onlyForOwnWindows) {
    Stop();
    hookState = {};
    hookState.target = target;
    hookState.message = message;
    hookState.onlyForOwnWindows = onlyForOwnWindows;
    hookState.ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hookState.ready) return false;
    thread_ = CreateThread(nullptr, 0, ThreadMain, nullptr, 0, &threadId_);
    if (thread_) WaitForSingleObject(hookState.ready, INFINITE);
    CloseHandle(hookState.ready);
    hookState.ready = nullptr;
    if (thread_ && !hookState.installed) {
        WaitForSingleObject(thread_, INFINITE);
        CloseHandle(thread_);
        thread_ = nullptr;
        threadId_ = 0;
    }
    return thread_ != nullptr;
}

void PrintScreenHook::Stop() {
    if (!thread_) return;
    PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    WaitForSingleObject(thread_, INFINITE);
    CloseHandle(thread_);
    thread_ = nullptr;
    threadId_ = 0;
}

DWORD WINAPI PrintScreenHook::ThreadMain(void*) {
    // Create the message queue first so the WM_QUIT from Stop is never lost.
    MSG message;
    PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandleW(nullptr), 0);
    hookState.installed = hook != nullptr;
    SetEvent(hookState.ready);
    if (!hook) return 1;
    // Low-level hooks are called through this thread's message loop.
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    }
    UnhookWindowsHookEx(hook);
    return 0;
}

LRESULT CALLBACK PrintScreenHook::KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto& key = *reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        if (key.vkCode == VK_SNAPSHOT) {
            const bool down = (key.flags & LLKHF_UP) == 0;
            const bool leave = down && (ModifiersHeld() || (hookState.onlyForOwnWindows && !OwnWindowInFront()));
            switch (hookState.filter.OnKey(down, leave)) {
                case PrintScreenFilter::Action::Capture:
                    PostMessageW(hookState.target, hookState.message, 0, 0);
                    return 1;
                case PrintScreenFilter::Action::Swallow:
                    return 1;
                case PrintScreenFilter::Action::Pass:
                    break;
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

}  // namespace flashshot
