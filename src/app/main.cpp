#include "app/tray_app.h"
#include "core/render.h"

#include <commctrl.h>
#include <objbase.h>

#include <cwchar>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    // "--background" is used by the Start with Windows entry: start silently.
    const bool startedAtSignIn = wcsstr(commandLine, L"--background") != nullptr;

    // One instance per user session. A second launch only tells the running
    // one, which shows its window.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\FlashShotScreenCapture.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND running = FindWindowW(flashshot::kTrayWindowClass, nullptr); running && !startedAtSignIn) {
            // Let the running copy bring its window to the front.
            DWORD process = 0;
            GetWindowThreadProcessId(running, &process);
            AllowSetForegroundWindow(process);
            PostMessageW(running, flashshot::WM_APP_ALREADY_RUNNING, 0, 0);
        }
        return 0;
    }

    // Image encoding (WIC) and the file dialogs are COM.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);

    int exitCode = 0;
    {
        flashshot::GdiplusSession gdiplus;
        flashshot::TrayApp app;
        if (gdiplus.ok() && app.Initialize(instance, startedAtSignIn)) {
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                if (app.PreTranslate(message)) continue;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        } else {
            exitCode = 1;
        }
    }
    CoUninitialize();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return exitCode;
}
